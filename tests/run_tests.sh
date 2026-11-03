#!/bin/sh
# Tests de integración del torneo. Uso: tests/run_tests.sh (desde la raíz).
# Funciona en Linux y macOS; valgrind y TSan se ejecutan si están disponibles.

cd "$(dirname "$0")/.." || exit 1
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
pasados=0
fallados=0

ok()   { pasados=$((pasados + 1)); echo "  OK    $1"; }
falla() { fallados=$((fallados + 1)); echo "  FALLA $1"; [ -n "$2" ] && sed 's/^/        /' "$2" | tail -15; }

# Ejecuta el torneo con un límite de tiempo (sin depender de `timeout`).
correr() { # correr <limite_s> <salida> args...
    limite=$1; salida=$2; shift 2
    ./torneo "$@" > "$salida" 2>&1 &
    pid=$!
    ( sleep "$limite"; kill -KILL "$pid" 2>/dev/null ) &
    perro=$!
    wait "$pid"; codigo=$?
    kill "$perro" 2>/dev/null; wait "$perro" 2>/dev/null
    return $codigo
}

echo "== Compilación"
make clean > /dev/null
if make > "$OUT/make.txt" 2>&1 && ! grep -qi "warning" "$OUT/make.txt"; then
    ok "make sin warnings"
else
    falla "make con errores o warnings" "$OUT/make.txt"; exit 1
fi

echo "== Argumentos y configuración"
for args in "" "0 1" "5 x" "5 2 no_existe.txt"; do
    # shellcheck disable=SC2086
    ./torneo $args > /dev/null 2>&1
    [ $? -eq 2 ] && ok "rechaza './torneo $args'" || falla "no rechaza './torneo $args'"
done
printf 'DELAY_MAX=5000\n' > "$OUT/malo.txt"
./torneo 2 1 "$OUT/malo.txt" > "$OUT/r.txt" 2>&1
[ $? -eq 2 ] && grep -q "HEARTBEAT" "$OUT/r.txt" && ok "config inválido rechazado con mensaje" \
    || falla "config inválido" "$OUT/r.txt"

echo "== Partidas"
rm -rf logs
correr 30 "$OUT/r.txt" 2 1 config.txt
if [ $? -eq 0 ] && grep -q "Tablero inicial" logs/partida_1.log && grep -q "Resumen" logs/partida_1.log; then
    ok "2 1: una partida con log completo"
else
    falla "2 1" "$OUT/r.txt"
fi

correr 120 "$OUT/r.txt" 20 3 config.txt
max=$(sed -n 's/.*simultáneas: \([0-9]*\)\/3.*/\1/p' "$OUT/r.txt")
if [ -n "$max" ] && [ "$max" -le 3 ] && ! grep -qE "activas [4-9]/3" "$OUT/r.txt"; then
    ok "20 3: nunca más de 3 mesas activas (máximo $max)"
else
    falla "20 3: límite de mesas" "$OUT/r.txt"
fi

correr 60 "$OUT/r.txt" 7 2 config.txt
[ "$(grep -c "SIN_PARTIDA" "$OUT/r.txt")" -eq 1 ] && ok "7 2: un jugador sin rival" \
    || falla "7 2: jugador sin rival" "$OUT/r.txt"

correr 30 "$OUT/r.txt" 2 1 tests/config_puma_atrapado.txt
grep -q "ganan Perros (Puma acorralado)" "$OUT/r.txt" && ok "Puma atrapado: ganan Perros" \
    || falla "Puma atrapado" "$OUT/r.txt"

correr 30 "$OUT/r.txt" 2 1 tests/config_captura.txt
grep -q "captura perro" "$OUT/r.txt" && ok "hay capturas por salto" || falla "capturas" "$OUT/r.txt"

correr 30 "$OUT/r.txt" 4 2 tests/config_empate.txt
grep -q "empate (tiempo máximo" "$OUT/r.txt" && ok "empate por tiempo máximo" || falla "empate" "$OUT/r.txt"

correr 180 "$OUT/r.txt" 40 20 config.txt
if [ $? -eq 0 ] && ! grep -q "abandono" "$OUT/r.txt"; then
    ok "FAIL_PROB=0: 20 partidas sin abandonos falsos"
else
    falla "abandonos falsos con FAIL_PROB=0" "$OUT/r.txt"
fi

echo "== Resiliencia (4.1) y estrés"
rm -rf logs
correr 60 "$OUT/r.txt" 20 5 tests/config_fallo.txt
if [ $? -eq 0 ] && grep -q "abandono" "$OUT/r.txt" && cat logs/*.log | grep -q "Liberando casilla"; then
    ok "fallos: abandono detectado y casillas huérfanas liberadas"
else
    falla "fallos (4.1)" "$OUT/r.txt"
fi

correr 120 "$OUT/r.txt" 200 50 tests/config_estres.txt
[ $? -eq 0 ] && grep -q "Partidas: 100" "$OUT/r.txt" && ok "estrés 200 50 sin retardos: termina" \
    || falla "estrés (¿deadlock o timeout?)" "$OUT/r.txt"

echo "== SIGINT (4.2)"
./torneo 20 1 config.txt > "$OUT/int.txt" 2>&1 &
pid=$!
sleep 2
kill -INT "$pid"
wait "$pid"; codigo=$?
if [ $codigo -eq 0 ] && grep -q "SIGINT recibido" "$OUT/int.txt" && ! grep -q "PENDIENTE" "$OUT/int.txt" \
   && grep -q "esperaba mesa" "$OUT/int.txt" && grep -q "apagado de forma ordenada" "$OUT/int.txt"; then
    ok "SIGINT: cierre ordenado, todos con resultado (incl. par en tránsito)"
else
    falla "SIGINT" "$OUT/int.txt"
fi

echo "== Herramientas (si están disponibles)"
if make tsan > "$OUT/tsan_make.txt" 2>&1; then
    ./torneo_tsan 6 3 config.txt > "$OUT/tsan.txt" 2>&1
    grep -q "ThreadSanitizer" "$OUT/tsan.txt" && falla "TSan reporta data races" "$OUT/tsan.txt" \
        || ok "TSan: sin data races"
else
    echo "  --    TSan no disponible"
fi
if command -v valgrind > /dev/null 2>&1; then
    valgrind --leak-check=full --track-fds=yes --error-exitcode=99 \
        ./torneo 4 2 tests/config_valgrind.txt > "$OUT/vg.txt" 2>&1
    if grep -q "All heap blocks were freed" "$OUT/vg.txt" && [ "$(grep -c 'Open file descriptor' "$OUT/vg.txt")" -le 3 ]; then
        ok "valgrind: 0 fugas y solo fd estándar"
    else
        falla "valgrind" "$OUT/vg.txt"
    fi
else
    echo "  --    valgrind no disponible (use: docker compose run --rm valgrind)"
fi

echo
echo "Resultado: $pasados pasados, $fallados fallados"
[ $fallados -eq 0 ]
