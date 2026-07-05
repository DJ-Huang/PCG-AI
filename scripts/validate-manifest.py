#!/usr/bin/env python3
"""Validate that C++ element registrations and node-manifest.json are in sync.

Extracts type names from:
  - pcg-core/src/elements/*.cpp  (map.emplace("TypeName", ...))
  - schema/node-manifest.json   (nodes[].type)

Reports any mismatches and exits with code 1 if out of sync.
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CPP_ELEMENTS_DIR = REPO_ROOT / "pcg-core" / "src" / "elements"
MANIFEST_PATH = REPO_ROOT / "schema" / "node-manifest.json"

EMPLACE_RE = re.compile(r'map\.emplace\(\s*"([^"]+)"')


def extract_cpp_types():
    """Extract registered element type names from C++ source files."""
    cpp_types = set()
    if not CPP_ELEMENTS_DIR.is_dir():
        print(f"ERROR: C++ elements directory not found: {CPP_ELEMENTS_DIR}")
        sys.exit(1)

    for cpp_file in sorted(CPP_ELEMENTS_DIR.glob("*.cpp")):
        text = cpp_file.read_text(encoding="utf-8")
        for match in EMPLACE_RE.finditer(text):
            cpp_types.add(match.group(1))

    return cpp_types


def extract_manifest_types():
    """Extract node type names from node-manifest.json."""
    if not MANIFEST_PATH.is_file():
        print(f"ERROR: node-manifest.json not found: {MANIFEST_PATH}")
        sys.exit(1)

    with open(MANIFEST_PATH, encoding="utf-8") as f:
        manifest = json.load(f)

    return {node["type"] for node in manifest.get("nodes", [])}


def main():
    cpp_types = extract_cpp_types()
    manifest_types = extract_manifest_types()

    cpp_only = cpp_types - manifest_types
    manifest_only = manifest_types - cpp_types

    print(f"C++ elements:     {len(cpp_types)}")
    print(f"Manifest nodes:   {len(manifest_types)}")
    print()

    if cpp_only:
        print("ERROR: Registered in C++ but missing from manifest:")
        for t in sorted(cpp_only):
            print(f"  - {t}")

    if manifest_only:
        print("ERROR: In manifest but not registered in C++:")
        for t in sorted(manifest_only):
            print(f"  - {t}")

    if not cpp_only and not manifest_only:
        print(f"OK: C++ and manifest are in sync ({len(cpp_types)} nodes)")
        return 0

    print()
    print("FAILED: Manifest is out of sync with C++ registrations.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
