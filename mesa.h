/*
 * mesa.h — Mesa de juego: estado compartido, hilo Juez y reglas.
 *
 * Quién protege qué:
 *   tablero[c]            -> la casilla c (monitor en Casillas). Solo se lee o
 *                            escribe teniendo la casilla tomada (o antes de
 *                            crear / después de hacer join de las fichas).
 *   vista, estado, contadores, acorralado, iniciada, log
 *                         -> mtx_mesa.
 *   terminar, latido[]    -> atómicos.
 *
 * Jerarquía de locks: casillas (ascendente) -> mtx_mesa -> mtx_consola.
 * El Juez NUNCA toma casillas.
 */
#ifndef MESA_H
#define MESA_H

#include "common.h"
#include "config.h"
#include "tablero.h"

#include <stdatomic.h>

struct Servidor;

typedef enum { FICHA_VIVA, FICHA_CAPTURADA, FICHA_TERMINADA } EstadoFicha;

typedef enum {
    MESA_EN_CURSO,
    MESA_GANA_PUMA,
    MESA_GANAN_PERROS,
    MESA_EMPATE,
    MESA_ABORTADA,
} ResultadoMesa;

typedef enum {
    VALIDO,
    RECHAZO_NO_ES_SUYA,          /* la ficha no está en el origen        */
    RECHAZO_FUERA_DE_VECINDARIO, /* tocaría casillas que no bloqueó      */
    RECHAZO_NO_ADYACENTE,
    RECHAZO_OCUPADA,
    RECHAZO_PERRO_NO_SALTA,      /* los perros no capturan               */
    RECHAZO_SALTO_INVALIDO,      /* no es una línea recta de 3 nodos     */
    RECHAZO_SIN_PERRO,           /* no hay perro que saltar              */
} Validacion;

typedef struct {
    int origen, destino;
    int sobre;                   /* VACIA = movimiento simple; si no, captura */
} Movimiento;

typedef struct Mesa Mesa;

typedef struct {
    Mesa *mesa;
    int id;                      /* 0 = Puma, 1..12 = Perros */
    int pos;                     /* solo la usa el propio hilo de la ficha */
    unsigned semilla;
} Ficha;

struct Mesa {
    int partida;                 /* número de partida (para el log)       */
    int slot;                    /* 0..M-1                                */
    int jugador_puma, jugador_perros;
    const Config *cfg;
    struct Servidor *srv;

    int tablero[NUM_NODOS];      /* id de ficha o VACIA (ver arriba)      */
    Casillas casillas;

    pthread_mutex_t mtx_mesa;
    pthread_cond_t cond_juez;    /* solo espera el Juez                   */
    pthread_cond_t cond_estado;  /* arranque / término; fichas duermen aquí */
    char vista[NUM_NODOS];
    EstadoFicha estado[NUM_FICHAS];
    int capturas;
    long movimientos, rechazos;
    bool acorralado, iniciada;
    FILE *log;
    long long t_inicio;

    atomic_bool terminar;
    _Atomic long long latido[NUM_FICHAS];

    Ficha fichas[NUM_FICHAS];
    pthread_t hilos[NUM_FICHAS];
};

/* Crea la mesa (sin iniciar). El hilo Juez la libera al terminar. */
Mesa *mesa_crear(struct Servidor *srv, const Config *cfg, int partida, int slot,
                 int jugador_puma, int jugador_perros);
void *juez_hilo(void *arg);

/* ---- API del Juez usada por las fichas (código de reglas en mesa.c) --- */

/* Requiere tener `bloqueadas` tomadas. No modifica nada. */
Validacion juez_validar(Mesa *m, int ficha, const Movimiento *mv,
                        const int *bloqueadas, int nb);
/* Aplica un movimiento ya validado a tablero[]; retorna el perro capturado o VACIA. */
int juez_aplicar(Mesa *m, int ficha, const Movimiento *mv);
/* Registra en vista/log/contadores (toma mtx_mesa). */
void juez_registrar_movimiento(Mesa *m, int ficha, const Movimiento *mv, int capturado);
void juez_registrar_rechazo(Mesa *m, int ficha, const Movimiento *mv, Validacion v);
void juez_reportar_acorralado(Mesa *m);
void juez_reportar_fallo(Mesa *m, int ficha, int casillas_retenidas);

void mesa_snapshot(Mesa *m, char out[NUM_NODOS]);
void mesa_marcar_terminada(Mesa *m, int ficha);
const char *nombre_ficha(int ficha, char *buf, size_t len);

#endif
