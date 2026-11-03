/*
 * common.h — Constantes, macros y utilidades compartidas por todos los módulos.
 *
 * IMPORTANTE: debe ser el PRIMER include de cada .c, porque define
 * _POSIX_C_SOURCE antes de cualquier header del sistema. Con -std=c17 (sin
 * extensiones GNU) es lo que expone pthread, clock_gettime, rand_r, sigwait...
 */
#ifndef COMMON_H
#define COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- Tablero ---------------------------------------------------------- */
#define NUM_NODOS 28                 /* 1 trono + 2 intermedios + 5x5        */
#define NUM_PERROS 12
#define NUM_FICHAS (NUM_PERROS + 1)  /* ficha 0 = Puma, 1..12 = Perros        */
#define ID_PUMA 0
#define VACIA (-1)                   /* casilla sin ficha / sin dueño         */
#define MAX_ADJ 4                    /* sin diagonales: máximo 4 vecinos      */
#define MAX_SALTOS 4
#define MAX_VECINDARIO (1 + MAX_ADJ + MAX_SALTOS)  /* Puma: hasta 9 casillas  */
#define MAX_CANDIDATOS (MAX_ADJ + MAX_SALTOS)
#define CAPTURAS_VICTORIA 6

/* ---- Tiempos e hilos -------------------------------------------------- */
#define STACK_HILO (256 * 1024)      /* stack por hilo (estrés con ~900 hilos) */
#define TICK_JUEZ_MS 100             /* cada cuánto revisa el Juez            */
#define TRAMO_ESPERA_MS 50           /* tramo de espera por una casilla       */

/*
 * CHECK: las funciones pthread_* retornan 0 o un código de error (no usan
 * errno). Un error aquí es un bug de programación: se informa y se aborta.
 */
#define CHECK(llamada)                                                       \
    do {                                                                     \
        int check_err_ = (llamada);                                          \
        if (check_err_ != 0) {                                               \
            fprintf(stderr, "%s:%d: %s falló: %s\n", __FILE__, __LINE__,     \
                    #llamada, strerror(check_err_));                         \
            abort();                                                         \
        }                                                                    \
    } while (0)

/* Milisegundos de CLOCK_MONOTONIC: para latidos, duraciones y mov/s. */
static inline long long ahora_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

/*
 * Deadline absoluto "ahora + ms" en CLOCK_REALTIME, que es el reloj que usa
 * pthread_cond_timedwait por defecto (macOS no tiene
 * pthread_condattr_setclock, así que no se puede cambiar de forma portable).
 */
static inline struct timespec deadline_ms(long long ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(ms / 1000);
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return ts;
}

/* Espera con timeout: ETIMEDOUT es un resultado normal, otro error es bug. */
static inline bool esperar_hasta(pthread_cond_t *c, pthread_mutex_t *m,
                                 const struct timespec *fin)
{
    int e = pthread_cond_timedwait(c, m, fin);
    if (e == ETIMEDOUT)
        return false;
    CHECK(e);
    return true;
}

/* Crea un hilo con stack reducido (todos los hilos del programa pasan por aquí). */
static inline void crear_hilo(pthread_t *hilo, void *(*fn)(void *), void *arg)
{
    pthread_attr_t attr;
    CHECK(pthread_attr_init(&attr));
    CHECK(pthread_attr_setstacksize(&attr, STACK_HILO));
    CHECK(pthread_create(hilo, &attr, fn, arg));
    CHECK(pthread_attr_destroy(&attr));
}

/* Aleatoriedad por hilo: rand_r (rand() no es thread-safe). */
static inline int azar_rango(unsigned *semilla, int min, int max)
{
    if (max <= min)
        return min;
    return min + rand_r(semilla) % (max - min + 1);
}

static inline double azar01(unsigned *semilla)
{
    return rand_r(semilla) / ((double)RAND_MAX + 1.0);
}

#endif
