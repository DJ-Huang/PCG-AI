#!/usr/bin/env bash
# Build pcg-core (dylib + static lib) on macOS and optionally copy into Unity Plugins.
set -euo pipefail

CONFIGURATION="Release"
COPY_TO_UNITY=false
RUN_TESTS=false

usage() {
    echo "Usage: $0 [--config Debug|Release] [--copy-to-unity] [--run-tests]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            CONFIGURATION="$2"
            shift 2
            ;;
        --copy-to-unity)
            COPY_TO_UNITY=true
            shift
            ;;
        --run-tests)
            RUN_TESTS=true
            shift
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
CORE_DIR="$ROOT/pcg-core"
BUILD_DIR="$CORE_DIR/build"
UNITY_PLUGINS="$ROOT/Unity/Assets/PcgPlugin/Plugins/macOS"

echo "==> Configuring pcg-core ($CONFIGURATION)"
cmake -S "$CORE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION"

echo "==> Building pcg-core"
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

if $RUN_TESTS; then
    echo "==> Running ctest"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

# Single-config generators (Ninja/Make) place artifacts directly in build/.
# Multi-config would use build/Release — macOS Xcode is multi-config but we use default Unix Makefiles.
if [[ -f "$BUILD_DIR/libPcgCore.dylib" ]]; then
    DYLIB="$BUILD_DIR/libPcgCore.dylib"
    STATIC_LIB="$BUILD_DIR/libPcgCore.a"
elif [[ -f "$BUILD_DIR/$CONFIGURATION/libPcgCore.dylib" ]]; then
    DYLIB="$BUILD_DIR/$CONFIGURATION/libPcgCore.dylib"
    STATIC_LIB="$BUILD_DIR/$CONFIGURATION/libPcgCore.a"
else
    echo "ERROR: libPcgCore.dylib not found under $BUILD_DIR"
    exit 1
fi

for artifact in "$DYLIB" "$STATIC_LIB"; do
    if [[ ! -f "$artifact" ]]; then
        echo "ERROR: Missing build artifact: $artifact"
        exit 1
    fi
done

echo "==> Artifacts:"
ls -la "$DYLIB" "$STATIC_LIB"

if $COPY_TO_UNITY; then
    mkdir -p "$UNITY_PLUGINS"
    for src in "$DYLIB" "$STATIC_LIB"; do
        dest="$UNITY_PLUGINS/$(basename "$src")"
        if cp -f "$src" "$dest" 2>/dev/null; then
            echo "Copied -> $dest"
        else
            pending="${dest}.new"
            cp -f "$src" "$pending"
            echo "WARN: Unity may have locked $(basename "$dest"); wrote ${pending} instead."
        fi
    done
fi

echo "Done."
