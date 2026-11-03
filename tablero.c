/*
 * tablero.c — Topología, saltos, distancias, render y monitor de casillas.
 *
 * Tablero (según la figura del enunciado, sin diagonales en la cuadrícula):
 *
 *            T
 *          / | \
 *        L---+---R        (el '+' es un cruce de líneas, NO es un nodo)
 *      / |   |   | \
 *     .---.---.---.---.   fila 0
 *     |   |   |   |   |
 *     ...                 filas 1..4
 */
#include "common.h"
#include "tablero.h"

#include <assert.h>

/* Tablas de solo lectura: se escriben en topologia_init() antes de crear
 * cualquier hilo (pthread_create establece happens-before), luego solo se leen. */
static int coord_x[NUM_NODOS], coord_y[NUM_NODOS];
static int adj[NUM_NODOS][MAX_ADJ];
static int num_adj[NUM_NODOS];
static Salto saltos[NUM_NODOS][MAX_SALTOS];
static int num_saltos[NUM_NODOS];
static int dist[NUM_NODOS][NUM_NODOS];

static void agregar_arista(int a, int b)
{
    assert(num_adj[a] < MAX_ADJ && num_adj[b] < MAX_ADJ);
    adj[a][num_adj[a]++] = b;
    adj[b][num_adj[b]++] = a;
}

static int mcd(int a, int b)
{
    a = abs(a);
    b = abs(b);
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Dirección normalizada de a hacia b (p. ej. T→(0,2) mide 2 pero es (0,1)). */
static void direccion(int a, int b, int *dx, int *dy)
{
    int x = coord_x[b] - coord_x[a];
    int y = coord_y[b] - coord_y[a];
    int g = mcd(x, y);
    *dx = x / g;
    *dy = y / g;
}

static void calcular_saltos(void)
{
    /* Salto a→b→c si a–b y b–c son aristas con la misma dirección. */
    for (int a = 0; a < NUM_NODOS; a++) {
        for (int i = 0; i < num_adj[a]; i++) {
            int b = adj[a][i];
            int dx1, dy1;
            direccion(a, b, &dx1, &dy1);
            for (int j = 0; j < num_adj[b]; j++) {
                int c = adj[b][j];
                if (c == a)
                    continue;
                int dx2, dy2;
                direccion(b, c, &dx2, &dy2);
                if (dx1 == dx2 && dy1 == dy2) {
                    assert(num_saltos[a] < MAX_SALTOS);
                    saltos[a][num_saltos[a]++] = (Salto){.sobre = b, .destino = c};
                }
            }
        }
    }
}

static void calcular_distancias(void)
{
    for (int s = 0; s < NUM_NODOS; s++) {
        int cola[NUM_NODOS], ini = 0, fin = 0;
        for (int i = 0; i < NUM_NODOS; i++)
            dist[s][i] = -1;
        dist[s][s] = 0;
        cola[fin++] = s;
        while (ini < fin) {
            int u = cola[ini++];
            for (int i = 0; i < num_adj[u]; i++) {
                int v = adj[u][i];
                if (dist[s][v] < 0) {
                    dist[s][v] = dist[s][u] + 1;
                    cola[fin++] = v;
                }
            }
        }
    }
}

void topologia_init(void)
{
    /* Coordenadas: T=(2,0), L=(1,1), R=(3,1); cuadrícula (col, fila+2). */
    coord_x[NODO_T] = 2; coord_y[NODO_T] = 0;
    coord_x[NODO_L] = 1; coord_y[NODO_L] = 1;
    coord_x[NODO_R] = 3; coord_y[NODO_R] = 1;
    for (int f = 0; f < 5; f++)
        for (int c = 0; c < 5; c++) {
            coord_x[NODO(f, c)] = c;
            coord_y[NODO(f, c)] = f + 2;
        }

    /* Cuadrícula: solo aristas ortogonales. */
    for (int f = 0; f < 5; f++)
        for (int c = 0; c < 5; c++) {
            if (c + 1 < 5)
                agregar_arista(NODO(f, c), NODO(f, c + 1));
            if (f + 1 < 5)
                agregar_arista(NODO(f, c), NODO(f + 1, c));
        }

    /* Trono. */
    agregar_arista(NODO_T, NODO_L);
    agregar_arista(NODO_T, NODO_R);
    agregar_arista(NODO_T, NODO(0, 2));  /* vertical; cruza L–R sin nodo */
    agregar_arista(NODO_L, NODO_R);
    agregar_arista(NODO_L, NODO(0, 0));  /* diagonal */
    agregar_arista(NODO_L, NODO(0, 1));  /* vertical */
    agregar_arista(NODO_R, NODO(0, 4));  /* diagonal */
    agregar_arista(NODO_R, NODO(0, 3));  /* vertical */

    calcular_saltos();
    calcular_distancias();

    /* Verificaciones de adyacencias y saltos conocidos. */
    assert(topo_son_adyacentes(NODO_T, NODO_L));
    assert(topo_son_adyacentes(NODO_T, NODO(0, 2)));
    assert(!topo_son_adyacentes(NODO_T, NODO(0, 0)));
    assert(!topo_son_adyacentes(NODO(1, 1), NODO(2, 2)));   /* sin diagonales */
    assert(topo_es_salto(NODO_T, NODO_L, NODO(0, 0)));
    assert(topo_es_salto(NODO_T, NODO(0, 2), NODO(1, 2)));
    assert(topo_es_salto(NODO_L, NODO(0, 1), NODO(1, 1)));
    assert(topo_es_salto(NODO(0, 0), NODO_L, NODO_T));
    assert(topo_es_salto(NODO(1, 2), NODO(0, 2), NODO_T));
    assert(topo_es_salto(NODO(2, 0), NODO(2, 1), NODO(2, 2)));
    assert(!topo_es_salto(NODO_L, NODO_R, NODO(0, 4)));
    assert(!topo_es_salto(NODO(0, 1), NODO_L, NODO_T));     /* no colineal */
    for (int i = 0; i < NUM_NODOS; i++)
        for (int j = 0; j < NUM_NODOS; j++)
            assert(dist[i][j] >= 0);                        /* grafo conexo */
    assert(topo_dist(NODO_T, NODO(4, 2)) == 5);
}

int topo_adj(int nodo, const int **lista)
{
    *lista = adj[nodo];
    return num_adj[nodo];
}

int topo_saltos(int nodo, const Salto **lista)
{
    *lista = saltos[nodo];
    return num_saltos[nodo];
}

bool topo_son_adyacentes(int a, int b)
{
    for (int i = 0; i < num_adj[a]; i++)
        if (adj[a][i] == b)
            return true;
    return false;
}

bool topo_es_salto(int origen, int sobre, int destino)
{
    for (int i = 0; i < num_saltos[origen]; i++)
        if (saltos[origen][i].sobre == sobre && saltos[origen][i].destino == destino)
            return true;
    return false;
}

int topo_dist(int a, int b)
{
    return dist[a][b];
}

const char *topo_nombre(int nodo, char *buf, size_t len)
{
    if (nodo == NODO_T)
        snprintf(buf, len, "T");
    else if (nodo == NODO_L)
        snprintf(buf, len, "L");
    else if (nodo == NODO_R)
        snprintf(buf, len, "R");
    else
        snprintf(buf, len, "(%d,%d)", (nodo - 3) / 5, (nodo - 3) % 5);
    return buf;
}

int tablero_vecindario(int pos, bool es_puma, int out[MAX_VECINDARIO])
{
    int n = 0;
    out[n++] = pos;
    for (int i = 0; i < num_adj[pos]; i++)
        out[n++] = adj[pos][i];
    if (es_puma)
        for (int i = 0; i < num_saltos[pos]; i++)
            out[n++] = saltos[pos][i].destino;

    /* Orden ascendente (inserción) y sin repetidos. */
    for (int i = 1; i < n; i++) {
        int v = out[i], j = i - 1;
        while (j >= 0 && out[j] > v) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = v;
    }
    int m = 0;
    for (int i = 0; i < n; i++)
        if (m == 0 || out[m - 1] != out[i])
            out[m++] = out[i];
    return m;
}

void tablero_render(const char vista[NUM_NODOS], char *buf, size_t len)
{
    int k = snprintf(buf, len,
                     "        %c\n"
                     "      / | \\\n"
                     "    %c---+---%c\n"
                     "  / |   |   | \\\n",
                     vista[NODO_T], vista[NODO_L], vista[NODO_R]);
    for (int f = 0; f < 5 && k >= 0 && (size_t)k < len; f++) {
        const char *g = &vista[NODO(f, 0)];
        k += snprintf(buf + k, len - (size_t)k, "%c---%c---%c---%c---%c   fila %d\n",
                      g[0], g[1], g[2], g[3], g[4], f);
        if (f < 4 && (size_t)k < len)
            k += snprintf(buf + k, len - (size_t)k, "|   |   |   |   |\n");
    }
}

/* ---- Monitor de casillas ---------------------------------------------- */

void casillas_init(Casillas *cs)
{
    for (int c = 0; c < NUM_NODOS; c++) {
        cs->owner[c] = VACIA;
        CHECK(pthread_mutex_init(&cs->mtx[c], NULL));
        CHECK(pthread_cond_init(&cs->cond[c], NULL));
    }
}

void casillas_destruir(Casillas *cs)
{
    for (int c = 0; c < NUM_NODOS; c++) {
        CHECK(pthread_cond_destroy(&cs->cond[c]));
        CHECK(pthread_mutex_destroy(&cs->mtx[c]));
    }
}

/* Toma UNA casilla; false si la partida terminó mientras esperaba. */
static bool tomar(Casillas *cs, int c, int yo, atomic_bool *terminar,
                  _Atomic long long *latido)
{
    CHECK(pthread_mutex_lock(&cs->mtx[c]));
    while (cs->owner[c] != VACIA) {
        /* terminar se revisa con mtx[c] tomado: el Juez hace broadcast de
         * cond[c] DESPUÉS de activar terminar y tomando mtx[c], así que o lo
         * vemos aquí o recibimos el broadcast. */
        if (atomic_load(terminar)) {
            CHECK(pthread_mutex_unlock(&cs->mtx[c]));
            return false;
        }
        struct timespec fin = deadline_ms(TRAMO_ESPERA_MS);
        esperar_hasta(&cs->cond[c], &cs->mtx[c], &fin);
        /* Esperar una casilla NO es estar caído: seguimos latiendo. */
        atomic_store(latido, ahora_ms());
    }
    cs->owner[c] = yo;
    CHECK(pthread_mutex_unlock(&cs->mtx[c]));
    return true;
}

static void soltar(Casillas *cs, int c, int yo)
{
    CHECK(pthread_mutex_lock(&cs->mtx[c]));
    assert(cs->owner[c] == yo);
    (void)yo;
    cs->owner[c] = VACIA;
    CHECK(pthread_cond_signal(&cs->cond[c]));
    CHECK(pthread_mutex_unlock(&cs->mtx[c]));
}

bool casillas_bloquear(Casillas *cs, const int *lista, int n, int yo,
                       atomic_bool *terminar, _Atomic long long *latido)
{
    /* Orden ascendente global => no hay espera circular => no hay deadlock. */
    for (int i = 0; i < n; i++) {
        assert(i == 0 || lista[i - 1] < lista[i]);
        if (!tomar(cs, lista[i], yo, terminar, latido)) {
            for (int j = i - 1; j >= 0; j--)
                soltar(cs, lista[j], yo);
            return false;
        }
    }
    return true;
}

void casillas_liberar(Casillas *cs, const int *lista, int n, int yo)
{
    for (int i = n - 1; i >= 0; i--)
        soltar(cs, lista[i], yo);
}

void casillas_despertar_todas(Casillas *cs)
{
    for (int c = 0; c < NUM_NODOS; c++) {
        CHECK(pthread_mutex_lock(&cs->mtx[c]));
        CHECK(pthread_cond_broadcast(&cs->cond[c]));
        CHECK(pthread_mutex_unlock(&cs->mtx[c]));
    }
}
