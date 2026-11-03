/*
 * config.c — Parseo y validación de config.txt.
 *
 * Formato:
 *   CLAVE=valor          (parámetros)
 *   # comentario         (hasta fin de línea)
 *   P 0 D ...            (líneas sin '=': celdas del tablero, en orden
 *                         trono, 2 intermedios, 5 filas de 5)
 * Celdas: P = Puma, D = Perro, 0 o . = libre.
 */
#include "common.h"
#include "config.h"

#include <ctype.h>
#include <limits.h>

/* Disposición por defecto: la misma del config.txt entregado. */
static const char DISPOSICION_DEFECTO[NUM_NODOS + 1] =
    "P"       /* trono          */
    ".."      /* intermedios    */
    "....."   /* fila 0         */
    "....."   /* fila 1         */
    ".DD.."   /* fila 2         */
    "DDDDD"   /* fila 3         */
    "DDDDD";  /* fila 4         */

void config_por_defecto(Config *cfg)
{
    cfg->seed = 42;
    cfg->delay_min = 50;
    cfg->delay_max = 300;
    cfg->max_duracion_ms = 15000;
    cfg->arrival_min = 0;
    cfg->arrival_max = 200;
    cfg->fail_prob = 0.0;
    cfg->heartbeat_timeout_ms = 1500;
    cfg->estrategia = 1;
    cfg->log_tablero = 1;
    cfg->tablero_consola = 0;
    memcpy(cfg->disposicion, DISPOSICION_DEFECTO, NUM_NODOS);
}

static char *recortar(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *fin = s + strlen(s);
    while (fin > s && isspace((unsigned char)fin[-1]))
        *--fin = '\0';
    return s;
}

static bool leer_entero(const char *txt, long min, long max, long *out)
{
    char *fin;
    errno = 0;
    long v = strtol(txt, &fin, 10);
    if (errno != 0 || fin == txt || *fin != '\0' || v < min || v > max)
        return false;
    *out = v;
    return true;
}

/* Asigna una clave; retorna false con mensaje si la clave o el valor son inválidos. */
static bool asignar(Config *cfg, const char *clave, const char *valor,
                    char *err, size_t err_len)
{
    struct {
        const char *nombre;
        int *destino;
        long min, max;
    } enteros[] = {
        {"DELAY_MIN", &cfg->delay_min, 0, 60000},
        {"DELAY_MAX", &cfg->delay_max, 0, 60000},
        {"MAX_DURACION_MS", &cfg->max_duracion_ms, 1, 3600000},
        {"ARRIVAL_MIN", &cfg->arrival_min, 0, 600000},
        {"ARRIVAL_MAX", &cfg->arrival_max, 0, 600000},
        {"HEARTBEAT_TIMEOUT_MS", &cfg->heartbeat_timeout_ms, 1, 600000},
        {"ESTRATEGIA", &cfg->estrategia, 0, 1},
        {"LOG_TABLERO", &cfg->log_tablero, 0, 1},
        {"TABLERO_CONSOLA", &cfg->tablero_consola, 0, 1},
    };

    if (strcmp(clave, "SEED") == 0) {
        char *fin;
        errno = 0;
        unsigned long v = strtoul(valor, &fin, 10);
        if (errno != 0 || fin == valor || *fin != '\0' || valor[0] == '-' || v > UINT_MAX) {
            snprintf(err, err_len, "SEED debe ser un entero no negativo (se leyó '%s')", valor);
            return false;
        }
        cfg->seed = (unsigned)v;
        return true;
    }
    if (strcmp(clave, "FAIL_PROB") == 0) {
        char *fin;
        errno = 0;
        double v = strtod(valor, &fin);
        if (errno != 0 || fin == valor || *fin != '\0' || v < 0.0 || v > 1.0) {
            snprintf(err, err_len, "FAIL_PROB debe estar entre 0 y 1 (se leyó '%s')", valor);
            return false;
        }
        cfg->fail_prob = v;
        return true;
    }
    for (size_t i = 0; i < sizeof enteros / sizeof enteros[0]; i++) {
        if (strcmp(clave, enteros[i].nombre) == 0) {
            long v;
            if (!leer_entero(valor, enteros[i].min, enteros[i].max, &v)) {
                snprintf(err, err_len, "%s debe ser un entero entre %ld y %ld (se leyó '%s')",
                         clave, enteros[i].min, enteros[i].max, valor);
                return false;
            }
            *enteros[i].destino = (int)v;
            return true;
        }
    }
    snprintf(err, err_len, "clave desconocida '%s'", clave);
    return false;
}

static bool validar(const Config *cfg, char *err, size_t err_len)
{
    int pumas = 0, perros = 0;
    for (int i = 0; i < NUM_NODOS; i++) {
        if (cfg->disposicion[i] == 'P')
            pumas++;
        else if (cfg->disposicion[i] == 'D')
            perros++;
    }
    if (pumas != 1 || perros != NUM_PERROS) {
        snprintf(err, err_len, "el tablero debe tener exactamente 1 P y %d D (tiene %d P y %d D)",
                 NUM_PERROS, pumas, perros);
        return false;
    }
    if (cfg->delay_min > cfg->delay_max) {
        snprintf(err, err_len, "DELAY_MIN (%d) no puede ser mayor que DELAY_MAX (%d)",
                 cfg->delay_min, cfg->delay_max);
        return false;
    }
    if (cfg->arrival_min > cfg->arrival_max) {
        snprintf(err, err_len, "ARRIVAL_MIN (%d) no puede ser mayor que ARRIVAL_MAX (%d)",
                 cfg->arrival_min, cfg->arrival_max);
        return false;
    }
    /* Una ficha que duerme DELAY_MAX no late: el timeout debe superarlo. */
    if (cfg->heartbeat_timeout_ms <= cfg->delay_max + 100) {
        snprintf(err, err_len, "HEARTBEAT_TIMEOUT_MS (%d) debe ser mayor que DELAY_MAX + 100 (%d)",
                 cfg->heartbeat_timeout_ms, cfg->delay_max + 100);
        return false;
    }
    return true;
}

bool config_cargar(const char *ruta, Config *cfg, char *err, size_t err_len)
{
    config_por_defecto(cfg);

    FILE *f = fopen(ruta, "r");
    if (f == NULL) {
        snprintf(err, err_len, "no se pudo abrir '%s': %s", ruta, strerror(errno));
        return false;
    }

    char linea[512], detalle[256];
    char celdas[NUM_NODOS];
    int num_celdas = 0, num_linea = 0;
    bool ok = true;

    while (ok && fgets(linea, sizeof linea, f) != NULL) {
        num_linea++;
        char *comentario = strchr(linea, '#');
        if (comentario != NULL)
            *comentario = '\0';
        char *txt = recortar(linea);
        if (*txt == '\0')
            continue;

        char *igual = strchr(txt, '=');
        if (igual != NULL) {
            *igual = '\0';
            if (!asignar(cfg, recortar(txt), recortar(igual + 1), detalle, sizeof detalle)) {
                snprintf(err, err_len, "%s:%d: %s", ruta, num_linea, detalle);
                ok = false;
            }
            continue;
        }

        /* Línea de tablero: tokens de un carácter separados por espacios. */
        for (char *tok = strtok(txt, " \t\r\n"); ok && tok != NULL; tok = strtok(NULL, " \t\r\n")) {
            char c = (char)toupper((unsigned char)tok[0]);
            if (tok[1] != '\0' || (c != 'P' && c != 'D' && c != '0' && c != '.')) {
                snprintf(err, err_len, "%s:%d: celda inválida '%s' (use P, D o 0)",
                         ruta, num_linea, tok);
                ok = false;
            } else if (num_celdas >= NUM_NODOS) {
                snprintf(err, err_len, "%s:%d: el tablero tiene más de %d celdas",
                         ruta, num_linea, NUM_NODOS);
                ok = false;
            } else {
                celdas[num_celdas++] = (c == '0') ? '.' : c;
            }
        }
    }
    fclose(f);
    if (!ok)
        return false;

    if (num_celdas != 0 && num_celdas != NUM_NODOS) {
        snprintf(err, err_len,
                 "%s: el tablero tiene %d celdas; se esperaban %d (1 trono + 2 intermedios + 25)",
                 ruta, num_celdas, NUM_NODOS);
        return false;
    }
    if (num_celdas == NUM_NODOS)
        memcpy(cfg->disposicion, celdas, NUM_NODOS);

    if (!validar(cfg, detalle, sizeof detalle)) {
        snprintf(err, err_len, "%s: %s", ruta, detalle);
        return false;
    }
    return true;
}
