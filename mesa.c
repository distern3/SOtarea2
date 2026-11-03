/*
 * mesa.c — Hilo Juez: inicializa la mesa, arbitra, detecta victoria /
 * abandono / apagado / empate, cierra la partida y libera la mesa.
 * También contiene las reglas (validar y aplicar movimientos).
 */
#include "common.h"
#include "mesa.h"
#include "jugador.h"
#include "log.h"
#include "servidor.h"

#include <assert.h>
#include <stdarg.h>

#define TAM_RENDER 512

const char *nombre_ficha(int ficha, char *buf, size_t len)
{
    if (ficha == ID_PUMA)
        snprintf(buf, len, "Puma");
    else
        snprintf(buf, len, "D%d", ficha);
    return buf;
}

/* ---- Log de la partida (requiere mtx_mesa, o ser el único hilo) -------- */

static void log_linea(Mesa *m, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void log_linea(Mesa *m, const char *fmt, ...)
{
    if (m->log == NULL)
        return;
    long long t = ahora_ms() - m->t_inicio;
    fprintf(m->log, "[%4lld.%03llds] ", t / 1000, t % 1000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(m->log, fmt, ap);
    va_end(ap);
    fputc('\n', m->log);
}

static void log_tablero(Mesa *m)
{
    if (m->log == NULL)
        return;
    char buf[TAM_RENDER];
    tablero_render(m->vista, buf, sizeof buf);
    fputs(buf, m->log);
    fputc('\n', m->log);
}

static void consola_tablero(Mesa *m)
{
    char buf[TAM_RENDER], prefijo[24];
    tablero_render(m->vista, buf, sizeof buf);
    snprintf(prefijo, sizeof prefijo, "[Mesa %d] ", m->slot + 1);
    consola_bloque(prefijo, buf);
}

/* ---- Creación e inicialización ---------------------------------------- */

Mesa *mesa_crear(Servidor *srv, const Config *cfg, int partida, int slot,
                 int jugador_puma, int jugador_perros)
{
    Mesa *m = calloc(1, sizeof *m);
    if (m == NULL) {
        perror("calloc");
        abort();
    }
    m->srv = srv;
    m->cfg = cfg;
    m->partida = partida;
    m->slot = slot;
    m->jugador_puma = jugador_puma;
    m->jugador_perros = jugador_perros;
    return m;
}

/* Paso 1 del Juez: tablero, primitivas y log. */
static void mesa_inicializar(Mesa *m)
{
    int siguiente_perro = 1;
    for (int c = 0; c < NUM_NODOS; c++) {
        char celda = m->cfg->disposicion[c];
        m->vista[c] = celda;
        m->tablero[c] = VACIA;
        if (celda == 'P') {
            m->tablero[c] = ID_PUMA;
            m->fichas[ID_PUMA].pos = c;
        } else if (celda == 'D') {
            m->tablero[c] = siguiente_perro;
            m->fichas[siguiente_perro].pos = c;
            siguiente_perro++;
        }
    }
    assert(siguiente_perro == NUM_FICHAS);

    casillas_init(&m->casillas);
    CHECK(pthread_mutex_init(&m->mtx_mesa, NULL));
    CHECK(pthread_cond_init(&m->cond_juez, NULL));
    CHECK(pthread_cond_init(&m->cond_estado, NULL));
    atomic_init(&m->terminar, false);

    long long ahora = ahora_ms();
    m->t_inicio = ahora;
    for (int i = 0; i < NUM_FICHAS; i++) {
        m->estado[i] = FICHA_VIVA;
        atomic_init(&m->latido[i], ahora);
        m->fichas[i].mesa = m;
        m->fichas[i].id = i;
        m->fichas[i].semilla = m->cfg->seed ^ ((unsigned)m->partida * 7919u) ^
                               ((unsigned)i * 104729u);
    }

    char ruta[64];
    snprintf(ruta, sizeof ruta, "logs/partida_%d.log", m->partida);
    m->log = fopen(ruta, "w");
    if (m->log == NULL)
        consola("[M%d]  aviso: no se pudo crear %s (%s); la partida sigue sin log",
                m->slot + 1, ruta, strerror(errno));

    log_linea(m, "Partida #%d en mesa %d: J%d (Puma) vs J%d (Perros)",
              m->partida, m->slot + 1, m->jugador_puma, m->jugador_perros);
    log_linea(m, "Retardo %d-%d ms, estrategia %s, máx. %d ms, FAIL_PROB %.3f",
              m->cfg->delay_min, m->cfg->delay_max,
              m->cfg->estrategia ? "heurística" : "azar",
              m->cfg->max_duracion_ms, m->cfg->fail_prob);
    log_linea(m, "Tablero inicial:");
    log_tablero(m);
}

/* ---- Reglas (se ejecutan en el hilo de la ficha, con casillas tomadas) - */

static bool contiene(const int *lista, int n, int x)
{
    for (int i = 0; i < n; i++)
        if (lista[i] == x)
            return true;
    return false;
}

Validacion juez_validar(Mesa *m, int ficha, const Movimiento *mv,
                        const int *bloqueadas, int nb)
{
    /* El protocolo exige que la ficha tenga tomadas todas las casillas que
     * se leen o escriben; si no, su lectura de tablero[] sería una carrera. */
    if (!contiene(bloqueadas, nb, mv->origen) || !contiene(bloqueadas, nb, mv->destino) ||
        (mv->sobre != VACIA && !contiene(bloqueadas, nb, mv->sobre)))
        return RECHAZO_FUERA_DE_VECINDARIO;
    /* Si la ficha ya no está en su origen (p. ej. fue capturada), no mueve. */
    if (m->tablero[mv->origen] != ficha)
        return RECHAZO_NO_ES_SUYA;

    if (mv->sobre == VACIA) {
        if (!topo_son_adyacentes(mv->origen, mv->destino))
            return RECHAZO_NO_ADYACENTE;
    } else {
        if (ficha != ID_PUMA)
            return RECHAZO_PERRO_NO_SALTA;
        if (!topo_es_salto(mv->origen, mv->sobre, mv->destino))
            return RECHAZO_SALTO_INVALIDO;
        int saltado = m->tablero[mv->sobre];
        if (saltado == VACIA || saltado == ID_PUMA)
            return RECHAZO_SIN_PERRO;
    }
    if (m->tablero[mv->destino] != VACIA)
        return RECHAZO_OCUPADA;
    return VALIDO;
}

int juez_aplicar(Mesa *m, int ficha, const Movimiento *mv)
{
    int capturado = VACIA;
    if (mv->sobre != VACIA) {
        capturado = m->tablero[mv->sobre];
        m->tablero[mv->sobre] = VACIA;
    }
    m->tablero[mv->origen] = VACIA;
    m->tablero[mv->destino] = ficha;
    return capturado;
}

static const char *validacion_texto(Validacion v)
{
    switch (v) {
    case VALIDO: return "válido";
    case RECHAZO_NO_ES_SUYA: return "la ficha no está en el origen";
    case RECHAZO_FUERA_DE_VECINDARIO: return "casilla no bloqueada";
    case RECHAZO_NO_ADYACENTE: return "destino no adyacente";
    case RECHAZO_OCUPADA: return "casilla ocupada";
    case RECHAZO_PERRO_NO_SALTA: return "los perros no capturan";
    case RECHAZO_SALTO_INVALIDO: return "salto no colineal";
    case RECHAZO_SIN_PERRO: return "no hay perro que saltar";
    }
    return "?";
}

/* Invariantes del juego, verificados tras cada movimiento (con mtx_mesa). */
static void verificar_invariantes(const Mesa *m)
{
    int pumas = 0, perros = 0;
    for (int c = 0; c < NUM_NODOS; c++) {
        if (m->vista[c] == 'P')
            pumas++;
        else if (m->vista[c] == 'D')
            perros++;
    }
    assert(pumas == 1);
    assert(perros == NUM_PERROS - m->capturas);
    (void)pumas;
    (void)perros;
}

void juez_registrar_movimiento(Mesa *m, int ficha, const Movimiento *mv, int capturado)
{
    char nf[8], no[8], nd[8], ns[8], nc[8];
    nombre_ficha(ficha, nf, sizeof nf);
    topo_nombre(mv->origen, no, sizeof no);
    topo_nombre(mv->destino, nd, sizeof nd);

    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    /* La ficha todavía retiene sus casillas: movimientos en conflicto se
     * ordenan igual en tablero[] y en vista[]. */
    m->vista[mv->origen] = '.';
    m->vista[mv->destino] = (ficha == ID_PUMA) ? 'P' : 'D';
    m->movimientos++;
    if (capturado != VACIA) {
        m->vista[mv->sobre] = '.';
        m->capturas++;
        m->estado[capturado] = FICHA_CAPTURADA;   /* ya no debe latir */
    }
    verificar_invariantes(m);

    if (capturado != VACIA) {
        topo_nombre(mv->sobre, ns, sizeof ns);
        log_linea(m, "%s %s -> %s CAPTURA a %s en %s (capturas: %d)", nf, no, nd,
                  nombre_ficha(capturado, nc, sizeof nc), ns, m->capturas);
        consola("[M%d]  Puma captura perro en %s — capturas %d", m->slot + 1, ns, m->capturas);
        CHECK(pthread_cond_signal(&m->cond_juez));
    } else {
        log_linea(m, "%s %s -> %s", nf, no, nd);
    }
    if (m->cfg->log_tablero)
        log_tablero(m);
    if (m->cfg->tablero_consola)
        consola_tablero(m);
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
}

void juez_registrar_rechazo(Mesa *m, int ficha, const Movimiento *mv, Validacion v)
{
    char nf[8], no[8], nd[8];
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    m->rechazos++;
    log_linea(m, "Juez rechaza %s %s -> %s%s: %s", nombre_ficha(ficha, nf, sizeof nf),
              topo_nombre(mv->origen, no, sizeof no), topo_nombre(mv->destino, nd, sizeof nd),
              mv->sobre != VACIA ? " (salto)" : "", validacion_texto(v));
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
}

void juez_reportar_acorralado(Mesa *m)
{
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    if (!m->acorralado) {
        m->acorralado = true;
        log_linea(m, "Puma sin movimientos válidos: ACORRALADO");
        CHECK(pthread_cond_signal(&m->cond_juez));
    }
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
}

void juez_reportar_fallo(Mesa *m, int ficha, int casillas_retenidas)
{
    char nf[8];
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    log_linea(m, "FALLO SIMULADO: %s termina abruptamente reteniendo %d casillas",
              nombre_ficha(ficha, nf, sizeof nf), casillas_retenidas);
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
}

void mesa_snapshot(Mesa *m, char out[NUM_NODOS])
{
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    memcpy(out, m->vista, NUM_NODOS);
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
}

void mesa_marcar_terminada(Mesa *m, int ficha)
{
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    if (m->estado[ficha] == FICHA_VIVA)
        m->estado[ficha] = FICHA_TERMINADA;
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
}

/* ---- Arbitraje -------------------------------------------------------- */

/* Revisa las condiciones de término, en orden. Requiere mtx_mesa. */
static ResultadoMesa evaluar(Mesa *m, char *motivo, size_t len)
{
    if (m->capturas >= CAPTURAS_VICTORIA) {
        snprintf(motivo, len, "%d capturas", m->capturas);
        return MESA_GANA_PUMA;
    }
    if (m->acorralado) {
        snprintf(motivo, len, "Puma acorralado");
        return MESA_GANAN_PERROS;
    }
    long long ahora = ahora_ms();
    for (int i = 0; i < NUM_FICHAS; i++) {
        if (m->estado[i] != FICHA_VIVA)   /* capturadas no laten: se ignoran */
            continue;
        long long sin_latir = ahora - atomic_load(&m->latido[i]);
        if (sin_latir > m->cfg->heartbeat_timeout_ms) {
            char nf[8];
            nombre_ficha(i, nf, sizeof nf);
            if (i == ID_PUMA) {
                snprintf(motivo, len, "abandono del Puma (%s sin latido hace %lld ms)",
                         nf, sin_latir);
                return MESA_GANAN_PERROS;
            }
            snprintf(motivo, len, "abandono de Perros (%s sin latido hace %lld ms)",
                     nf, sin_latir);
            return MESA_GANA_PUMA;
        }
    }
    if (servidor_apagando(m->srv)) {
        snprintf(motivo, len, "servidor apagado (SIGINT)");
        return MESA_ABORTADA;
    }
    if (ahora - m->t_inicio >= m->cfg->max_duracion_ms) {
        snprintf(motivo, len, "tiempo máximo (%d ms)", m->cfg->max_duracion_ms);
        return MESA_EMPATE;
    }
    return MESA_EN_CURSO;
}

static const char *resultado_mesa_texto(ResultadoMesa r)
{
    switch (r) {
    case MESA_GANA_PUMA: return "gana Puma";
    case MESA_GANAN_PERROS: return "ganan Perros";
    case MESA_EMPATE: return "empate";
    case MESA_ABORTADA: return "abortada";
    case MESA_EN_CURSO: break;
    }
    return "?";
}

void *juez_hilo(void *arg)
{
    Mesa *m = arg;

    /* 1. Inicialización. */
    mesa_inicializar(m);

    /* 2. Arranque: crear las 13 fichas y soltarlas a la vez. */
    for (int i = 0; i < NUM_FICHAS; i++)
        crear_hilo(&m->hilos[i], ficha_hilo, &m->fichas[i]);
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    m->iniciada = true;
    log_linea(m, "Arranque: 13 fichas en juego");
    CHECK(pthread_cond_broadcast(&m->cond_estado));

    /* 3. Bucle de arbitraje (tick de 100 ms o evento en cond_juez). */
    char motivo[160];
    ResultadoMesa r;
    while ((r = evaluar(m, motivo, sizeof motivo)) == MESA_EN_CURSO) {
        struct timespec fin = deadline_ms(TICK_JUEZ_MS);
        esperar_hasta(&m->cond_juez, &m->mtx_mesa, &fin);
    }

    /* 4.1 Ordenar el término a las fichas. */
    atomic_store(&m->terminar, true);
    log_linea(m, "FIN: %s (%s)", resultado_mesa_texto(r), motivo);
    CHECK(pthread_cond_broadcast(&m->cond_estado));
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
    casillas_despertar_todas(&m->casillas);  /* las que esperan una casilla */

    /* 4.2 Join de las 13 fichas (una ficha caída ya terminó). */
    for (int i = 0; i < NUM_FICHAS; i++)
        CHECK(pthread_join(m->hilos[i], NULL));

    /* 4.3 Casillas huérfanas: el join dio happens-before con todas las
     * fichas, así que leer owner[] aquí no es una carrera. */
    for (int c = 0; c < NUM_NODOS; c++) {
        int dueno = m->casillas.owner[c];
        if (dueno != VACIA) {
            char nf[8], nc[8];
            log_linea(m, "Liberando casilla %s retenida por %s (ficha caída)",
                      topo_nombre(c, nc, sizeof nc), nombre_ficha(dueno, nf, sizeof nf));
            m->casillas.owner[c] = VACIA;
        }
    }
    /* tablero[] y vista[] deben coincidir. */
    for (int c = 0; c < NUM_NODOS; c++) {
        char esperado = m->tablero[c] == VACIA ? '.' : (m->tablero[c] == ID_PUMA ? 'P' : 'D');
        assert(m->vista[c] == esperado);
        (void)esperado;
    }

    /* 4.4 Resumen, destruir primitivas y cerrar el log. */
    double segundos = (ahora_ms() - m->t_inicio) / 1000.0;
    double mov_s = segundos > 0 ? m->movimientos / segundos : 0.0;
    log_linea(m, "Resumen: %s (%s) — %ld movimientos, %ld rechazos, %d capturas, "
                 "%.2f s, %.1f mov/s",
              resultado_mesa_texto(r), motivo, m->movimientos, m->rechazos, m->capturas,
              segundos, mov_s);
    log_linea(m, "Tablero final:");
    log_tablero(m);
    consola("[M%d]  FIN partida #%d: %s (%s) — %ld movs, %ld rechazos en %.1f s (%.1f mov/s)",
            m->slot + 1, m->partida, resultado_mesa_texto(r), motivo, m->movimientos,
            m->rechazos, segundos, mov_s);

    casillas_destruir(&m->casillas);
    CHECK(pthread_cond_destroy(&m->cond_estado));
    CHECK(pthread_cond_destroy(&m->cond_juez));
    CHECK(pthread_mutex_destroy(&m->mtx_mesa));
    if (m->log != NULL)
        fclose(m->log);

    /* 4.5 Informar al servidor y liberar la mesa. */
    servidor_mesa_terminada(m->srv, m->slot, m->jugador_puma, m->jugador_perros, r, motivo);
    free(m);
    return NULL;
}
