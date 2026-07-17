#!/usr/bin/env bash
# Build pcg-core (dylib + static lib) on macOS and optionally copy into Unity Plugins.
set -euo pipefail

CONFIGURATION="Release"
COPY_TO_UNITY=false
RUN_TESTS=false
TEST_REGEX=""

usage() {
    echo "Usage: $0 [--config Debug|Release] [--copy-to-unity] [--run-tests] [--test-regex <regex>]"
    echo "  --run-tests          Run all focused functional tests (demo graphs excluded)"
    echo "  --test-regex <regex> Run only matching focused tests"
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
        --test-regex)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --test-regex"
                usage
            fi
            TEST_REGEX="$2"
            RUN_TESTS=true
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
CORE_DIR="$ROOT/pcg-core"
BUILD_DIR="$CORE_DIR/build"
UNITY_PLUGINS="$ROOT/Unity/Assets/PcgPlugin/Plugins/macOS"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "==> Configuring pcg-core ($CONFIGURATION)"
cmake -S "$CORE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION"

echo "==> Building pcg-core"
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" -j"$JOBS"

if $RUN_TESTS; then
    CTEST_ARGS=(--test-dir "$BUILD_DIR" --output-on-failure --label-regex fast --parallel "$JOBS")
    if [[ -n "$TEST_REGEX" ]]; then
        CTEST_ARGS+=(--tests-regex "$TEST_REGEX")
        echo "==> Running focused ctest selection: $TEST_REGEX"
    else
        echo "==> Running all focused ctests"
    fi
    ctest "${CTEST_ARGS[@]}"
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
            dest="$pending"
        fi
        if [[ "$(basename "$dest")" == *.dylib ]]; then
            xattr -cr "$dest" 2>/dev/null || true
            codesign --force --sign - "$dest"
            codesign --verify --verbose=2 "$dest"
            echo "Re-signed -> $dest"
        fi
    done
fi

echo "Done."
