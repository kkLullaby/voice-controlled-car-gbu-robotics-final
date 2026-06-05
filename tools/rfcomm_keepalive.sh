#!/usr/bin/env bash
#
# Hold an RFCOMM/SPP link to one or more HC-04 modules. Restarts each link
# if it drops. Run BEFORE pc_voice_controller / web_panel so that
# /dev/rfcomm<N> stays connected and Python can keep its serial open.
#
# Why we need this:
#   Linux `rfcomm bind` is on-demand: opening /dev/rfcomm<N> from Python
#   triggers a fresh SPP handshake every time, which HC-04 hates — the
#   first byte after open() often vanishes before SPP is up. `rfcomm
#   connect` foregrounds the link so the channel is up before our writes.
#
# Usage:
#   sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12 1:04:25:02:05:03:42
#   # arg form is <rfcomm-index>:<MAC>  channel is fixed at 1
#
# Defaults to the two MACs from this project if no args given. Edit if your
# MACs change.
#
# Stop with Ctrl+C — every connect subprocess is terminated.

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "[rfcomm_keepalive] must run as root (sudo)" >&2
    exit 1
fi

if [[ $# -eq 0 ]]; then
    set -- "0:04:25:01:21:00:12" "1:04:25:02:05:03:42"
fi

CHANNEL=1
PIDS=()

# Kill any leftover rfcomm connect processes from previous runs / manual
# experiments. EBUSY shows up when a stale `rfcomm connect 0 ...` still
# holds rfcomm0 and our watchdog can't grab it. We do this BEFORE binding
# anything ourselves so the slate is clean.
existing="$(pgrep -af 'rfcomm connect' || true)"
if [[ -n "$existing" ]]; then
    echo "[rfcomm_keepalive] killing leftover rfcomm connect processes:"
    echo "$existing" | sed 's/^/    /'
    pkill -f 'rfcomm connect' 2>/dev/null || true
    # Give the kernel a moment to release the tty.
    sleep 1
fi

cleanup() {
    echo "[rfcomm_keepalive] shutting down ${#PIDS[@]} connect processes..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    echo "[rfcomm_keepalive] done"
}
trap cleanup EXIT INT TERM

watchdog() {
    local idx="$1"
    local mac="$2"
    local backoff=1
    while true; do
        echo "[rfcomm $idx] connecting to $mac channel $CHANNEL..."
        # `rfcomm connect` blocks until the link drops. Output goes to stderr;
        # we tag it so multi-board logs stay readable.
        if rfcomm connect "$idx" "$mac" "$CHANNEL" 2>&1 \
           | sed -u "s/^/[rfcomm $idx] /"; then
            echo "[rfcomm $idx] disconnected cleanly"
            backoff=1
        else
            echo "[rfcomm $idx] connect failed (exit $?), retry in ${backoff}s"
            sleep "$backoff"
            # cap at 30s so we don't hammer Bluetooth forever
            backoff=$(( backoff < 30 ? backoff * 2 : 30 ))
        fi
    done
}

for spec in "$@"; do
    if [[ "$spec" != *:* ]]; then
        echo "[rfcomm_keepalive] bad arg '$spec', expected <idx>:<MAC>" >&2
        exit 2
    fi
    idx="${spec%%:*}"
    mac="${spec#*:}"
    watchdog "$idx" "$mac" &
    PIDS+=("$!")
done

echo "[rfcomm_keepalive] watching ${#PIDS[@]} link(s). Ctrl+C to stop."
wait
