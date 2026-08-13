#!/usr/bin/env python3
"""Build the PCG builtin subgraph library index and sync it to the editors.

Source of truth: library/<category>/<name>.pcgsubgraph + <name>.libmeta.json.

Outputs:
  library/library-index.json
  web/pcg-editor/public/library/                       (web editor static copy)
  Unity/Assets/PcgPlugin/Editor/BuiltinLibrary/        (unity package copy + .meta)

Modes:
  default  — recompute contentHash, rewrite stale asset files, emit index, sync
  --check  — verify only (CI): fail on stale contentHash / stale index / stale copies
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "library"
INDEX_PATH = LIBRARY / "library-index.json"
WEB_TARGET = ROOT / "web" / "pcg-editor" / "public" / "library"
UNITY_TARGET = ROOT / "Unity" / "Assets" / "PcgPlugin" / "Editor" / "BuiltinLibrary"
NODE_MANIFEST_PATH = ROOT / "schema" / "node-manifest.json"

PCGSUBGRAPH_IMPORTER_SCRIPT_GUID = "B3IXtS+kWy270dxP3mBV+OT4ZRmBu7fdC0+mXTlo8FxjKOx63KyRr/7hzPBo"

PIN_TYPES = {
    "Any", "Param", "SpatialPoint", "SpatialSpline", "SpatialSurface",
    "SpatialMesh", "SpatialGeometry", "Texture", "HeightField", "Material",
}


class LibraryError(ValueError):
    pass


# ── Canonical contentHash (mirrors PcgSubgraphAssetContentHash.Compute) ─────

def _f32(value: float) -> float:
    return struct.unpack("f", struct.pack("f", float(value)))[0]


def _fmt_f32(value: float) -> str:
    v = _f32(value)
    if v == 0:
        return "0"
    if v == int(v) and abs(v) < 1e15:
        return str(int(v))
    for precision in range(1, 10):
        s = f"{v:.{precision}g}"
        if _f32(float(s)) == v:
            break
    else:
        s = f"{v:.9g}"
    if "e" in s or "E" in s:
        mantissa, _, exponent = s.lower().partition("e")
        s = f"{mantissa}E{int(exponent):+03d}"
    return s


def _fmt_f64(value: float) -> str:
    v = float(value)
    if v == int(v) and abs(v) < 1e15:
        return str(int(v))
    s = repr(v)
    if "e" in s or "E" in s:
        raise LibraryError(f"exponent-form numbers are not supported in library assets: {value!r}")
    return s


def _json_string(value: str) -> str:
    out = ['"']
    for ch in value:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def _str_of(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return _fmt_f64(value)
    return str(value)


def _is_int_text(text: str) -> bool:
    return re.fullmatch(r"[+-]?\d+", text) is not None


def _is_float_text(text: str) -> bool:
    return re.fullmatch(r"[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?", text) is not None


def _inferred_value(text: str) -> str:
    if _is_int_text(text) or _is_float_text(text):
        return text
    if text in ("true", "false"):
        return text
    return _json_string(text)


def _append_json_property(key: str, value, prop_type: str) -> str:
    if prop_type == "integer":
        return f"{_json_string(key)}: {int(float(_str_of(value)))}"
    if prop_type == "number":
        return f"{_json_string(key)}: {_fmt_f32(float(_str_of(value)))}"
    if prop_type == "boolean":
        raw = _str_of(value)
        truthy = raw.lower() == "true" or (raw.lstrip("-").isdigit() and float(raw) != 0)
        return f"{_json_string(key)}: {'true' if truthy else 'false'}"
    if prop_type == "vector3":
        raise LibraryError("vector3 properties are not supported in library assets yet")
    return f"{_json_string(key)}: {_json_string(_str_of(value))}"


def _canonical_data(node_type: str, data: dict, manifest_nodes: dict) -> str:
    definition = manifest_nodes.get(node_type)
    if definition is None:
        parts = [f"{_json_string(k)}: {_inferred_value(_str_of(v))}" for k, v in data.items()]
        return "{" + ", ".join(parts) + "}"

    parts = []
    manifest_props = definition.get("properties", {})
    for key, prop in manifest_props.items():
        value = data.get(key, prop.get("default"))
        parts.append(_append_json_property(key, value, prop.get("type", "string")))
    for key, value in data.items():
        if key in manifest_props:
            continue
        parts.append(f"{_json_string(key)}: {_inferred_value(_str_of(value))}")
    return "{" + ", ".join(parts) + "}"


def _canonical_ports(key: str, ports: list) -> str:
    items = []
    for port in ports:
        text = (
            '{"id": ' + _json_string(_str_of(port.get("id")))
            + ', "name": ' + _json_string(_str_of(port.get("name")))
            + ', "pinType": ' + _json_string(_str_of(port.get("pinType")) or "Any")
        )
        if port.get("anchorPlaced"):
            text += (
                ', "anchorPlaced": true'
                + ', "anchorX": ' + _fmt_f32(float(port.get("anchorX", 0)))
                + ', "anchorY": ' + _fmt_f32(float(port.get("anchorY", 0)))
            )
        items.append(text + "}")
    return f'"{key}": [' + ",".join(items) + "]"


def _canonical_parameters(parameters: list) -> str:
    items = []
    for param in parameters:
        default_text = _str_of(param.get("default"))
        param_type = _str_of(param.get("type")) or "number"
        if param_type in ("integer", "number", "boolean"):
            default_json = default_text
        else:
            default_json = _json_string(default_text)
        items.append(
            '{"id": ' + _json_string(_str_of(param.get("id")))
            + ', "name": ' + _json_string(_str_of(param.get("name")))
            + ', "type": ' + _json_string(param_type)
            + ', "default": ' + default_json
            + ', "exposed": ' + ("true" if param.get("exposed", True) else "false")
            + ', "targetNode": ' + _json_string(_str_of(param.get("targetNode")))
            + ', "targetProperty": ' + _json_string(_str_of(param.get("targetProperty")))
            + ', "hasRange": ' + ("true" if param.get("hasRange") else "false")
            + ', "min": ' + _fmt_f32(float(param.get("min", 0)))
            + ', "max": ' + _fmt_f32(float(param.get("max", 1)))
            + "}"
        )
    return '"parameters": [' + ",".join(items) + "]"


def _canonical_nodes(nodes: list, manifest_nodes: dict) -> str:
    items = []
    for node in nodes:
        position = node.get("position") or {}
        items.append(
            '{"id": ' + _json_string(_str_of(node.get("id")))
            + ',"type": ' + _json_string(_str_of(node.get("type")))
            + ',"position": { "x": ' + _fmt_f32(float(position.get("x", 0)))
            + ', "y": ' + _fmt_f32(float(position.get("y", 0)))
            + ' },"data": ' + _canonical_data(_str_of(node.get("type")), node.get("data") or {}, manifest_nodes)
            + "}"
        )
    return '"nodes": [' + ",".join(items) + "]"


def _canonical_edges(edges: list) -> str:
    items = []
    for edge in edges:
        text = (
            '{"id": ' + _json_string(_str_of(edge.get("id")))
            + ', "source": ' + _json_string(_str_of(edge.get("source")))
            + ', "target": ' + _json_string(_str_of(edge.get("target")))
        )
        for key in ("sourceHandle", "targetHandle", "sourcePinType", "targetPinType"):
            value = _str_of(edge.get(key))
            if value:
                text += f', "{key}": ' + _json_string(value)
        items.append(text + "}")
    return '"edges": [' + ",".join(items) + "]"


def compute_content_hash(asset: dict, manifest_nodes: dict) -> str:
    if asset.get("subgraphs"):
        raise LibraryError("nested subgraphs are not supported in library assets yet")
    parts = [
        '"version": ' + _json_string(_str_of(asset.get("version")) or "1.0"),
        '"name": ' + _json_string(_str_of(asset.get("name"))),
        _canonical_ports("inputs", asset.get("inputs") or []),
        _canonical_ports("outputs", asset.get("outputs") or []),
    ]
    if _str_of(asset.get("version")) == "2.0":
        parts.append(_canonical_parameters(asset.get("parameters") or []))
    parts.append(_canonical_nodes(asset.get("nodes") or [], manifest_nodes))
    parts.append(_canonical_edges(asset.get("edges") or []))
    parts.append('"subgraphs": []')
    canonical = "{" + ",".join(parts) + "}"
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


# ── Validation ───────────────────────────────────────────────────────────────

def validate_asset(path: Path, asset: dict, manifest_nodes: dict) -> None:
    def fail(message: str) -> None:
        raise LibraryError(f"{path.relative_to(ROOT)}: {message}")

    if asset.get("version") != "2.0":
        fail('library assets must use version "2.0"')
    if not _str_of(asset.get("name")):
        fail("name is required")
    if not asset.get("outputs"):
        fail("at least one output port is required")

    for key in ("inputs", "outputs"):
        seen = set()
        for port in asset.get(key) or []:
            port_id = _str_of(port.get("id"))
            if not port_id:
                fail(f"{key}: port id is required")
            if port_id in seen:
                fail(f"{key}: duplicate port id '{port_id}'")
            seen.add(port_id)
            if _str_of(port.get("pinType")) not in PIN_TYPES:
                fail(f"{key}.{port_id}: unknown pinType '{port.get('pinType')}'")

    nodes = asset.get("nodes") or []
    node_ids = [ _str_of(n.get("id")) for n in nodes ]
    if len(set(node_ids)) != len(node_ids):
        fail("duplicate node id")
    for node in nodes:
        node_type = _str_of(node.get("type"))
        if node_type not in manifest_nodes and node_type not in ("SubgraphInput", "SubgraphOutput"):
            fail(f"node '{node.get('id')}': unknown type '{node_type}'")
    if asset.get("inputs") and "SubgraphInput" not in [_str_of(n.get("type")) for n in nodes]:
        fail("declared inputs require a SubgraphInput node")
    if "SubgraphOutput" not in [_str_of(n.get("type")) for n in nodes]:
        fail("a SubgraphOutput node is required")

    for edge in asset.get("edges") or []:
        for key in ("source", "target"):
            if _str_of(edge.get(key)) not in node_ids:
                fail(f"edge '{edge.get('id')}': {key} node '{edge.get(key)}' not found")

    for param in asset.get("parameters") or []:
        target_node = _str_of(param.get("targetNode"))
        if target_node not in node_ids:
            fail(f"parameter '{param.get('id')}': targetNode '{target_node}' not found")
        target_type = next((_str_of(n.get("type")) for n in nodes if _str_of(n.get("id")) == target_node), "")
        props = (manifest_nodes.get(target_type) or {}).get("properties", {})
        target_property = _str_of(param.get("targetProperty"))
        if props and target_property not in props:
            fail(f"parameter '{param.get('id')}': '{target_type}.{target_property}' not in manifest")
        if param.get("type") not in ("integer", "number"):
            fail(f"parameter '{param.get('id')}': only integer/number parameters are supported in library assets")


def validate_sidecar(path: Path, sidecar: dict) -> None:
    for key in ("id", "displayName", "category", "assetVersion", "keywords", "description"):
        if key not in sidecar:
            raise LibraryError(f"{path.relative_to(ROOT)}: missing '{key}'")
    expected_id = f"pcg.lib:{path.parent.name}:{path.name.removesuffix('.libmeta.json')}"
    if sidecar["id"] != expected_id:
        raise LibraryError(f"{path.relative_to(ROOT)}: id '{sidecar['id']}' must be '{expected_id}'")
    if not sidecar["keywords"] or not all(isinstance(k, str) for k in sidecar["keywords"]):
        raise LibraryError(f"{path.relative_to(ROOT)}: keywords must be a non-empty string array")


# ── Sync ─────────────────────────────────────────────────────────────────────

def _meta_guid(relative_path: str) -> str:
    return uuid.uuid5(uuid.NAMESPACE_URL, f"pcg-builtin-library:{relative_path}").hex


def _write_meta(path: Path, guid: str, body: str) -> None:
    meta_path = path.with_name(path.name + ".meta")
    content = f"fileFormatVersion: 2\nguid: {guid}\n{body}"
    if not meta_path.exists() or meta_path.read_text(encoding="utf-8") != content:
        meta_path.write_text(content, encoding="utf-8")


def _folder_meta_body() -> str:
    return "folderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n"


def _asset_meta_body(path: Path) -> str:
    if path.suffix == ".pcgsubgraph":
        return (
            "ScriptedImporter:\n  internalIDToNameTable: []\n  externalObjects: {}\n"
            "  serializedVersion: 2\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n"
            f"  script: {{fileID: 11500000, guid: {PCGSUBGRAPH_IMPORTER_SCRIPT_GUID}, type: 3}}\n"
        )
    if path.suffix in (".json", ".md"):
        return "TextScriptImporter:\n  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n"
    return "DefaultImporter:\n  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n"


def sync_tree(source_root: Path, target_root: Path, with_meta: bool, check: bool, errors: list) -> None:
    source_files = sorted(p for p in source_root.rglob("*") if p.is_file() and not p.name.endswith(".libmeta.json"))
    expected_targets = set()
    for source in source_files:
        relative = source.relative_to(source_root)
        target = target_root / relative
        expected_targets.add(target)
        if check:
            if not target.exists() or target.read_bytes() != source.read_bytes():
                errors.append(f"stale sync copy: {target.relative_to(ROOT)}")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists() or target.read_bytes() != source.read_bytes():
                shutil.copy2(source, target)
        if with_meta:
            meta_rel = relative.as_posix()
            directory = target.parent
            while directory != target_root and target_root in directory.parents:
                folder_rel = directory.relative_to(target_root).as_posix()
                _write_meta(directory, _meta_guid(f"dir:{folder_rel}"), _folder_meta_body())
                directory = directory.parent
            _write_meta(target, _meta_guid(meta_rel), _asset_meta_body(target))
    if target_root.exists():
        for stale in sorted(target_root.rglob("*")):
            if stale.is_file() and stale.suffix != ".meta" and stale not in expected_targets:
                if check:
                    errors.append(f"unexpected synced file: {stale.relative_to(ROOT)}")
                else:
                    stale.unlink()
                    stale_meta = stale.with_name(stale.name + ".meta")
                    if stale_meta.exists():
                        stale_meta.unlink()
    if with_meta and not check:
        _write_meta(target_root, _meta_guid(f"dir:{target_root.name}"), _folder_meta_body())


# ── Main ─────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify only; do not write")
    args = parser.parse_args()

    manifest = json.loads(NODE_MANIFEST_PATH.read_text(encoding="utf-8"))
    manifest_nodes = {node["type"]: node for node in manifest["nodes"]}

    assets = sorted(LIBRARY.rglob("*.pcgsubgraph"))
    if not assets:
        print("No library assets found under library/", file=sys.stderr)
        return 1

    errors: list[str] = []
    items = []
    for asset_path in assets:
        relative = asset_path.relative_to(LIBRARY).as_posix()
        sidecar_path = asset_path.with_suffix(".libmeta.json")
        try:
            asset = json.loads(asset_path.read_text(encoding="utf-8"))
            sidecar = json.loads(sidecar_path.read_text(encoding="utf-8")) if sidecar_path.exists() else None
            if sidecar is None:
                raise LibraryError(f"{relative}: missing sidecar {sidecar_path.name}")
            validate_asset(asset_path, asset, manifest_nodes)
            validate_sidecar(sidecar_path, sidecar)
            content_hash = compute_content_hash(asset, manifest_nodes)
        except LibraryError as error:
            errors.append(str(error))
            continue

        if asset.get("contentHash") != content_hash:
            message = f"{relative}: contentHash {asset.get('contentHash', '')[:8]}… -> {content_hash[:8]}…"
            if args.check:
                errors.append(f"stale contentHash: {relative}")
            else:
                asset["contentHash"] = content_hash
                asset_path.write_text(json.dumps(asset, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
                print(f"updated {message}")

        thumbnail = asset_path.with_suffix(".png")
        doc = asset_path.with_suffix(".md")
        item = {
            "id": sidecar["id"],
            "displayName": sidecar["displayName"],
            "category": sidecar["category"],
            "file": relative,
            "assetVersion": sidecar["assetVersion"],
            "contentHash": content_hash,
            "keywords": sidecar["keywords"],
            "description": sidecar["description"],
            "inputs": asset["inputs"],
            "outputs": asset["outputs"],
            "parameters": [
                {
                    "id": p["id"],
                    "name": p.get("name", p["id"]),
                    "type": p.get("type", "number"),
                    "default": p.get("default"),
                    "hasRange": bool(p.get("hasRange")),
                    "min": p.get("min", 0),
                    "max": p.get("max", 1),
                }
                for p in asset.get("parameters") or []
            ],
        }
        if thumbnail.exists():
            item["thumbnail"] = thumbnail.relative_to(LIBRARY).as_posix()
        if doc.exists():
            item["doc"] = doc.relative_to(LIBRARY).as_posix()
        if sidecar.get("minCoreVersion"):
            item["minCoreVersion"] = sidecar["minCoreVersion"]
        items.append(item)

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    index = {"version": 1, "items": items}
    index_text = json.dumps(index, indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not INDEX_PATH.exists() or INDEX_PATH.read_text(encoding="utf-8") != index_text:
            print("error: stale library-index.json (run scripts/build_library_index.py)", file=sys.stderr)
            return 1
        sync_errors: list[str] = []
        sync_tree(LIBRARY, WEB_TARGET, with_meta=False, check=True, errors=sync_errors)
        sync_tree(LIBRARY, UNITY_TARGET, with_meta=True, check=True, errors=sync_errors)
        if sync_errors:
            for error in sync_errors:
                print(f"error: {error}", file=sys.stderr)
            return 1
        print(f"Builtin library OK: {len(items)} asset(s)")
        return 0

    INDEX_PATH.write_text(index_text, encoding="utf-8")
    sync_errors = []
    sync_tree(LIBRARY, WEB_TARGET, with_meta=False, check=False, errors=sync_errors)
    sync_tree(LIBRARY, UNITY_TARGET, with_meta=True, check=False, errors=sync_errors)
    print(f"Built library-index.json: {len(items)} asset(s); synced to web public/ and Unity BuiltinLibrary/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
