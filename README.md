# Torneo de Komikan Concurrente

Tarea 2 de Sistemas Operativos: un servidor empareja N jugadores (Puma vs
Perros) en un máximo de M mesas simultáneas. En cada mesa, 13 hilos (1 Puma y
12 Perros) se mueven sin turnos, y un hilo Juez arbitra la partida. La
sincronización usa solo `pthread_mutex` y `pthread_cond` (POSIX).

## Compilación y ejecución

```sh
make
./torneo N M [config.txt]
```

- **N**: número total de jugadores (clientes).
- **M**: número máximo de mesas activas simultáneamente.
- **config.txt** (opcional): parámetros y disposición inicial del tablero. Sin
  archivo, se usan los valores por defecto.

Ejemplos: `./torneo 10 3 config.txt` y `./torneo 7 2` (un jugador queda sin
rival). Con **Ctrl+C** el servidor se apaga de forma ordenada.

Compila en Linux (gcc) y macOS (clang) con `-Wall -Wextra -std=c17`.

## Salida

- **Consola:** llegadas, emparejamientos, asignación de mesas, capturas,
  resultado de cada partida y una tabla final por jugador.
- **`logs/partida_<n>.log`:** cada movimiento, cada rechazo del Juez y el
  tablero tras cada movimiento. Para seguirlo en vivo:
  `tail -f logs/partida_1.log`.

## config.txt

| Clave | Default | Descripción |
|---|---|---|
| `SEED` | 42 | Semilla base (reproduce las decisiones de cada hilo, no el intercalado). |
| `DELAY_MIN` / `DELAY_MAX` | 50 / 300 | Retardo entre movimientos de una ficha (ms). |
| `MAX_DURACION_MS` | 15000 | Al cumplirse, la partida termina en empate. |
| `ARRIVAL_MIN` / `ARRIVAL_MAX` | 0 / 200 | Retardo de llegada de cada jugador (ms). |
| `FAIL_PROB` | 0 | Probabilidad de fallo simulado por ciclo de ficha (sección 4.1). |
| `HEARTBEAT_TIMEOUT_MS` | 1500 | Tiempo sin latido para declarar caída una ficha. |
| `ESTRATEGIA` | 1 | 1 = heurística, 0 = movimientos al azar. |
| `LOG_TABLERO` | 1 | Tablero tras cada movimiento en el log. |
| `TABLERO_CONSOLA` | 0 | Tablero tras cada movimiento también en la consola. |

Las líneas sin `=` describen el tablero: 1 celda de trono, 2 intermedias y 5
filas de 5, con `P` (Puma), `D` (Perro) o `0` (libre).

## Tests y herramientas

```sh
tests/run_tests.sh        # integración: mesas, N impar, capturas, fallos, estrés, SIGINT, TSan
make tsan && ./torneo_tsan 6 3 config.txt   # ThreadSanitizer
```

Con Docker, para tener un Linux fijo con valgrind:

```sh
docker compose run --rm valgrind
docker compose run --rm tests
docker compose run --rm torneo     # demo de Ctrl+C
```

## Estructura

| Archivo | Contenido |
|---|---|
| `main.c` | Argumentos, máscara de señales, arranque, tabla final y limpieza. |
| `servidor.c` | Clientes, cola FIFO, Matchmaker, límite de M mesas e hilo de señales (`sigwait`). |
| `mesa.c` | Hilo Juez, reglas (`juez_validar` / `juez_aplicar`), arbitraje, cierre y casillas huérfanas. |
| `jugador.c` | Ciclo de una ficha y heurística de movimientos. |
| `tablero.c` | Topología, saltos, distancias, render y monitor de casillas. |
| `config.c`, `log.c` | Configuración y consola thread-safe. |
| `docs/plan.md` | Diseño completo y decisiones. |
