#!/usr/bin/env python3
"""Archive the reference image to durable local disk and bind it to the plan.

The conversation attachment / URL is NOT durable memory: context compaction can
drop pasted images and URLs can rot. Run this right after new_authoring_plan.py
so every later stage re-reads the same archived file.

Sources: local path (copied), http(s) URL (downloaded), data: URI (decoded).
With --plan, rewrites sourceImage to the archived path and records the original
under referenceArchive.originalSource.
"""

from __future__ import annotations

import argparse
import base64
import json
import re
import shutil
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import load_json, write_json  # noqa: E402

_MIME_EXT = {
    "image/png": ".png",
    "image/jpeg": ".jpg",
    "image/webp": ".webp",
    "image/gif": ".gif",
    "image/bmp": ".bmp",
}
_KNOWN_EXT = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"}


def slugify(text: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return slug or "reference"


def ext_from_source(source: str) -> str:
    if source.startswith("data:"):
        mime = source[5:].split(";", 1)[0].strip().lower()
        return _MIME_EXT.get(mime, ".png")
    path_part = source.split("?", 1)[0].split("#", 1)[0]
    ext = Path(path_part).suffix.lower()
    return ext if ext in _KNOWN_EXT else ".png"


def read_source(source: str) -> bytes:
    if source.startswith("data:"):
        header, _, payload = source.partition(",")
        if ";base64" not in header:
            raise ValueError("data: URI must be base64-encoded")
        return base64.b64decode(payload)
    if "://" in source:
        request = urllib.request.Request(source, headers={"User-Agent": "pcg-archive/1.0"})
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.read()
    path = Path(source).expanduser()
    if not path.is_file():
        raise FileNotFoundError(f"reference image not found: {path}")
    return path.read_bytes()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, help="Local path, http(s) URL, or data: URI")
    parser.add_argument("--plan", type=Path, help="*-plan.json to bind the archived path into")
    parser.add_argument("--out-dir", type=Path, help="Archive directory (default: plan dir or cwd)")
    parser.add_argument("--slug", default="", help="Override ref_<slug> name (default: plan targetName)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    plan_path = args.plan.expanduser().resolve() if args.plan else None
    plan = load_json(plan_path) if plan_path else None

    slug = args.slug.strip()
    if not slug and plan:
        slug = slugify(str(plan.get("targetName") or ""))
    if not slug:
        slug = slugify(Path(args.image.split("?", 1)[0]).stem)

    out_dir = args.out_dir.expanduser().resolve() if args.out_dir else (
        plan_path.parent if plan_path else Path.cwd()
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    payload_bytes = read_source(args.image)
    if not payload_bytes:
        raise ValueError("archived reference would be empty")

    dest = out_dir / f"ref_{slug}{ext_from_source(args.image)}"
    dest.write_bytes(payload_bytes)

    if plan is not None and plan_path is not None:
        plan["referenceArchive"] = {
            "archivedPath": str(dest),
            "originalSource": args.image if args.image != str(dest) else plan.get("sourceImage", ""),
            "archivedAt": datetime.now(timezone.utc).isoformat(),
        }
        plan["sourceImage"] = str(dest)
        write_json(plan_path, plan)

    result = {"archivedPath": str(dest), "bytes": len(payload_bytes), "plan": str(plan_path) if plan_path else None}
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        print(dest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
