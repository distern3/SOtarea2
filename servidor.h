/*
 * servidor.h — Clientes, cola de espera, Matchmaker, mesas y señales.
 *
 * Todo el estado del servidor está protegido por `mtx`. Cada cond tiene un
 * único tipo de esperador (o se usa broadcast), para que ningún signal se
 * "pierda" despertando al hilo equivocado:
 *   cond_matchmaker  -> solo el Matchmaker (2 en cola, mesa libre, apagado)
 *   cond_llegada     -> solo clientes durmiendo su retardo de llegada
 *   cond_resultados  -> clientes esperando su resultado (siempre broadcast)
 */
#ifndef SERVIDOR_H
#define SERVIDOR_H

#include "common.h"
#include "config.h"
#include "mesa.h"

#include <stdatomic.h>

typedef enum { ROL_NINGUNO, ROL_PUMA, ROL_PERROS } Rol;

typedef enum {
    RES_PENDIENTE,
    RES_GANO,
    RES_PERDIO,
    RES_EMPATE,
    RES_ABORTADA,
    RES_SIN_PARTIDA,
    RES_NO_LLEGO,
} ResultadoJugador;

typedef struct Servidor Servidor;

typedef struct {
    Servidor *srv;
    int id;                     /* 1..N */
    Rol rol;
    int partida, mesa;          /* 0 = sin asignar */
    ResultadoJugador resultado;
    char motivo[160];
    unsigned semilla;
} Jugador;

struct Servidor {
    int N, M;
    const Config *cfg;
    atomic_bool apagando;

    pthread_mutex_t mtx;
    pthread_cond_t cond_matchmaker, cond_llegada, cond_resultados;

    int *cola;                  /* FIFO de índices de jugador */
    int cola_ini, cola_len;
    int llegados;
    int mesas_activas, max_mesas_activas;
    bool *slot_ocupado;         /* M slots */
    int partidas;

    Jugador *jugadores;         /* N */
    pthread_t *clientes;        /* N */
    pthread_t *jueces;          /* hasta N/2 */
    int num_jueces;
    pthread_t matchmaker;
};

void servidor_init(Servidor *s, int N, int M, const Config *cfg);
void servidor_iniciar(Servidor *s);    /* crea Matchmaker y N clientes */
void servidor_esperar(Servidor *s);    /* join de clientes, Matchmaker y Jueces */
void servidor_destruir(Servidor *s);
void servidor_imprimir_resultados(const Servidor *s);

void *servidor_hilo_senales(void *arg);   /* sigwait(SIGINT, SIGUSR1) */
bool servidor_apagando(Servidor *s);

/* La llama el Juez al cerrar su partida (sin tener locks de mesa). */
void servidor_mesa_terminada(Servidor *s, int slot, int j_puma, int j_perros,
                             ResultadoMesa r, const char *motivo);

#endif
