#!/usr/bin/env bash
# Build and optionally run pcg-server (localhost cook HTTP backend).
set -euo pipefail

CONFIGURATION="Release"
RUN=false
PORT=17890

usage() {
    echo "Usage: $0 [--config Debug|Release] [--run] [--port 17890]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            CONFIGURATION="$2"
            shift 2
            ;;
        --run)
            RUN=true
            shift
            ;;
        --port)
            PORT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_DIR="$ROOT/pcg-server"
BUILD_DIR="$SERVER_DIR/build"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "==> Configuring pcg-server ($CONFIGURATION)"
cmake -S "$SERVER_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION"

echo "==> Building pcg-server"
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" -j"$JOBS"

BIN=""
if [[ -f "$BUILD_DIR/pcg-server" ]]; then
    BIN="$BUILD_DIR/pcg-server"
elif [[ -f "$BUILD_DIR/$CONFIGURATION/pcg-server" ]]; then
    BIN="$BUILD_DIR/$CONFIGURATION/pcg-server"
elif [[ -f "$BUILD_DIR/Release/pcg-server" ]]; then
    BIN="$BUILD_DIR/Release/pcg-server"
fi

if [[ -z "$BIN" ]]; then
    echo "ERROR: pcg-server binary not found under $BUILD_DIR"
    exit 1
fi

echo "==> Built: $BIN"
if $RUN; then
    echo "==> Starting pcg-server on port $PORT"
    exec "$BIN" --port "$PORT"
fi
