#!/usr/bin/env bash
# Sync node-manifest.json from schema/ to Unity Editor/Graph/ for plugin distribution.
# Run this before packaging or distributing the Unity plugin.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

SRC="$REPO_ROOT/schema/node-manifest.json"
DST_EDITOR="$REPO_ROOT/Unity/Assets/PcgPlugin/Editor/Graph/node-manifest.json"
DST_RESOURCES_DIR="$REPO_ROOT/Unity/Assets/PcgPlugin/Resources"
DST_RESOURCES="$DST_RESOURCES_DIR/node-manifest.json"

if [ ! -f "$SRC" ]; then
    echo "ERROR: Source not found: $SRC"
    exit 1
fi

mkdir -p "$(dirname "$DST_EDITOR")" "$DST_RESOURCES_DIR"
cp "$SRC" "$DST_EDITOR"
cp "$SRC" "$DST_RESOURCES"
echo "Synced: $SRC → $DST_EDITOR"
echo "Synced: $SRC → $DST_RESOURCES"
