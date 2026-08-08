#!/usr/bin/env python3
"""Parse pcg-server /v1/cook PCGR binary responses (v1 envelope).

Ported from web/pcg-editor/src/cookResult.ts — use when cook returns binary and
you need the error string or counts without opening the web editor.

Usage:
  curl -sS -X POST http://127.0.0.1:17890/v1/cook -d @graph.json \\
    -H 'Content-Type: application/json' -o /tmp/out.pcgr
  python3 parse_pcgr.py /tmp/out.pcgr
  python3 parse_pcgr.py /tmp/out.pcgr --json
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

COOK_RESULT_MAGIC = 0x52474350  # 'PCGR'
COOK_RESULT_VERSION = 1


def _read_blob(data: bytes, offset: int) -> tuple[bytes, int]:
    if offset + 4 > len(data):
        raise ValueError("Truncated blob length")
    (length,) = struct.unpack_from("<I", data, offset)
    offset += 4
    end = offset + length
    if end > len(data):
        raise ValueError("Truncated blob payload")
    return data[offset:end], end


def parse_cook_result(data: bytes) -> dict:
    if len(data) < 48:
        raise ValueError("Cook result too short")

    magic, version, code, kind, nodes_executed, nodes_skipped = struct.unpack_from(
        "<IIiIii", data, 0
    )
    if magic != COOK_RESULT_MAGIC:
        raise ValueError(f"Bad cook result magic 0x{magic:08x}")
    if version != COOK_RESULT_VERSION:
        raise ValueError(f"Unsupported cook result version {version}")

    graph_execute_ms, binary_write_ms = struct.unpack_from("<dd", data, 24)
    point_count, point_attr_flags, vertex_count, index_count = struct.unpack_from(
        "<IIii", data, 40
    )

    offset = 56
    error_bytes, offset = _read_blob(data, offset)
    json_bytes, offset = _read_blob(data, offset)
    mesh, offset = _read_blob(data, offset)
    points, offset = _read_blob(data, offset)
    geometry, offset = _read_blob(data, offset)
    heightfield, offset = _read_blob(data, offset)
    perf_bytes, offset = _read_blob(data, offset)

    return {
        "code": code,
        "kind": kind,
        "nodesExecuted": nodes_executed,
        "nodesSkipped": nodes_skipped,
        "graphExecuteMs": graph_execute_ms,
        "binaryWriteMs": binary_write_ms,
        "pointCount": point_count,
        "pointAttrFlags": point_attr_flags,
        "vertexCount": vertex_count,
        "indexCount": index_count,
        "error": error_bytes.decode("utf-8", errors="replace"),
        "json": json_bytes.decode("utf-8", errors="replace"),
        "meshBytes": len(mesh),
        "pointsBytes": len(points),
        "geometryBytes": len(geometry),
        "heightfieldBytes": len(heightfield),
        "perf": perf_bytes.decode("utf-8", errors="replace"),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pcgr", type=Path, help="PCGR binary file from /v1/cook")
    parser.add_argument("--json", action="store_true", help="Emit JSON summary")
    args = parser.parse_args(argv)

    if not args.pcgr.is_file():
        print(f"ERROR: file not found: {args.pcgr}", file=sys.stderr)
        return 1

    try:
        result = parse_cook_result(args.pcgr.read_bytes())
    except (ValueError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"code: {result['code']}")
        print(f"kind: {result['kind']}")
        print(f"nodesExecuted: {result['nodesExecuted']}")
        print(f"vertices: {result['vertexCount']}  indices: {result['indexCount']}")
        if result["error"]:
            print(f"error: {result['error']}")
        if result["json"]:
            print(f"json: {result['json'][:500]}")
    return 0 if result["code"] == 0 and not result["error"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
