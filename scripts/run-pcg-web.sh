#!/usr/bin/env bash
# Start pcg-server (cook API) and the Vite web editor together.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PCG_PORT="${PCG_SERVER_PORT:-17890}"
VITE_PORT="${VITE_PORT:-5173}"
VITE_HOST="${VITE_HOST:-127.0.0.1}"

SERVER_PID=""
VITE_PID=""
STARTED_SERVER=0
STARTED_VITE=0

port_listening() {
    lsof -iTCP:"$1" -sTCP:LISTEN -P -n >/dev/null 2>&1
}

wait_http() {
    local url="$1"
    local label="$2"
    local i
    for i in $(seq 1 50); do
        if curl -sf "$url" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    echo "[pcg-web] ERROR: $label did not become ready ($url)" >&2
    return 1
}

cleanup() {
    if [[ "$STARTED_VITE" -eq 1 && -n "$VITE_PID" ]]; then
        kill "$VITE_PID" 2>/dev/null || true
        wait "$VITE_PID" 2>/dev/null || true
    fi
    if [[ "$STARTED_SERVER" -eq 1 && -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT INT TERM

find_pcg_server() {
    local candidate
    for candidate in \
        "$ROOT/pcg-server/build/pcg-server" \
        "$ROOT/pcg-server/build/Release/pcg-server" \
        "$ROOT/pcg-server/build/Debug/pcg-server"; do
        if [[ -x "$candidate" ]]; then
            return 0
        fi
    done
    return 1
}

if port_listening "$PCG_PORT"; then
    echo "[pcg-web] pcg-server already listening on http://127.0.0.1:${PCG_PORT}"
else
    if ! find_pcg_server; then
        echo "[pcg-web] pcg-server not found; building..."
        "$ROOT/scripts/build-pcg-server.sh"
    fi
    echo "[pcg-web] starting pcg-server on http://127.0.0.1:${PCG_PORT}"
    "$ROOT/scripts/run-pcg-server.sh" &
    SERVER_PID=$!
    STARTED_SERVER=1
    wait_http "http://127.0.0.1:${PCG_PORT}/v1/health" "pcg-server"
fi

if port_listening "$VITE_PORT"; then
    echo "[pcg-web] Vite already listening on http://${VITE_HOST}:${VITE_PORT}"
else
    if [[ ! -d "$ROOT/web/pcg-editor/node_modules" ]]; then
        echo "[pcg-web] node_modules missing; running npm install..."
        (cd "$ROOT/web/pcg-editor" && npm install)
    fi
    echo "[pcg-web] starting web editor on http://${VITE_HOST}:${VITE_PORT}"
    (
        cd "$ROOT/web/pcg-editor"
        npm run dev -- --host "$VITE_HOST" --port "$VITE_PORT"
    ) &
    VITE_PID=$!
    STARTED_VITE=1
    wait_http "http://${VITE_HOST}:${VITE_PORT}/" "Vite"
fi

echo ""
echo "[pcg-web] ready"
echo "  API:    http://127.0.0.1:${PCG_PORT}/v1/health"
echo "  Editor: http://${VITE_HOST}:${VITE_PORT}/"
echo "  Review: http://${VITE_HOST}:${VITE_PORT}/review?graph=<path>"
echo ""
echo "Press Ctrl+C to stop services started by this script."

if [[ -n "$SERVER_PID" && -n "$VITE_PID" ]]; then
    wait "$SERVER_PID" "$VITE_PID"
elif [[ -n "$SERVER_PID" ]]; then
    wait "$SERVER_PID"
elif [[ -n "$VITE_PID" ]]; then
    wait "$VITE_PID"
else
    echo "[pcg-web] both services were already running; nothing to wait on." >&2
    trap - EXIT INT TERM
    exit 0
fi
