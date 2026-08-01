#!/usr/bin/env python3
"""Validate every committed .pcgsubgraph asset against the versioned schema."""

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = ROOT / "schema" / "subgraph-schema.json"
EXCLUDED_PARTS = {".git", "Library", "node_modules"}


def main() -> int:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    validator = Draft202012Validator(schema)
    failures = []

    for path in sorted(ROOT.rglob("*.pcgsubgraph")):
        if EXCLUDED_PARTS.intersection(path.parts):
            continue
        try:
            asset = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            failures.append(f"{path.relative_to(ROOT)}: invalid JSON: {error}")
            continue

        for error in validator.iter_errors(asset):
            location = error.json_path if error.json_path else "$"
            failures.append(f"{path.relative_to(ROOT)}: {location}: {error.message}")

    if failures:
        print("Subgraph schema validation failed:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1

    print("All .pcgsubgraph assets validate against schema/subgraph-schema.json.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
