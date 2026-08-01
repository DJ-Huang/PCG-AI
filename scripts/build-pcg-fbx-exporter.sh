#!/usr/bin/env bash
set -euo pipefail

CONFIGURATION="Release"
COPY_TO_UNITY=false
RUN_TESTS=false

usage() {
    echo "Usage: $0 [--config Debug|Release] [--run-tests]"
    echo "  --copy-to-unity  DEPRECATED (ignored): FBX export is in pcg-server"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) CONFIGURATION="$2"; shift 2 ;;
        --copy-to-unity) COPY_TO_UNITY=true; shift ;;
        --run-tests) RUN_TESTS=true; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE_DIR="$ROOT/pcg-fbx-exporter"
BUILD_DIR="$SOURCE_DIR/build-macos"
UNITY_PLUGINS="$ROOT/Unity/Assets/PcgPlugin/Plugins/Editor/macOS"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION"
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

if $RUN_TESTS; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [[ -f "$BUILD_DIR/libPcgFbxExporter.dylib" ]]; then
    LIBRARY="$BUILD_DIR/libPcgFbxExporter.dylib"
elif [[ -f "$BUILD_DIR/$CONFIGURATION/libPcgFbxExporter.dylib" ]]; then
    LIBRARY="$BUILD_DIR/$CONFIGURATION/libPcgFbxExporter.dylib"
else
    echo "ERROR: libPcgFbxExporter.dylib not found under $BUILD_DIR"
    exit 1
fi

if $COPY_TO_UNITY; then
    echo "WARN: --copy-to-unity is deprecated and ignored. FBX export runs in pcg-server."
fi

echo "Built $LIBRARY"
