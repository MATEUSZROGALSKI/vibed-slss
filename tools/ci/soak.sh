#!/usr/bin/env bash
# Doc 8.4 / goal G5 — headless soak: run the screensaver session under
# Xvfb + Mesa llvmpipe for SLC_SOAK_SEC (default 8 h), sampling RSS every
# 30 s. Fails if the process exits before the duration (crash) or peak
# RSS exceeds SLC_SOAK_MAX_RSS_KB (default 500 MB).
#
# Usage: soak.sh <path-to-slc_host_linux>
set -u

BIN=${1:?usage: soak.sh <slc_host_linux>}
DURATION_SEC=${SLC_SOAK_SEC:-28800}
MAX_RSS_KB=${SLC_SOAK_MAX_RSS_KB:-512000}
SAMPLE_SEC=${SLC_SOAK_SAMPLE_SEC:-30}

export LIBGL_ALWAYS_SOFTWARE=1
export SLC_TEST_DISABLE_INPUT_CHECK=1
export SLC_STEAM_ROOT=${SLC_STEAM_ROOT:?set SLC_STEAM_ROOT to a fixture steam tree}

"$BIN" --run --allow-test-hooks &
PID=$!
start=$SECONDS
peak=0

while kill -0 "$PID" 2>/dev/null; do
    rss=$(awk '/VmRSS/ {print $2}' "/proc/$PID/status" 2>/dev/null || echo 0)
    [ "${rss:-0}" -gt "$peak" ] && peak=$rss
    if [ $((SECONDS - start)) -ge "$DURATION_SEC" ]; then
        break
    fi
    sleep "$SAMPLE_SEC"
done

elapsed=$((SECONDS - start))
if kill -0 "$PID" 2>/dev/null; then
    kill "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    echo "soak: completed ${elapsed}s, killed cleanly"
else
    wait "$PID"
    rc=$?
    echo "soak: process exited after ${elapsed}s (rc=$rc)"
    if [ "$elapsed" -lt "$DURATION_SEC" ]; then
        echo "soak: FAIL — crash before duration" >&2
        exit 1
    fi
fi

echo "soak: peak RSS ${peak} kB (cap ${MAX_RSS_KB} kB)"
if [ "$peak" -gt "$MAX_RSS_KB" ]; then
    echo "soak: FAIL — RSS cap exceeded" >&2
    exit 1
fi
echo "soak: PASS"
