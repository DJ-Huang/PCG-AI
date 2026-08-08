#!/usr/bin/env python3
"""Create a starter Graph Authoring Plan JSON (*-plan.json) for PCG reference-image jobs."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import COMPLEXITY_MINIMUMS, make_starter_plan, write_json  # noqa: E402


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target_name", help="Human-readable object name")
    parser.add_argument("--image", default="", help="Reference image path or URL")
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

    plan = make_starter_plan(
        args.target_name,
        image=args.image,
        complexity=args.complexity,
        pcg_path=args.pcg,
    )
    write_json(out, plan)
    print(out)
    if args.json:
        print(json.dumps(plan, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
