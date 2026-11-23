#!/usr/bin/env bash
# Start pcg-server (cook API) and the Vite web editor together.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PCG_PORT="${PCG_SERVER_PORT:-17890}"
VITE_PORT="${VITE_PORT:-5173}"
VITE_HOST="${VITE_HOST:-127.0.0.1}"
VITE_URL_HOST="$VITE_HOST"
if [[ "$VITE_HOST" == *:* ]]; then
    VITE_URL_HOST="[$VITE_HOST]"
fi
case "$VITE_HOST" in
    0.0.0.0) VITE_PROBE_HOST="127.0.0.1" ;;
    ::)      VITE_PROBE_HOST="[::1]" ;;
    *)       VITE_PROBE_HOST="$VITE_URL_HOST" ;;
esac

SERVER_PID=""
VITE_PID=""

port_listening() {
    lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1
}

wait_http() {
    local pid="$1" port="$2" url="$3" label="$4"
    local i
    for ((i = 0; i < 50; i++)); do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "[pcg-web] ERROR: $label exited before becoming ready ($url)" >&2
            return 1
        fi
        # A successful HTTP response alone might be from another process that
        # won the port between the preflight check and our child's bind.
        if lsof -nP -a -p "$pid" -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1 &&
            curl --noproxy '*' --max-time 1 -fs "$url" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    echo "[pcg-web] ERROR: $label did not become ready ($url)" >&2
    return 1
}

cleanup() {
    local pid
    # These are the direct children started below, never PIDs found by port.
    for pid in "$VITE_PID" "$SERVER_PID"; do
        if [[ -n "$pid" ]]; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    for pid in "$VITE_PID" "$SERVER_PID"; do
        if [[ -n "$pid" ]]; then
            wait "$pid" 2>/dev/null || true
        fi
    done
}

for tool in lsof curl node; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "[pcg-web] ERROR: $tool is required; install it and retry" >&2
        exit 1
    fi
done
for port in "$PCG_PORT" "$VITE_PORT"; do
    if ! [[ "$port" =~ ^[1-9][0-9]{0,4}$ && "$port" -le 65535 ]]; then
        echo "[pcg-web] ERROR: invalid TCP port: $port" >&2
        exit 1
    fi
done
if [[ "$PCG_PORT" == "$VITE_PORT" ]]; then
    echo "[pcg-web] ERROR: PCG_SERVER_PORT and VITE_PORT must differ" >&2
    exit 1
fi
if port_listening "$PCG_PORT"; then
    echo "[pcg-web] ERROR: API port $PCG_PORT is already in use; choose another PCG_SERVER_PORT" >&2
    exit 1
fi
if port_listening "$VITE_PORT"; then
    echo "[pcg-web] ERROR: editor port $VITE_PORT is already in use; choose another VITE_PORT" >&2
    exit 1
fi

if [[ ! -f "$ROOT/web/pcg-editor/node_modules/vite/bin/vite.js" ]]; then
    if ! command -v npm >/dev/null 2>&1; then
        echo "[pcg-web] ERROR: npm is required to install the missing Vite dependency" >&2
        exit 1
    fi
    echo "[pcg-web] Vite missing; running npm install..."
    (cd "$ROOT/web/pcg-editor" && npm install)
fi

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo "[pcg-web] starting pcg-server on http://127.0.0.1:${PCG_PORT}"
"$ROOT/scripts/run-pcg-server.sh" &
SERVER_PID=$!
wait_http "$SERVER_PID" "$PCG_PORT" "http://127.0.0.1:${PCG_PORT}/v1/health" "pcg-server"

EDITOR_URL="http://${VITE_URL_HOST}:${VITE_PORT}"
echo "[pcg-web] starting web editor on $EDITOR_URL"
(
    cd "$ROOT/web/pcg-editor"
    # Exec the local CLI directly so the tracked PID is the TCP listener.
    exec node node_modules/vite/bin/vite.js --host "$VITE_HOST" --port "$VITE_PORT" --strictPort
) &
VITE_PID=$!
wait_http "$VITE_PID" "$VITE_PORT" "http://${VITE_PROBE_HOST}:${VITE_PORT}/" "Vite"

echo ""
echo "[pcg-web] ready"
echo "  API:    http://127.0.0.1:${PCG_PORT}/v1/health"
echo "  MCP:    http://127.0.0.1:${PCG_PORT}/mcp"
echo "  Editor: $EDITOR_URL/"
echo "  Review: $EDITOR_URL/review?graph=<path>"
echo ""
echo "Press Ctrl+C to stop services started by this script."

# Bash 3 (macOS default) does not support wait -n. Stop the other child when
# either exits rather than waiting indefinitely for both.
while kill -0 "$SERVER_PID" 2>/dev/null && kill -0 "$VITE_PID" 2>/dev/null; do
    sleep 0.2
done
echo "[pcg-web] ERROR: a service exited; stopping the launcher" >&2
exit 1
