/*
 * servidor.c — Clientes (N hilos), Matchmaker, control de M mesas y
 * manejo de SIGINT con un hilo dedicado (sigwait).
 */
#include "common.h"
#include "servidor.h"
#include "log.h"

#include <signal.h>

static void *xcalloc(size_t n, size_t tam)
{
    void *p = calloc(n, tam);
    if (p == NULL) {
        perror("calloc");
        exit(1);
    }
    return p;
}

bool servidor_apagando(Servidor *s)
{
    return atomic_load(&s->apagando);
}

void servidor_init(Servidor *s, int N, int M, const Config *cfg)
{
    memset(s, 0, sizeof *s);
    s->N = N;
    s->M = M;
    s->cfg = cfg;
    atomic_init(&s->apagando, false);
    CHECK(pthread_mutex_init(&s->mtx, NULL));
    CHECK(pthread_cond_init(&s->cond_matchmaker, NULL));
    CHECK(pthread_cond_init(&s->cond_llegada, NULL));
    CHECK(pthread_cond_init(&s->cond_resultados, NULL));
    s->cola = xcalloc((size_t)N, sizeof *s->cola);
    s->slot_ocupado = xcalloc((size_t)M, sizeof *s->slot_ocupado);
    s->jugadores = xcalloc((size_t)N, sizeof *s->jugadores);
    s->clientes = xcalloc((size_t)N, sizeof *s->clientes);
    s->jueces = xcalloc((size_t)N / 2 + 1, sizeof *s->jueces);
    for (int i = 0; i < N; i++) {
        Jugador *j = &s->jugadores[i];
        j->srv = s;
        j->id = i + 1;
        j->resultado = RES_PENDIENTE;
        j->semilla = cfg->seed ^ ((unsigned)(i + 1) * 2654435761u);
    }
}

/* Requiere mtx. */
static void fijar_resultado(Servidor *s, int idx, ResultadoJugador r, const char *motivo)
{
    s->jugadores[idx].resultado = r;
    snprintf(s->jugadores[idx].motivo, sizeof s->jugadores[idx].motivo, "%s", motivo);
}

/* ---- Cliente ----------------------------------------------------------- */

static void *cliente_hilo(void *arg)
{
    Jugador *j = arg;
    Servidor *s = j->srv;
    int idx = j->id - 1;

    /* 1. Retardo de llegada (despertable por el apagado). */
    int ms = azar_rango(&j->semilla, s->cfg->arrival_min, s->cfg->arrival_max);
    struct timespec fin = deadline_ms(ms);
    CHECK(pthread_mutex_lock(&s->mtx));
    while (!servidor_apagando(s) && esperar_hasta(&s->cond_llegada, &s->mtx, &fin))
        ;

    /* 2. Encolarse (o no, si el servidor ya no acepta jugadores). */
    if (servidor_apagando(s)) {
        fijar_resultado(s, idx, RES_NO_LLEGO, "servidor apagado antes de su llegada");
        CHECK(pthread_mutex_unlock(&s->mtx));
        return NULL;
    }
    s->cola[s->cola_ini + s->cola_len++] = idx;
    s->llegados++;
    consola("[srv] Jugador %d llegó (en cola: %d)", j->id, s->cola_len);
    CHECK(pthread_cond_signal(&s->cond_matchmaker));   /* único esperador */

    /* 3. Esperar el resultado. Todo jugador encolado recibe uno: lo escribe
     * el Juez, o el Matchmaker (sin rival / par en tránsito al apagar). */
    while (s->jugadores[idx].resultado == RES_PENDIENTE)
        CHECK(pthread_cond_wait(&s->cond_resultados, &s->mtx));
    CHECK(pthread_mutex_unlock(&s->mtx));
    return NULL;
}

/* ---- Matchmaker -------------------------------------------------------- */

static void *matchmaker_hilo(void *arg)
{
    Servidor *s = arg;

    CHECK(pthread_mutex_lock(&s->mtx));
    for (;;) {
        /* Esperar 2 jugadores en cola (o que no lleguen más, o apagado). */
        while (!servidor_apagando(s) && s->cola_len < 2 && s->llegados < s->N)
            CHECK(pthread_cond_wait(&s->cond_matchmaker, &s->mtx));
        if (servidor_apagando(s) || s->cola_len < 2)
            break;

        /* Emparejar: el primero en la cola es Puma, el segundo Perros. */
        int jp = s->cola[s->cola_ini++];
        int jd = s->cola[s->cola_ini++];
        s->cola_len -= 2;
        int partida = ++s->partidas;
        s->jugadores[jp].rol = ROL_PUMA;
        s->jugadores[jd].rol = ROL_PERROS;
        s->jugadores[jp].partida = s->jugadores[jd].partida = partida;
        consola("[srv] Par #%d: J%d (Puma) vs J%d (Perros) — esperando mesa",
                partida, jp + 1, jd + 1);

        /* Esperar pasivamente una mesa libre. Este par está "en tránsito":
         * ya no está en la cola, así que si llega el apagado hay que darle
         * un resultado aquí mismo. */
        while (!servidor_apagando(s) && s->mesas_activas == s->M)
            CHECK(pthread_cond_wait(&s->cond_matchmaker, &s->mtx));
        if (servidor_apagando(s)) {
            fijar_resultado(s, jp, RES_ABORTADA, "servidor apagado mientras esperaba mesa");
            fijar_resultado(s, jd, RES_ABORTADA, "servidor apagado mientras esperaba mesa");
            break;
        }

        int slot = 0;
        while (s->slot_ocupado[slot])
            slot++;
        s->slot_ocupado[slot] = true;
        s->mesas_activas++;
        if (s->mesas_activas > s->max_mesas_activas)
            s->max_mesas_activas = s->mesas_activas;
        s->jugadores[jp].mesa = s->jugadores[jd].mesa = slot + 1;
        consola("[srv] Mesa %d asignada a par #%d — mesas activas %d/%d",
                slot + 1, partida, s->mesas_activas, s->M);

        Mesa *m = mesa_crear(s, s->cfg, partida, slot, jp + 1, jd + 1);
        crear_hilo(&s->jueces[s->num_jueces++], juez_hilo, m);
    }

    /* Quienes quedan en la cola no tendrán partida. */
    while (s->cola_len > 0) {
        int j = s->cola[s->cola_ini++];
        s->cola_len--;
        if (servidor_apagando(s)) {
            fijar_resultado(s, j, RES_SIN_PARTIDA, "servidor apagado antes de emparejar");
        } else {
            fijar_resultado(s, j, RES_SIN_PARTIDA, "sin rival (N impar)");
            consola("[srv] Jugador %d sin rival (N impar)", j + 1);
        }
    }
    CHECK(pthread_cond_broadcast(&s->cond_resultados));
    CHECK(pthread_mutex_unlock(&s->mtx));
    return NULL;
}

void servidor_mesa_terminada(Servidor *s, int slot, int j_puma, int j_perros,
                             ResultadoMesa r, const char *motivo)
{
    int p = j_puma - 1, d = j_perros - 1;
    CHECK(pthread_mutex_lock(&s->mtx));
    switch (r) {
    case MESA_GANA_PUMA:
        fijar_resultado(s, p, RES_GANO, motivo);
        fijar_resultado(s, d, RES_PERDIO, motivo);
        break;
    case MESA_GANAN_PERROS:
        fijar_resultado(s, p, RES_PERDIO, motivo);
        fijar_resultado(s, d, RES_GANO, motivo);
        break;
    case MESA_EMPATE:
        fijar_resultado(s, p, RES_EMPATE, motivo);
        fijar_resultado(s, d, RES_EMPATE, motivo);
        break;
    case MESA_ABORTADA:
    case MESA_EN_CURSO:
        fijar_resultado(s, p, RES_ABORTADA, motivo);
        fijar_resultado(s, d, RES_ABORTADA, motivo);
        break;
    }
    s->slot_ocupado[slot] = false;
    s->mesas_activas--;
    consola("[srv] Mesa %d liberada — mesas activas %d/%d", slot + 1, s->mesas_activas, s->M);
    CHECK(pthread_cond_signal(&s->cond_matchmaker));
    CHECK(pthread_cond_broadcast(&s->cond_resultados));
    CHECK(pthread_mutex_unlock(&s->mtx));
}

/* ---- Señales ----------------------------------------------------------- */

void *servidor_hilo_senales(void *arg)
{
    Servidor *s = arg;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);

    for (;;) {
        int sig;
        CHECK(sigwait(&set, &sig));
        if (sig == SIGUSR1)       /* fin normal: main nos avisa */
            return NULL;
        if (servidor_apagando(s)) {
            consola("[srv] Apagado ya en curso...");
            continue;
        }
        /* SIGINT: estamos en un hilo normal (no en un handler), así que
         * podemos tomar mutex e imprimir sin restricciones. */
        consola("\n[srv] SIGINT recibido: apagando servidor...");
        CHECK(pthread_mutex_lock(&s->mtx));
        atomic_store(&s->apagando, true);
        CHECK(pthread_cond_broadcast(&s->cond_matchmaker));
        CHECK(pthread_cond_broadcast(&s->cond_llegada));
        CHECK(pthread_cond_broadcast(&s->cond_resultados));
        CHECK(pthread_mutex_unlock(&s->mtx));
        /* Cada Juez lo verá en su próximo tick (<= 100 ms). */
    }
}

/* ---- Arranque, espera y limpieza --------------------------------------- */

void servidor_iniciar(Servidor *s)
{
    crear_hilo(&s->matchmaker, matchmaker_hilo, s);
    for (int i = 0; i < s->N; i++)
        crear_hilo(&s->clientes[i], cliente_hilo, &s->jugadores[i]);
}

void servidor_esperar(Servidor *s)
{
    for (int i = 0; i < s->N; i++)
        CHECK(pthread_join(s->clientes[i], NULL));
    CHECK(pthread_join(s->matchmaker, NULL));
    /* El Matchmaker ya terminó: num_jueces no cambia más. */
    for (int i = 0; i < s->num_jueces; i++)
        CHECK(pthread_join(s->jueces[i], NULL));
}

void servidor_destruir(Servidor *s)
{
    CHECK(pthread_cond_destroy(&s->cond_resultados));
    CHECK(pthread_cond_destroy(&s->cond_llegada));
    CHECK(pthread_cond_destroy(&s->cond_matchmaker));
    CHECK(pthread_mutex_destroy(&s->mtx));
    free(s->cola);
    free(s->slot_ocupado);
    free(s->jugadores);
    free(s->clientes);
    free(s->jueces);
}

static const char *resultado_texto(ResultadoJugador r)
{
    switch (r) {
    case RES_PENDIENTE: return "PENDIENTE";
    case RES_GANO: return "GANÓ";
    case RES_PERDIO: return "PERDIÓ";
    case RES_EMPATE: return "EMPATE";
    case RES_ABORTADA: return "ABORTADA";
    case RES_SIN_PARTIDA: return "SIN_PARTIDA";
    case RES_NO_LLEGO: return "NO_LLEGÓ";
    }
    return "?";
}

void servidor_imprimir_resultados(const Servidor *s)
{
    int gana_puma = 0, gana_perros = 0, empates = 0, abortadas = 0;
    printf("\n================ RESULTADOS DEL TORNEO ================\n");
    printf("%-8s %-7s %-7s %-5s %-12s %s\n", "Jugador", "Rol", "Partida", "Mesa",
           "Resultado", "Motivo");
    for (int i = 0; i < s->N; i++) {
        const Jugador *j = &s->jugadores[i];
        const char *rol = j->rol == ROL_PUMA ? "Puma" : j->rol == ROL_PERROS ? "Perros" : "-";
        char partida[16] = "-", mesa[16] = "-";
        if (j->partida > 0)
            snprintf(partida, sizeof partida, "#%d", j->partida);
        if (j->mesa > 0)
            snprintf(mesa, sizeof mesa, "%d", j->mesa);
        printf("J%-7d %-7s %-7s %-5s %-12s %s\n", j->id, rol, partida, mesa,
               resultado_texto(j->resultado), j->motivo);
        if (j->rol == ROL_PUMA) {
            if (j->resultado == RES_GANO) gana_puma++;
            else if (j->resultado == RES_PERDIO) gana_perros++;
            else if (j->resultado == RES_EMPATE) empates++;
            else if (j->resultado == RES_ABORTADA) abortadas++;
        }
    }
    printf("--------------------------------------------------------\n");
    printf("Partidas: %d | gana Puma: %d | ganan Perros: %d | empates: %d | abortadas: %d\n",
           s->partidas, gana_puma, gana_perros, empates, abortadas);
    printf("Máximo de mesas activas simultáneas: %d/%d\n", s->max_mesas_activas, s->M);
}
