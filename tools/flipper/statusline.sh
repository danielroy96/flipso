#!/bin/sh
# Status line: what the Flipper is up to, at a glance.
#
# This never opens the serial port. The port takes one holder at a time, and a
# status line that grabbed it would break whatever deploy or log stream is
# running. Everything here comes from the filesystem and `lsof`.
PORT=$(ls /dev/cu.usbmodemflip_* 2>/dev/null | head -1)

if [ -z "$PORT" ]; then
    DEVICE="Flipper: not connected"
else
    HOLDER=$(lsof -t "$PORT" 2>/dev/null | head -1)
    if [ -n "$HOLDER" ]; then
        CMD=$(ps -o command= -p "$HOLDER" 2>/dev/null)
        case "$CMD" in
            *"flipctl.py log"*)    BUSY="streaming log" ;;
            *"flipctl.py deploy"*) BUSY="deploying" ;;
            *"flipctl.py shot"*)   BUSY="screenshot" ;;
            *flipctl.py*)          BUSY="flipctl" ;;
            *runfap.py*|*ufbt*)    BUSY="ufbt upload" ;;
            *)                     BUSY="busy" ;;
        esac
        DEVICE="Flipper: $BUSY"
    else
        DEVICE="Flipper: ready"
    fi
fi

FAP=$(cd "$(dirname "$0")/../.." && pwd)/dist/flipso.fap
if [ -f "$FAP" ]; then
    SIZE=$(( $(stat -f%z "$FAP" 2>/dev/null || echo 0) / 1024 ))
    AGE=$(( ($(date +%s) - $(stat -f%m "$FAP" 2>/dev/null || date +%s)) / 60 ))
    if [ "$AGE" -lt 60 ]; then
        BUILD="build ${SIZE}K, ${AGE}m ago"
    else
        BUILD="build ${SIZE}K, $((AGE / 60))h ago"
    fi
else
    BUILD="not built"
fi

printf '%s \033[2m|\033[0m %s' "$DEVICE" "$BUILD"
