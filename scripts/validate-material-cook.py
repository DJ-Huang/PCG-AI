#!/usr/bin/env python3
"""Cook the Standard PBR example through pcg-server and verify its material contract."""

import argparse
import json
import struct
import urllib.request
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GRAPH = ROOT / "examples" / "material-standard-pbr.pcg"
RESULT_HEADER_SIZE = 56
RESULT_MAGIC = 0x52474350
MESH_MAGIC = 0x4D474350
MESH_FLAG_NORMALS = 0x1
MESH_FLAG_COLORS = 0x2
MESH_FLAG_UVS = 0x4
MESH_FLAG_MATERIALS = 0x8


def multipart(meta: bytes, graph: bytes) -> tuple[bytes, str]:
    boundary = f"pcg-material-{uuid.uuid4().hex}"
    chunks: list[bytes] = []
    for name, payload in (("meta", meta), ("graph", graph)):
        chunks.extend(
            (
                f"--{boundary}\r\n".encode(),
                f'Content-Disposition: form-data; name="{name}"\r\n'.encode(),
                b"Content-Type: application/json\r\n\r\n",
                payload,
                b"\r\n",
            )
        )
    chunks.append(f"--{boundary}--\r\n".encode())
    return b"".join(chunks), boundary


def read_blob(payload: bytes, offset: int) -> tuple[bytes, int]:
    if offset + 4 > len(payload):
        raise AssertionError("Cook response blob length is truncated")
    (size,) = struct.unpack_from("<I", payload, offset)
    offset += 4
    end = offset + size
    if end > len(payload):
        raise AssertionError("Cook response blob is truncated")
    return payload[offset:end], end


def parse_material_section(mesh: bytes) -> tuple[list[str], list[int]]:
    if len(mesh) < 24:
        raise AssertionError("PCGM payload is too short")
    magic, version, vertex_count, index_count, flags, section_size = struct.unpack_from("<IIIIII", mesh, 0)
    if magic != MESH_MAGIC:
        raise AssertionError(f"Unexpected PCGM magic: 0x{magic:08x}")
    if version != 3:
        raise AssertionError(f"Material graph must return PCGM v3, got v{version}")
    if not flags & MESH_FLAG_MATERIALS:
        raise AssertionError("PCGM material flag is missing")

    offset = 24 + vertex_count * 12 + index_count * 4
    if flags & MESH_FLAG_NORMALS:
        offset += vertex_count * 12
    if flags & MESH_FLAG_COLORS:
        offset += vertex_count * 16
    if flags & MESH_FLAG_UVS:
        offset += vertex_count * 8
    section_end = offset + section_size
    if section_size < 4 or section_end > len(mesh):
        raise AssertionError("PCGM material section is invalid")

    (slot_count,) = struct.unpack_from("<I", mesh, offset)
    offset += 4
    slots: list[str] = []
    for _ in range(slot_count):
        (name_size,) = struct.unpack_from("<I", mesh, offset)
        offset += 4
        slots.append(mesh[offset : offset + name_size].decode("utf-8"))
        offset += name_size

    triangle_count = index_count // 3
    expected_end = offset + triangle_count * 4
    if expected_end != section_end:
        raise AssertionError("PCGM per-triangle material table size is invalid")
    triangle_materials = list(struct.unpack_from(f"<{triangle_count}I", mesh, offset))
    return slots, triangle_materials


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:17890")
    parser.add_argument("--graph", type=Path, default=DEFAULT_GRAPH)
    args = parser.parse_args()

    graph = args.graph.read_bytes()
    body, boundary = multipart(json.dumps({"seed": 42, "api_version": 1}).encode(), graph)
    request = urllib.request.Request(
        f"{args.base.rstrip('/')}/v1/cook",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        payload = response.read()

    if len(payload) < RESULT_HEADER_SIZE:
        raise AssertionError("Cook response header is truncated")
    magic, version, code, kind = struct.unpack_from("<IIiI", payload, 0)
    if magic != RESULT_MAGIC or version != 1 or code != 0 or kind != 2:
        raise AssertionError(
            f"Unexpected cook result: magic=0x{magic:08x}, version={version}, code={code}, kind={kind}"
        )

    offset = RESULT_HEADER_SIZE
    error, offset = read_blob(payload, offset)
    result_json, offset = read_blob(payload, offset)
    mesh, _ = read_blob(payload, offset)
    if error:
        raise AssertionError(error.decode("utf-8", errors="replace"))

    result = json.loads(result_json)
    metadata = result.get("mesh_metadata", {})
    library = metadata.get("pbrMaterials", {})
    material = library.get("brushed_copper")
    if metadata.get("material") != "brushed_copper" or not isinstance(material, dict):
        raise AssertionError("Cook JSON did not preserve the assigned brushed_copper PBR material")
    if material.get("shaderId") != "pcg.standard-pbr":
        raise AssertionError("Cook JSON did not preserve the Standard PBR shader id")
    if material.get("metallic") != 0.9 or material.get("roughness") != 0.28:
        raise AssertionError("Cook JSON did not preserve the PBR scalar parameters")

    slots, triangle_materials = parse_material_section(mesh)
    if slots != ["brushed_copper"]:
        raise AssertionError(f"Unexpected PCGM material slots: {slots}")
    if not triangle_materials or any(slot != 0 for slot in triangle_materials):
        raise AssertionError("PCGM triangles are not all assigned to brushed_copper")

    print(
        "Material cook contract OK: "
        f"slot={slots[0]}, triangles={len(triangle_materials)}, metallic={material['metallic']}, "
        f"roughness={material['roughness']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
