#!/usr/bin/env bash
# Run an already-built pcg-server (or build first).
# Replaces any existing listener on PCG_SERVER_PORT before starting.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PCG_SERVER_PORT:-17890}"
BIN=""

"$ROOT/scripts/stop-pcg-server.sh"

for candidate in \
    "$ROOT/pcg-server/build/pcg-server" \
    "$ROOT/pcg-server/build/Release/pcg-server" \
    "$ROOT/pcg-server/build/Debug/pcg-server"; do
    if [[ -x "$candidate" ]]; then
        BIN="$candidate"
        break
    fi
done

if [[ -z "$BIN" ]]; then
    echo "==> pcg-server not found; building..."
    "$ROOT/scripts/build-pcg-server.sh"
    BIN="$ROOT/pcg-server/build/pcg-server"
fi

exec "$BIN" --port "$PORT"
