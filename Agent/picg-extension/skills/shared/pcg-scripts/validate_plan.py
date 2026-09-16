#!/usr/bin/env python3
"""Validate a Graph Authoring Plan (*-plan.json).

Normal mode: schema / field checks.
--strict-quality: blocks shallow plans before .pcg authoring (analogous to
img2threejs validate_sculpt_spec.py --strict-quality).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import (  # noqa: E402
    COMPLEXITY_MINIMUMS,
    VALID_ACTIONS,
    load_json,
    pass_order,
    strict_quality_issues,
)


def basic_issues(plan: dict) -> list[str]:
    issues: list[str] = []
    if not plan.get("targetName"):
        issues.append("missing targetName")
    if "schemaVersion" not in plan:
        issues.append("missing schemaVersion (expected 1 or 2)")
    complexity = plan.get("complexity")
    if complexity not in COMPLEXITY_MINIMUMS:
        issues.append(f"complexity must be one of {sorted(COMPLEXITY_MINIMUMS)}")
    if "qualityContract" not in plan:
        issues.append("missing qualityContract")
    if "buildPasses" not in plan:
        issues.append("missing buildPasses")
    elif not pass_order(plan):
        issues.append("buildPasses is empty")
    history = plan.get("reviewHistory", [])
    if history is not None and not isinstance(history, list):
        issues.append("reviewHistory must be a list")
    elif isinstance(history, list):
        for index, entry in enumerate(history):
            if not isinstance(entry, dict):
                issues.append(f"reviewHistory[{index}] must be an object")
                continue
            action = entry.get("action")
            if action is not None and action not in VALID_ACTIONS:
                issues.append(f"reviewHistory[{index}].action invalid: {action}")
            if "passId" not in entry:
                issues.append(f"reviewHistory[{index}] missing passId")
    return issues


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", type=Path)
    parser.add_argument("--strict-quality", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        plan = load_json(args.plan.expanduser().resolve())
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 2

    errors = basic_issues(plan)
    quality = strict_quality_issues(plan) if args.strict_quality else []
    payload = {
        "ok": not errors and not quality,
        "errors": errors,
        "strictQualityIssues": quality,
        "passOrder": pass_order(plan),
    }
    if args.json:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
    else:
        if payload["ok"]:
            print("OK" + (" (strict-quality)" if args.strict_quality else ""))
        else:
            print("FAIL")
            for item in errors:
                print(f"error: {item}")
            for item in quality:
                print(f"strict-quality: {item}")
    return 0 if payload["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
