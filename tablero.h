/*
 * tablero.h — Topología del tablero (solo lectura) y monitor de casillas.
 *
 * Índices de nodos:
 *   0 = T (vértice del trono), 1 = L, 2 = R (nodos intermedios),
 *   3..27 = cuadrícula 5x5 con idx = 3 + fila*5 + col (fila 0 = la de arriba).
 */
#ifndef TABLERO_H
#define TABLERO_H

#include "common.h"

#include <stdatomic.h>

#define NODO_T 0
#define NODO_L 1
#define NODO_R 2
#define NODO(fila, col) (3 + (fila) * 5 + (col))

typedef struct {
    int sobre;   /* casilla del perro saltado */
    int destino; /* casilla donde cae el Puma */
} Salto;

/* ---- Topología (se construye una vez, antes de crear hilos) ----------- */
void topologia_init(void);
int topo_adj(int nodo, const int **lista);       /* retorna cantidad */
int topo_saltos(int nodo, const Salto **lista);  /* retorna cantidad */
bool topo_son_adyacentes(int a, int b);
bool topo_es_salto(int origen, int sobre, int destino);
int topo_dist(int a, int b);                     /* distancia BFS     */
const char *topo_nombre(int nodo, char *buf, size_t len); /* "T", "L", "(f,c)" */

/*
 * Vecindario de bloqueo de una ficha en `pos`, ORDENADO ASCENDENTE:
 *   Perro: {pos} ∪ adj(pos)                        (máx. 5)
 *   Puma:  {pos} ∪ adj(pos) ∪ destinos de saltos   (máx. 9)
 */
int tablero_vecindario(int pos, bool es_puma, int out[MAX_VECINDARIO]);

/* Dibuja `vista` ('P', 'D', '.') como texto multilínea en `buf`. */
void tablero_render(const char vista[NUM_NODOS], char *buf, size_t len);

/* ---- Monitor de casillas ---------------------------------------------- */
/*
 * Cada casilla c es un monitor: owner[c] (VACIA o id de ficha) protegido por
 * mtx[c], con cond[c] para esperar a que se libere. El mutex se toma solo
 * dentro de tomar/soltar (microsegundos): la casilla queda tomada a nivel
 * LÓGICO (owner), no a nivel de mutex. Por eso una ficha que muere teniendo
 * casillas no deja ningún mutex bloqueado, y el Juez puede liberarlas.
 */
typedef struct {
    int owner[NUM_NODOS];
    pthread_mutex_t mtx[NUM_NODOS];
    pthread_cond_t cond[NUM_NODOS];
} Casillas;

void casillas_init(Casillas *cs);
void casillas_destruir(Casillas *cs);

/*
 * Toma las casillas de `lista` (ya ordenada ascendente) para la ficha `yo`.
 * Espera en tramos de TRAMO_ESPERA_MS; en cada tramo actualiza `latido` y
 * revisa `terminar`. Si `terminar` se activa, suelta lo ya tomado y
 * retorna false.
 */
bool casillas_bloquear(Casillas *cs, const int *lista, int n, int yo,
                       atomic_bool *terminar, _Atomic long long *latido);
void casillas_liberar(Casillas *cs, const int *lista, int n, int yo);

/* Despierta a todos los que esperan alguna casilla (al terminar la partida). */
void casillas_despertar_todas(Casillas *cs);

#endif
