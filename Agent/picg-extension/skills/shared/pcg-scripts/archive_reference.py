#!/usr/bin/env python3
"""Archive one reference or an atomic labelled reference set to durable disk.

The conversation attachment / URL is NOT durable memory: context compaction can
drop pasted images and URLs can rot. Run this right after new_authoring_plan.py
so every later stage re-reads the same archived file.

Sources: local path (copied), http(s) URL (downloaded), data: URI (decoded).
Use repeatable ``--view ROLE=SOURCE`` or ``--from-plan`` for front/side/top.
The v1 ``--image`` path remains supported and is mirrored to legacy fields.
"""

from __future__ import annotations

import argparse
import base64
import json
import re
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import (  # noqa: E402
    TRIVIEW_ROLES,
    VALID_REFERENCE_ROLES,
    load_json,
    reference_views,
    write_json,
)

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


def parse_view_spec(value: str) -> tuple[str, str]:
    role, separator, source = value.partition("=")
    role = role.strip()
    source = source.strip()
    if not separator or not role or not source:
        raise ValueError("--view must use ROLE=SOURCE (for example front=/path/front.png)")
    if role not in VALID_REFERENCE_ROLES:
        raise ValueError(f"unsupported reference role {role!r}; expected {sorted(VALID_REFERENCE_ROLES)}")
    return role, source


def upsert_plan_view(
    views: list[dict],
    *,
    role: str,
    source: str,
    archived_path: Path,
    archived_at: str,
) -> None:
    current = next(
        (
            item
            for item in views
            if isinstance(item, dict) and (item.get("id") == role or item.get("role") == role)
        ),
        None,
    )
    if current is None:
        current = {"id": role, "role": role, "required": True}
        views.append(current)
    current.update(
        {
            "id": role,
            "role": role,
            "originalSource": source,
            "archivedPath": str(archived_path),
            "archivedAt": archived_at,
            "projection": "orthographic" if role in TRIVIEW_ROLES else current.get("projection", "unknown"),
            "required": current.get("required", True),
        }
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", help="Single local path, http(s) URL, or data: URI")
    parser.add_argument(
        "--role",
        default="primary",
        choices=sorted(VALID_REFERENCE_ROLES),
        help="Role for --image (default: primary)",
    )
    parser.add_argument(
        "--view",
        action="append",
        default=[],
        metavar="ROLE=SOURCE",
        help="Repeat for a labelled reference set, e.g. front=... side=... top=...",
    )
    parser.add_argument(
        "--from-plan",
        action="store_true",
        help="Archive every referenceSet.views[].originalSource already recorded in --plan",
    )
    parser.add_argument(
        "--require-triview",
        action="store_true",
        help="Fail unless front, side, and top are all present in this archive batch",
    )
    parser.add_argument("--plan", type=Path, help="*-plan.json to bind the archived path into")
    parser.add_argument("--out-dir", type=Path, help="Archive directory (default: plan dir or cwd)")
    parser.add_argument("--slug", default="", help="Override ref_<slug> name (default: plan targetName)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        return run(args)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 2


def run(args: argparse.Namespace) -> int:
    plan_path = args.plan.expanduser().resolve() if args.plan else None
    plan = load_json(plan_path) if plan_path else None

    sources: dict[str, str] = {}
    if args.from_plan:
        if plan is None:
            raise ValueError("--from-plan requires --plan")
        for item in reference_views(plan):
            role = str(item.get("role") or item.get("id") or "").strip()
            source = str(item.get("originalSource") or "").strip()
            if role and source:
                sources[role] = source
    if args.image:
        sources[args.role] = args.image
    for raw in args.view:
        role, source = parse_view_spec(raw)
        sources[role] = source
    if not sources:
        raise ValueError("provide --image, one or more --view ROLE=SOURCE values, or --from-plan")
    if args.require_triview:
        missing = [role for role in TRIVIEW_ROLES if role not in sources]
        if missing:
            raise ValueError(f"three-view archive missing: {', '.join(missing)}")

    slug = args.slug.strip()
    if not slug and plan:
        slug = slugify(str(plan.get("targetName") or ""))
    if not slug:
        first_source = next(iter(sources.values()))
        slug = slugify(Path(first_source.split("?", 1)[0]).stem)

    out_dir = args.out_dir.expanduser().resolve() if args.out_dir else (
        plan_path.parent if plan_path else Path.cwd()
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    payloads: dict[str, tuple[str, bytes]] = {}
    for role, source in sources.items():
        payload_bytes = read_source(source)
        if not payload_bytes:
            raise ValueError(f"archived reference {role} would be empty")
        payloads[role] = (source, payload_bytes)

    multi = len(payloads) > 1 or next(iter(payloads)) != "primary"
    destinations: dict[str, Path] = {}
    for role, (source, payload_bytes) in payloads.items():
        suffix = f"_{slug}_{role}" if multi else f"_{slug}"
        dest = out_dir / f"ref{suffix}{ext_from_source(source)}"
        dest.write_bytes(payload_bytes)
        destinations[role] = dest

    if plan is not None and plan_path is not None:
        archived_at = datetime.now(timezone.utc).isoformat()
        reference_set = plan.get("referenceSet")
        if not isinstance(reference_set, dict):
            reference_set = {"mode": "single", "views": []}
            plan["referenceSet"] = reference_set
        views = reference_set.get("views")
        if not isinstance(views, list):
            views = []
            reference_set["views"] = views
        for role, (source, _) in payloads.items():
            upsert_plan_view(
                views,
                role=role,
                source=source,
                archived_path=destinations[role],
                archived_at=archived_at,
            )
        roles = {str(item.get("role") or item.get("id") or "") for item in views if isinstance(item, dict)}
        reference_set["mode"] = (
            "orthographic-triplet"
            if set(TRIVIEW_ROLES).issubset(roles)
            else ("single" if len(roles) == 1 else "multi-view")
        )
        alias_role = "primary" if "primary" in destinations else (
            "front" if "front" in destinations else next(iter(destinations))
        )
        alias_source, alias_bytes = payloads[alias_role]
        alias_dest = destinations[alias_role]
        plan["referenceArchive"] = {
            "archivedPath": str(alias_dest),
            "originalSource": alias_source,
            "archivedAt": archived_at,
            "legacyAliasFor": alias_role,
        }
        plan["sourceImage"] = str(alias_dest)
        contract = plan.get("qualityContract")
        if isinstance(contract, dict):
            contract["requiredReferenceViews"] = [
                str(item.get("id") or item.get("role"))
                for item in views
                if isinstance(item, dict) and item.get("required") is not False
            ]
        write_json(plan_path, plan)

    archives = [
        {
            "viewId": role,
            "archivedPath": str(destinations[role]),
            "bytes": len(payloads[role][1]),
        }
        for role in payloads
    ]
    result = {
        "archives": archives,
        "archivedPath": archives[0]["archivedPath"],
        "plan": str(plan_path) if plan_path else None,
    }
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        for item in archives:
            print(f"{item['viewId']}: {item['archivedPath']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
