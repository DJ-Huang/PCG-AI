#!/usr/bin/env python3
"""Remove generated cook data embedded in Unity sample scenes.

PCG sample scenes keep graphs and host setup in source control. Cooked preview meshes
are reproducible output and should be regenerated after opening a scene.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


DOCUMENT_START = re.compile(r"(?=^--- !u!\d+ &-?\d+\s*$)", re.MULTILINE)
MESH_DOCUMENT = re.compile(r"^--- !u!43 &(-?\d+)\s*$", re.MULTILINE)
LAST_RESULT = re.compile(r"^  m_LastResultJson:\s*(.*)$", re.MULTILINE)


def scene_paths(inputs: list[Path]) -> list[Path]:
    paths: set[Path] = set()
    for input_path in inputs:
        if input_path.is_dir():
            paths.update(input_path.rglob("*.scene"))
        elif input_path.suffix == ".scene":
            paths.add(input_path)
        else:
            raise ValueError(f"Not a Unity scene or directory: {input_path}")
    return sorted(paths)


def embedded_mesh_ids(source: str) -> list[str]:
    return MESH_DOCUMENT.findall(source)


def strip_scene(source: str) -> tuple[str, list[str]]:
    parts = DOCUMENT_START.split(source)
    kept: list[str] = []
    removed: list[str] = []

    for part in parts:
        match = MESH_DOCUMENT.match(part)
        if match:
            removed.append(match.group(1))
        else:
            kept.append(part)

    result = "".join(kept)
    for file_id in removed:
        reference = re.compile(rf"\{{fileID:\s*{re.escape(file_id)}\}}")
        result, count = reference.subn("{fileID: 0}", result)
        if count == 0:
            raise ValueError(f"Embedded Mesh {file_id} has no m_Mesh reference")

    if embedded_mesh_ids(result):
        raise ValueError("Embedded Mesh document remained after rewrite")
    result = LAST_RESULT.sub("  m_LastResultJson: ''", result)
    return result, removed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail when a scene contains an embedded Mesh without changing files",
    )
    args = parser.parse_args()

    failures: list[str] = []
    for path in scene_paths(args.paths):
        source = path.read_text(encoding="utf-8")
        mesh_ids = embedded_mesh_ids(source)
        has_result = any(value.strip() not in {"", "''"} for value in LAST_RESULT.findall(source))
        if not mesh_ids and not has_result:
            continue
        if args.check:
            details: list[str] = []
            if mesh_ids:
                details.append(f"{len(mesh_ids)} embedded Mesh object(s)")
            if has_result:
                details.append("cached cook result")
            failures.append(f"{path}: {', '.join(details)}")
            continue

        result, removed = strip_scene(source)
        path.write_text(result, encoding="utf-8")
        print(f"Sanitized generated cook data in {path} ({len(removed)} Mesh object(s))")

    if failures:
        print("Generated cook data is embedded in source scenes:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    if args.check:
        print("Unity sample scenes contain no embedded cook data.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
