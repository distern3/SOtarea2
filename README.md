# Torneo de Komikan Concurrente

## Compilación
Ejecutar `make` en la raíz del proyecto. El código utiliza estrictamente primitivas POSIX para la sincronización.

## Ejecución
El sistema se invoca con la siguiente sintaxis:
`./torneo N M [config.txt]`

- **N**: Número total de clientes/jugadores a procesar.
- **M**: Número máximo de mesas concurrentes permitidas.
- **config.txt** (opcional): Archivo con tiempos de retardo, semilla y disposición del tablero.
