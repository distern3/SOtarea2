/*
 * main.c — Torneo de Komikan Concurrente.
 *
 * Uso: ./torneo N M [config.txt]
 *   N: número total de jugadores (clientes)
 *   M: número máximo de mesas activas simultáneamente
 */
#include "common.h"
#include "config.h"
#include "log.h"
#include "servidor.h"
#include "tablero.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

static void uso(const char *prog)
{
    fprintf(stderr,
            "Uso: %s N M [config.txt]\n"
            "  N  número de jugadores (1..100000)\n"
            "  M  máximo de mesas simultáneas (1..10000)\n",
            prog);
}

static bool leer_entero(const char *txt, long min, long max, int *out)
{
    char *fin;
    errno = 0;
    long v = strtol(txt, &fin, 10);
    if (errno != 0 || fin == txt || *fin != '\0' || v < min || v > max)
        return false;
    *out = (int)v;
    return true;
}

int main(int argc, char **argv)
{
    /* Salida por líneas incluso sin tty (tests, docker, redirecciones). */
    setvbuf(stdout, NULL, _IOLBF, 0);

    int N, M;
    if (argc < 3 || argc > 4) {
        uso(argv[0]);
        return 2;
    }
    if (!leer_entero(argv[1], 1, 100000, &N)) {
        fprintf(stderr, "Error: N inválido '%s'\n", argv[1]);
        uso(argv[0]);
        return 2;
    }
    if (!leer_entero(argv[2], 1, 10000, &M)) {
        fprintf(stderr, "Error: M inválido '%s'\n", argv[2]);
        uso(argv[0]);
        return 2;
    }

    Config cfg;
    if (argc == 4) {
        char err[512];
        if (!config_cargar(argv[3], &cfg, err, sizeof err)) {
            fprintf(stderr, "Error de configuración: %s\n", err);
            return 2;
        }
    } else {
        config_por_defecto(&cfg);
    }

    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        perror("mkdir logs");
        return 1;
    }

    topologia_init();

    /* Si nos lanzaron con SIGINT ignorado (p. ej. en segundo plano desde un
     * script), la señal se descartaría aunque esté bloqueada. Se restablece
     * SIG_DFL; como queda bloqueada, la acción por defecto nunca se ejecuta. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /* Bloquear SIGINT/SIGUSR1 ANTES de crear hilos: todos heredan la
     * máscara y solo el hilo de señales las recibe, con sigwait. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    CHECK(pthread_sigmask(SIG_BLOCK, &set, NULL));

    printf("Torneo de Komikan: N=%d jugadores, M=%d mesas, seed=%u, retardo %d-%d ms%s\n",
           N, M, cfg.seed, cfg.delay_min, cfg.delay_max, argc == 4 ? "" : " (config por defecto)");
    printf("Logs por partida en logs/partida_<n>.log — Ctrl+C para apagar\n\n");

    Servidor srv;
    servidor_init(&srv, N, M, &cfg);

    pthread_t hilo_senales;
    crear_hilo(&hilo_senales, servidor_hilo_senales, &srv);

    servidor_iniciar(&srv);
    servidor_esperar(&srv);

    /* Fin normal (o apagado ya completado): despertar al hilo de señales. */
    CHECK(pthread_kill(hilo_senales, SIGUSR1));
    CHECK(pthread_join(hilo_senales, NULL));

    servidor_imprimir_resultados(&srv);
    if (servidor_apagando(&srv))
        printf("Servidor apagado de forma ordenada: recursos liberados.\n");

    servidor_destruir(&srv);
    consola_destruir();
    return 0;
}
