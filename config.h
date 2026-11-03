/*
 * config.h — Parámetros del torneo, leídos desde config.txt (opcional).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

typedef struct {
    unsigned seed;
    int delay_min, delay_max;     /* retardo entre movimientos (ms)          */
    int max_duracion_ms;          /* al cumplirse: empate                    */
    int arrival_min, arrival_max; /* retardo de llegada de cada cliente (ms) */
    double fail_prob;             /* prob. de fallo por ciclo de ficha (4.1) */
    int heartbeat_timeout_ms;     /* ms sin latido => ficha caída            */
    int estrategia;               /* 1 = heurística, 0 = azar puro           */
    int log_tablero;              /* 1 = tablero tras cada mov. en el log    */
    int tablero_consola;          /* 1 = tablero tras cada mov. en consola   */
    /* Disposición inicial: 'P', 'D' o '.', en orden T, L, R, fila 0..4.    */
    char disposicion[NUM_NODOS];
} Config;

void config_por_defecto(Config *cfg);

/* Carga y valida `ruta`. Si falla, deja un mensaje en `err` y retorna false. */
bool config_cargar(const char *ruta, Config *cfg, char *err, size_t err_len);

#endif
