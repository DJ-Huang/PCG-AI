#!/usr/bin/env bash
# Stop pcg-server (or any process) listening on PCG_SERVER_PORT (default 17890).
set -euo pipefail

PORT="${PCG_SERVER_PORT:-17890}"

port_listening() {
    lsof -iTCP:"$PORT" -sTCP:LISTEN -P -n >/dev/null 2>&1
}

PIDS="$(lsof -tiTCP:"$PORT" -sTCP:LISTEN 2>/dev/null || true)"
if [[ -z "$PIDS" ]]; then
    exit 0
fi

echo "==> Stopping listener on port $PORT (pid: ${PIDS//$'\n'/ })"
kill $PIDS 2>/dev/null || true

for _ in $(seq 1 20); do
    if ! port_listening; then
        exit 0
    fi
    sleep 0.1
done

echo "==> Force-stopping listener on port $PORT"
kill -9 $PIDS 2>/dev/null || true
