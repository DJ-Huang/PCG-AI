#!/usr/bin/env python3
"""Create a v2 Graph Authoring Plan for single-image or orthographic-triplet jobs."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import (  # noqa: E402
    COMPLEXITY_MINIMUMS,
    VALID_FRONT_AXES,
    VALID_SIDE_VIEWS,
    make_starter_plan,
    write_json,
)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target_name", help="Human-readable object name")
    parser.add_argument("--image", default="", help="Single primary reference image path or URL")
    parser.add_argument("--front", default="", help="Orthographic front reference path or URL")
    parser.add_argument("--side", default="", help="Orthographic side reference path or URL")
    parser.add_argument("--top", default="", help="Orthographic top reference path or URL")
    parser.add_argument(
        "--front-axis",
        choices=sorted(VALID_FRONT_AXES),
        default="+z",
        help="Object-space axis that points out of the front face",
    )
    parser.add_argument(
        "--side-view",
        choices=sorted(VALID_SIDE_VIEWS),
        default="right",
        help="Whether the supplied side image is the right or left profile",
    )
    parser.add_argument(
        "--complexity",
        choices=sorted(COMPLEXITY_MINIMUMS),
        default="moderate",
        help="Initial complexity tier (refine after image observation)",
    )
    parser.add_argument("--pcg", default="", help="Optional path to the paired .pcg file")
    parser.add_argument("--out", type=Path, required=True, help="Output *-plan.json path")
    parser.add_argument("--force", action="store_true", help="Overwrite existing output")
    parser.add_argument("--json", action="store_true", help="Also print the plan JSON")
    args = parser.parse_args(argv)

    out = args.out.expanduser().resolve()
    if out.exists() and not args.force:
        print(f"error: {out} already exists; use --force to overwrite", file=sys.stderr)
        return 2

    triplet = {"front": args.front, "side": args.side, "top": args.top}
    supplied_triplet = [role for role, value in triplet.items() if value]
    if supplied_triplet and len(supplied_triplet) != len(triplet):
        missing = [role for role, value in triplet.items() if not value]
        print(
            "error: orthographic triplet must provide --front, --side, and --top together; "
            f"missing {', '.join(missing)}",
            file=sys.stderr,
        )
        return 2

    plan = make_starter_plan(
        args.target_name,
        image=args.image,
        references=triplet if supplied_triplet else None,
        complexity=args.complexity,
        pcg_path=args.pcg,
        front_axis=args.front_axis,
        side_view=args.side_view,
    )
    write_json(out, plan)
    print(out)
    if args.json:
        print(json.dumps(plan, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
