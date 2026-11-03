/*
 * log.c — Consola thread-safe (mtx_consola).
 */
#include "common.h"
#include "log.h"

#include <stdarg.h>

static pthread_mutex_t mtx_consola = PTHREAD_MUTEX_INITIALIZER;

void consola(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    CHECK(pthread_mutex_lock(&mtx_consola));
    vprintf(fmt, ap);
    putchar('\n');
    CHECK(pthread_mutex_unlock(&mtx_consola));
    va_end(ap);
}

void consola_bloque(const char *prefijo, const char *texto)
{
    CHECK(pthread_mutex_lock(&mtx_consola));
    const char *ini = texto;
    while (*ini != '\0') {
        const char *fin = strchr(ini, '\n');
        size_t largo = fin ? (size_t)(fin - ini) : strlen(ini);
        printf("%s%.*s\n", prefijo, (int)largo, ini);
        ini += largo + (fin ? 1 : 0);
    }
    CHECK(pthread_mutex_unlock(&mtx_consola));
}

void consola_destruir(void)
{
    CHECK(pthread_mutex_destroy(&mtx_consola));
}
