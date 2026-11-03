/*
 * log.h — Salida por consola thread-safe.
 *
 * mtx_consola es la HOJA de la jerarquía de locks: se puede tomar teniendo
 * cualquier otro lock, y nunca se toma otro lock mientras se tiene.
 */
#ifndef LOG_H
#define LOG_H

#include "common.h"

/* Imprime una línea completa (agrega '\n') de forma atómica. */
void consola(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Imprime un bloque multilínea anteponiendo `prefijo` a cada línea. */
void consola_bloque(const char *prefijo, const char *texto);

void consola_destruir(void);

#endif
