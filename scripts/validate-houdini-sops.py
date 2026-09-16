#!/usr/bin/env python3
"""Cook every Houdini-backed manifest node with a minimal compatible input graph."""

from __future__ import annotations

import argparse
import json
import struct
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULT_MAGIC = 0x52474350
RESULT_HEADER_SIZE = 56


def manifest_defaults(node: dict) -> dict:
    return {key: prop.get("default") for key, prop in node.get("properties", {}).items()}


def graph_node(node_id: str, node_type: str, data: dict) -> dict:
    return {
        "id": node_id,
        "type": node_type,
        "position": {"x": 0, "y": 0},
        "data": data,
    }


def source_for_pin(pin_type: str) -> tuple[str, dict]:
    if pin_type == "SpatialPoint":
        return "PointGenerate", {
            "npts": 8,
            "numberOfPoints": 8,
            "count": 8,
            "size": [1.0, 1.0, 1.0],
        }
    if pin_type == "SpatialSpline":
        return "CircleSpline", {"radius": [1.0, 1.0, 1.0], "divisions": 8}
    return "Sphere", {
        "radius": [1.0, 1.0, 1.0],
        "rows": 6,
        "columns": 8,
        "uniformScale": 1.0,
    }


def minimal_graph(node: dict) -> dict:
    nodes = [graph_node("subject", node["type"], manifest_defaults(node))]
    edges = []
    for index, pin in enumerate(node.get("inputs", [])):
        source_id = f"source-{index}"
        source_type, source_data = source_for_pin(pin["pinType"])
        nodes.append(graph_node(source_id, source_type, source_data))
        edges.append({
            "id": f"input-{index}",
            "source": source_id,
            "target": "subject",
            "sourceHandle": "out",
            "targetHandle": pin["id"],
        })
    nodes.append(graph_node("output", "Output", {}))
    edges.append({
        "id": "result",
        "source": "subject",
        "target": "output",
        "sourceHandle": node["outputs"][0]["id"],
        "targetHandle": "in",
    })
    return {"version": "1.0", "nodes": nodes, "edges": edges}


def multipart(graph: dict) -> tuple[bytes, str]:
    boundary = f"----pcg-houdini-{uuid.uuid4().hex}"

    def part(name: str, filename: str, payload: bytes) -> bytes:
        return (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
            "Content-Type: application/json\r\n\r\n"
        ).encode() + payload + b"\r\n"

    body = part("meta", "meta.json", b'{"seed":42,"api_version":1}')
    body += part("graph", "graph.json", json.dumps(graph, separators=(",", ":")).encode())
    body += f"--{boundary}--\r\n".encode()
    return body, boundary


def cook(base: str, graph: dict) -> tuple[int, str]:
    body, boundary = multipart(graph)
    request = urllib.request.Request(
        f"{base.rstrip('/')}/v1/cook",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        payload = response.read()
    if len(payload) < RESULT_HEADER_SIZE:
        return -1, "truncated cook response"
    magic, version, code, _kind = struct.unpack_from("<IIiI", payload, 0)
    if magic != RESULT_MAGIC or version != 1:
        return -1, "invalid cook response header"
    offset = RESULT_HEADER_SIZE
    error_size = struct.unpack_from("<I", payload, offset)[0]
    offset += 4
    error = payload[offset : offset + error_size].decode("utf-8", errors="replace")
    return code, error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:17890")
    args = parser.parse_args()

    manifest = json.loads((ROOT / "schema/node-manifest.json").read_text())
    nodes = [node for node in manifest["nodes"] if node.get("houdiniInternalNames")]
    failures = []
    for node in nodes:
        try:
            code, error = cook(args.base, minimal_graph(node))
        except Exception as exc:  # network/transport failures belong in the report
            failures.append((node["type"], str(exc)))
            continue
        if code != 0:
            failures.append((node["type"], error or f"cook code {code}"))

    if failures:
        for node_type, error in failures:
            print(f"FAIL {node_type}: {error}")
        print(f"{len(failures)} of {len(nodes)} Houdini-backed nodes failed minimal cook.")
        return 1

    print(f"All {len(nodes)} Houdini-backed node types passed minimal server cook.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
