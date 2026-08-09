#!/usr/bin/env python3
"""Validate the Web editor's bundled CC0 HDRI catalog and binary assets."""

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "web" / "pcg-editor" / "src" / "preview" / "builtinEnvironments.json"
ASSET_DIR = ROOT / "web" / "pcg-editor" / "public" / "environments" / "polyhaven"


def main() -> int:
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    environments = catalog.get("environments", [])
    if catalog.get("license") != "CC0-1.0":
        raise AssertionError("Built-in environment catalog must use CC0-1.0 assets")
    if len(environments) < 6:
        raise AssertionError(f"Expected at least 6 built-in environments, got {len(environments)}")

    ids: set[str] = set()
    files: set[str] = set()
    total_size = 0
    for environment in environments:
        environment_id = environment["id"]
        filename = environment["file"]
        preview_filename = environment["preview"]
        if environment_id in ids or filename in files:
            raise AssertionError(f"Duplicate built-in environment: {environment_id} / {filename}")
        ids.add(environment_id)
        files.add(filename)

        path = ASSET_DIR / filename
        payload = path.read_bytes()
        if not payload.startswith(b"#?RADIANCE") and not payload.startswith(b"#?RGBE"):
            raise AssertionError(f"Not a Radiance HDR file: {path}")
        if len(payload) != environment["size"]:
            raise AssertionError(f"Size mismatch for {filename}: {len(payload)} != {environment['size']}")
        digest = hashlib.md5(payload, usedforsecurity=False).hexdigest()
        if digest != environment["md5"]:
            raise AssertionError(f"MD5 mismatch for {filename}: {digest}")
        total_size += len(payload)

        preview_path = ASSET_DIR / preview_filename
        preview_payload = preview_path.read_bytes()
        if not preview_payload.startswith(b"\x89PNG\r\n\x1a\n"):
            raise AssertionError(f"Not a PNG preview: {preview_path}")

    unexpected = {path.name for path in ASSET_DIR.glob("*.hdr")} - files
    if unexpected:
        raise AssertionError(f"Uncatalogued built-in HDRIs: {sorted(unexpected)}")

    print(f"Built-in HDRI catalog OK: {len(environments)} CC0 assets, {total_size / 1024 / 1024:.1f} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
