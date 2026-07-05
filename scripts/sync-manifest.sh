#!/usr/bin/env bash
# Sync node-manifest.json from schema/ to Unity Editor/Graph/ for plugin distribution.
# Run this before packaging or distributing the Unity plugin.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

SRC="$REPO_ROOT/schema/node-manifest.json"
DST_DIR="$REPO_ROOT/Unity/Assets/PcgPlugin/Editor/Graph"
DST="$DST_DIR/node-manifest.json"

if [ ! -f "$SRC" ]; then
    echo "ERROR: Source not found: $SRC"
    exit 1
fi

mkdir -p "$DST_DIR"
cp "$SRC" "$DST"
echo "Synced: $SRC → $DST"
