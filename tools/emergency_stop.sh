#!/usr/bin/env bash
#
# Spam S\n to every /dev/rfcomm* in sight. Use when the car is going somewhere
# you didn't want it to go and you don't have time to click around in a TUI.
#
# Requires rfcomm_keepalive.sh (or `rfcomm connect`) to already be holding
# the SPP links — this script does NOT open new RFCOMM connections, it
# assumes you're trying to stop a car you were already controlling.
#
# Usage:
#   ./tools/emergency_stop.sh                       # all /dev/rfcomm*
#   ./tools/emergency_stop.sh /dev/rfcomm0          # one specific port
#   BURST=10 ./tools/emergency_stop.sh              # send more copies

set -u

BURST="${BURST:-5}"

if [[ $# -gt 0 ]]; then
    PORTS=("$@")
else
    shopt -s nullglob
    PORTS=(/dev/rfcomm*)
    shopt -u nullglob
fi

if [[ ${#PORTS[@]} -eq 0 ]]; then
    echo "[STOP] no /dev/rfcomm* found — is rfcomm_keepalive running?" >&2
    exit 1
fi

echo "[STOP] sending ${BURST}x 'S' to: ${PORTS[*]}"
for port in "${PORTS[@]}"; do
    (
        for _ in $(seq 1 "$BURST"); do
            # printf instead of echo so \n is literal newline, no surprises.
            printf 'S\n' > "$port" 2>/dev/null || echo "[STOP] $port: write failed" >&2
        done
        echo "[STOP] $port: done"
    ) &
done
wait
echo "[STOP] all sends complete"
