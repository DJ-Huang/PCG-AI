#!/usr/bin/env bash
# Run an already-built pcg-server (or build first).
# Leave existing listeners alone; the server fails if its address is in use.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PCG_SERVER_PORT:-17890}"
BIN=""

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
