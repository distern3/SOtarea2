/*
 * jugador.c — Ciclo de una ficha: dormir, bloquear vecindario, proponer,
 * dejar que el Juez valide, aplicar, liberar.
 *
 * Perros y Puma usan el MISMO protocolo; solo cambian el vecindario que
 * bloquean y la heurística con que ordenan sus propuestas.
 */
#include "common.h"
#include "jugador.h"
#include "mesa.h"

#define PROB_EXPLORAR 0.2   /* fracción de ciclos con orden aleatorio */

typedef struct {
    Movimiento mv;
    int puntaje;
} Candidato;

static void latir(Mesa *m, int id)
{
    atomic_store(&m->latido[id], ahora_ms());
}

/* Duerme el retardo aleatorio; retorna false si la partida terminó. */
static bool dormir_retardo(Mesa *m, Ficha *f)
{
    int ms = azar_rango(&f->semilla, m->cfg->delay_min, m->cfg->delay_max);
    struct timespec fin = deadline_ms(ms);
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    /* Se duerme en cond_estado para que el broadcast de término nos despierte. */
    while (!atomic_load(&m->terminar) && esperar_hasta(&m->cond_estado, &m->mtx_mesa, &fin))
        ;
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));
    return !atomic_load(&m->terminar);
}

/* ---- Heurística (usa una copia de vista[]: información, no autoridad) --- */

static int buscar_puma(const char snap[NUM_NODOS])
{
    for (int c = 0; c < NUM_NODOS; c++)
        if (snap[c] == 'P')
            return c;
    return VACIA;
}

static bool libre(const char snap[NUM_NODOS], int c, int origen)
{
    return snap[c] == '.' || c == origen;   /* al moverme, mi origen queda libre */
}

/* ¿Podría el Puma (en `puma`) saltar a un perro ubicado en `d`? */
static bool perro_expuesto(const char snap[NUM_NODOS], int d, int origen, int puma)
{
    const Salto *s;
    int ns = topo_saltos(puma, &s);
    for (int i = 0; i < ns; i++)
        if (s[i].sobre == d && libre(snap, s[i].destino, origen))
            return true;
    return false;
}

/* Puntaje de que un perro esté en `d`: cerca del Puma, pero no saltable. */
static int puntaje_perro(const char snap[NUM_NODOS], int d, int origen, int puma)
{
    int p = -10 * topo_dist(d, puma);
    if (perro_expuesto(snap, d, origen, puma))
        p -= 100;
    return p;
}

/*
 * Puntaje del Puma si se mueve a `d`: vecinos libres, capturas que quedarían
 * amenazadas, cercanía al centro (lejos de las esquinas y del trono, donde
 * es fácil acorralarlo) y fuerte castigo a las casillas casi sin salida.
 */
static int puntaje_puma(const char snap[NUM_NODOS], int d, int origen)
{
    int libres = 0, amenazas = 0;
    const int *ady;
    int na = topo_adj(d, &ady);
    for (int i = 0; i < na; i++)
        if (libre(snap, ady[i], origen))
            libres++;
    const Salto *s;
    int ns = topo_saltos(d, &s);
    for (int i = 0; i < ns; i++)
        if (snap[s[i].sobre] == 'D' && libre(snap, s[i].destino, origen))
            amenazas++;
    int p = 100 + 15 * libres + 25 * amenazas - 10 * topo_dist(d, NODO(2, 2));
    if (libres + amenazas <= 1)
        p -= 80;
    return p;
}

/*
 * Genera las propuestas de la ficha, ordenadas de mejor a peor.
 * Incluye destinos que, según la copia, están ocupados: el Juez decide.
 * Un perro con estrategia puede no proponer nada (se queda quieto si
 * ningún movimiento mejora su posición).
 */
static int generar_candidatos(Mesa *m, Ficha *f, const char snap[NUM_NODOS],
                              Candidato out[MAX_CANDIDATOS])
{
    bool es_puma = (f->id == ID_PUMA);
    bool azar = m->cfg->estrategia == 0 || azar01(&f->semilla) < PROB_EXPLORAR;
    int puma = es_puma ? f->pos : buscar_puma(snap);
    if (puma == VACIA)
        azar = true;
    int n = 0;

    const int *ady;
    int na = topo_adj(f->pos, &ady);
    for (int i = 0; i < na; i++) {
        int d = ady[i];
        int p;
        if (azar)
            p = rand_r(&f->semilla) % 1000;
        else if (snap[d] != '.')
            p = -10000;
        else if (es_puma)
            p = puntaje_puma(snap, d, f->pos);
        else
            p = puntaje_perro(snap, d, f->pos, puma);
        out[n++] = (Candidato){.mv = {.origen = f->pos, .destino = d, .sobre = VACIA}, .puntaje = p};
    }
    if (es_puma) {
        const Salto *s;
        int ns = topo_saltos(f->pos, &s);
        for (int i = 0; i < ns; i++) {
            int p;
            if (azar)
                p = rand_r(&f->semilla) % 1000;
            else if (snap[s[i].sobre] == 'D' && snap[s[i].destino] == '.')
                p = 1000;   /* capturar es siempre lo preferido */
            else
                p = -10000;
            out[n++] = (Candidato){
                .mv = {.origen = f->pos, .destino = s[i].destino, .sobre = s[i].sobre},
                .puntaje = p};
        }
    }

    /* Orden descendente por puntaje; desempate aleatorio. */
    for (int i = 1; i < n; i++) {
        Candidato c = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].puntaje < c.puntaje ||
                          (out[j].puntaje == c.puntaje && rand_r(&f->semilla) % 2))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = c;
    }

    /* Perro con estrategia: solo propone lo que mejora quedarse quieto. */
    if (!es_puma && !azar) {
        int actual = puntaje_perro(snap, f->pos, f->pos, puma);
        int k = 0;
        while (k < n && out[k].puntaje > actual)
            k++;
        n = k;
    }
    return n;
}

/* ---- Ciclo de la ficha ------------------------------------------------- */

void *ficha_hilo(void *arg)
{
    Ficha *f = arg;
    Mesa *m = f->mesa;
    bool es_puma = (f->id == ID_PUMA);

    latir(m, f->id);

    /* Esperar el arranque simultáneo. */
    CHECK(pthread_mutex_lock(&m->mtx_mesa));
    while (!m->iniciada && !atomic_load(&m->terminar))
        CHECK(pthread_cond_wait(&m->cond_estado, &m->mtx_mesa));
    CHECK(pthread_mutex_unlock(&m->mtx_mesa));

    while (dormir_retardo(m, f)) {
        latir(m, f->id);

        int bloq[MAX_VECINDARIO];
        int nb = tablero_vecindario(f->pos, es_puma, bloq);
        if (!casillas_bloquear(&m->casillas, bloq, nb, f->id, &m->terminar, &m->latido[f->id]))
            break;   /* la partida terminó mientras esperaba */

        /* Con mi casilla tomada, tablero[pos] es confiable. */
        if (m->tablero[f->pos] != f->id) {   /* me capturaron */
            casillas_liberar(&m->casillas, bloq, nb, f->id);
            return NULL;                     /* el Puma ya me marcó CAPTURADA */
        }

        /* Simulación de 4.1: el hilo muere RETENIENDO sus casillas. */
        if (m->cfg->fail_prob > 0.0 && azar01(&f->semilla) < m->cfg->fail_prob) {
            juez_reportar_fallo(m, f->id, nb);
            pthread_exit(NULL);
        }

        char snap[NUM_NODOS];
        mesa_snapshot(m, snap);
        Candidato cand[MAX_CANDIDATOS];
        int nc = generar_candidatos(m, f, snap, cand);

        bool movio = false;
        for (int i = 0; i < nc && !movio; i++) {
            Validacion v = juez_validar(m, f->id, &cand[i].mv, bloq, nb);
            if (v == VALIDO) {
                int capturado = juez_aplicar(m, f->id, &cand[i].mv);
                f->pos = cand[i].mv.destino;
                juez_registrar_movimiento(m, f->id, &cand[i].mv, capturado);
                movio = true;
            } else {
                juez_registrar_rechazo(m, f->id, &cand[i].mv, v);
            }
        }
        /* El Puma propone TODOS sus movimientos posibles: si el Juez rechazó
         * todos, con el vecindario bloqueado, está acorralado. */
        if (!movio && es_puma)
            juez_reportar_acorralado(m);

        casillas_liberar(&m->casillas, bloq, nb, f->id);
    }

    mesa_marcar_terminada(m, f->id);
    return NULL;
}
