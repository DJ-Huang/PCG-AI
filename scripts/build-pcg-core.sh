#!/usr/bin/env bash
# Build pcg-core (shared + static). Unity no longer loads these plugins;
# use scripts/build-pcg-server.sh for Editor cook.
set -euo pipefail

CONFIGURATION="Release"
COPY_TO_UNITY=false
RUN_TESTS=false
TEST_REGEX=""

usage() {
    echo "Usage: $0 [--config Debug|Release] [--run-tests] [--test-regex <regex>]"
    echo "  --run-tests          Run all focused functional tests (demo graphs excluded)"
    echo "  --test-regex <regex> Run only matching focused tests"
    echo "  --copy-to-unity      DEPRECATED (ignored): Unity uses pcg-server, not Plugins dylib"
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

case "$(uname -s)" in
    Linux) SHARED_LIB_NAME="libPcgCore.so" ;;
    *)     SHARED_LIB_NAME="libPcgCore.dylib" ;;
esac

# Single-config generators place artifacts in build/; multi-config generators
# put them in the selected configuration's subdirectory.
if [[ -f "$BUILD_DIR/$SHARED_LIB_NAME" ]]; then
    ARTIFACT_DIR="$BUILD_DIR"
elif [[ -f "$BUILD_DIR/$CONFIGURATION/$SHARED_LIB_NAME" ]]; then
    ARTIFACT_DIR="$BUILD_DIR/$CONFIGURATION"
else
    echo "ERROR: $SHARED_LIB_NAME not found under $BUILD_DIR"
    exit 1
fi

SHARED_LIB="$ARTIFACT_DIR/$SHARED_LIB_NAME"
STATIC_LIB="$ARTIFACT_DIR/libPcgCore.a"
for artifact in "$SHARED_LIB" "$STATIC_LIB"; do
    if [[ ! -f "$artifact" ]]; then
        echo "ERROR: Missing build artifact: $artifact"
        exit 1
    fi
done

echo "==> Artifacts:"
ls -la "$SHARED_LIB" "$STATIC_LIB"

if $COPY_TO_UNITY; then
    echo "WARN: --copy-to-unity is deprecated and ignored."
    echo "      Unity no longer loads PcgCore from Plugins/. Use scripts/build-pcg-server.sh instead."
fi

echo "Done."
