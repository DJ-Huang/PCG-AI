#!/usr/bin/env python3
"""Validate that C++ element registrations and node-manifest.json are in sync.

Extracts type names from:
  - pcg-core/src/elements/*.cpp  (map.emplace("TypeName", ...))
  - schema/node-manifest.json   (nodes[].type)

Also validates optional Inspector section/layout metadata when present.

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
    """Extract native types; Editor-only ROP nodes intentionally have no C++ element."""
    if not MANIFEST_PATH.is_file():
        print(f"ERROR: node-manifest.json not found: {MANIFEST_PATH}")
        sys.exit(1)

    with open(MANIFEST_PATH, encoding="utf-8") as f:
        manifest = json.load(f)

    return {
        node["type"]
        for node in manifest.get("nodes", [])
        if not node.get("editorOnly", False)
    }


def load_manifest():
    with open(MANIFEST_PATH, encoding="utf-8") as f:
        return json.load(f)


def validate_visible_clause(node_type, key, clause, props, errors, label):
    """Validate one visibleWhen clause: property + (equals or oneOf)."""
    if not isinstance(clause, dict):
        errors.append(f"{node_type}.{key}: {label} must be object")
        return
    driver = clause.get("property")
    if not driver:
        errors.append(f"{node_type}.{key}: {label} missing property")
    elif driver not in props:
        errors.append(
            f"{node_type}.{key}: {label} property '{driver}' not found"
        )
    has_equals = "equals" in clause
    one_of = clause.get("oneOf")
    has_one_of = isinstance(one_of, list) and len(one_of) > 0
    if not has_equals and not has_one_of:
        errors.append(
            f"{node_type}.{key}: {label} requires equals or non-empty oneOf"
        )


def validate_visible_when(node_type, key, visible, props, errors):
    """Match Unity PcgNodeManifest: classic property/equals/oneOf or any[]."""
    any_clauses = visible.get("any")
    if any_clauses is not None:
        if not isinstance(any_clauses, list) or len(any_clauses) == 0:
            errors.append(f"{node_type}.{key}: visibleWhen.any must be non-empty list")
            return
        for i, clause in enumerate(any_clauses):
            validate_visible_clause(
                node_type, key, clause, props, errors, f"visibleWhen.any[{i}]"
            )
        return

    validate_visible_clause(node_type, key, visible, props, errors, "visibleWhen")


def validate_inspector_layout(manifest):
    """Validate optional inspectorSections / property layout metadata."""
    errors = []
    for node in manifest.get("nodes", []):
        node_type = node.get("type", "<unknown>")
        sections = node.get("inspectorSections")
        props = node.get("properties") or {}
        if not sections:
            for key, prop in props.items():
                if not isinstance(prop, dict):
                    continue
                if "section" in prop:
                    errors.append(
                        f"{node_type}.{key}: has section but node has no inspectorSections"
                    )
            continue

        section_ids = []
        for section in sections:
            if not isinstance(section, dict):
                errors.append(f"{node_type}: inspectorSections entry must be object")
                continue
            sid = section.get("id")
            if not sid:
                errors.append(f"{node_type}: inspectorSections entry missing id")
                continue
            if sid in section_ids:
                errors.append(f"{node_type}: duplicate section id '{sid}'")
            section_ids.append(sid)

        known = set(section_ids)
        orders_by_section = {}

        for key, prop in props.items():
            if not isinstance(prop, dict):
                errors.append(f"{node_type}.{key}: property must be object")
                continue

            section = prop.get("section")
            if section is not None and section != "":
                if section not in known:
                    errors.append(f"{node_type}.{key}: unknown section '{section}'")
                if "order" in prop:
                    orders_by_section.setdefault(section, []).append((key, prop["order"]))

            visible = prop.get("visibleWhen")
            if visible is not None:
                if not isinstance(visible, dict):
                    errors.append(f"{node_type}.{key}: visibleWhen must be object")
                else:
                    validate_visible_when(node_type, key, visible, props, errors)

            enabled = prop.get("enabledWhen")
            if enabled is not None:
                if not isinstance(enabled, dict):
                    errors.append(f"{node_type}.{key}: enabledWhen must be object")
                else:
                    driver = enabled.get("property")
                    if not driver:
                        errors.append(f"{node_type}.{key}: enabledWhen missing property")
                    elif driver not in props:
                        errors.append(
                            f"{node_type}.{key}: enabledWhen property '{driver}' not found"
                        )

            companion = prop.get("companionField")
            if companion is not None:
                if not isinstance(companion, str) or companion == "":
                    errors.append(f"{node_type}.{key}: companionField must be non-empty string")
                elif companion not in props:
                    errors.append(
                        f"{node_type}.{key}: companionField '{companion}' not found"
                    )

        for section, items in orders_by_section.items():
            seen = {}
            for key, order in items:
                if order in seen:
                    errors.append(
                        f"{node_type}: duplicate order {order} in section '{section}' "
                        f"({seen[order]} and {key})"
                    )
                seen[order] = key

    return errors


def main():
    cpp_types = extract_cpp_types()
    manifest_types = extract_manifest_types()
    manifest = load_manifest()

    cpp_only = cpp_types - manifest_types
    manifest_only = manifest_types - cpp_types

    print(f"C++ elements:     {len(cpp_types)}")
    print(f"Manifest nodes:   {len(manifest_types)}")
    print()

    failed = False

    if cpp_only:
        failed = True
        print("ERROR: Registered in C++ but missing from manifest:")
        for t in sorted(cpp_only):
            print(f"  - {t}")

    if manifest_only:
        failed = True
        print("ERROR: In manifest but not registered in C++:")
        for t in sorted(manifest_only):
            print(f"  - {t}")

    layout_errors = validate_inspector_layout(manifest)
    if layout_errors:
        failed = True
        print("ERROR: Inspector layout metadata:")
        for err in layout_errors:
            print(f"  - {err}")
    else:
        print("OK: Inspector layout metadata")

    if not failed:
        print(f"OK: C++ and manifest are in sync ({len(cpp_types)} nodes)")
        return 0

    print()
    print("FAILED: Manifest validation failed.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
