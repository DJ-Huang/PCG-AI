#!/usr/bin/env python3
"""Gate Graph Authoring Plan passes: status / check / sync.

Analogous to img2threejs forge/stage3_build/orchestrate_passes.py.
Scripts enforce structure; they never score visuals.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import (  # noqa: E402
    completed_passes,
    current_pass,
    load_json,
    pass_acceptance,
    pass_order,
    strict_quality_issues,
    sync_pipeline_state,
    write_json,
)


def cmd_status(plan: dict, plan_path: Path, as_json: bool) -> int:
    sync_pipeline_state(plan)
    ids = pass_order(plan)
    completed = completed_passes(plan, ids)
    current = current_pass(plan)
    payload = {
        "plan": str(plan_path),
        "targetName": plan.get("targetName"),
        "currentPass": current,
        "completed": completed,
        "passOrder": ids,
        "acceptance": pass_acceptance(plan, current) if current != "complete" else [],
        "strictQualityIssues": strict_quality_issues(plan),
    }
    if as_json:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
    else:
        print(f"target: {payload['targetName']}")
        print(f"current pass: {current}")
        print(f"completed: {', '.join(completed) or '(none)'}")
        if payload["acceptance"]:
            print("acceptance:")
            for item in payload["acceptance"]:
                print(f"- {item}")
        if payload["strictQualityIssues"]:
            print("strict-quality issues (block codegen until fixed):")
            for item in payload["strictQualityIssues"]:
                print(f"- {item}")
    return 0


def cmd_check(plan: dict, pass_id: str, as_json: bool) -> int:
    sync_pipeline_state(plan)
    ids = pass_order(plan)
    if pass_id not in ids:
        print(f"error: unknown pass {pass_id!r}; expected one of: {', '.join(ids)}", file=sys.stderr)
        return 2
    completed = completed_passes(plan, ids)
    current = current_pass(plan)
    unlocked = pass_id in completed or pass_id == current
    issues = strict_quality_issues(plan) if pass_id not in {"module-plan"} else []
    # Allow module-plan even with shallow contract; block later passes
    if pass_id != "module-plan" and current != "complete" and issues:
        payload = {
            "ok": False,
            "passId": pass_id,
            "reason": "strict-quality failed",
            "strictQualityIssues": issues,
        }
        print(json.dumps(payload, indent=2, ensure_ascii=False) if as_json else f"FAIL: strict-quality\n" + "\n".join(f"- {i}" for i in issues))
        return 1
    if not unlocked:
        prev_index = ids.index(pass_id) - 1
        previous = ids[prev_index] if prev_index >= 0 else ""
        reason = f"pass {pass_id!r} is locked; complete {previous!r} first with append_review.py action=continue"
        payload = {"ok": False, "passId": pass_id, "currentPass": current, "reason": reason}
        print(json.dumps(payload, indent=2, ensure_ascii=False) if as_json else f"FAIL: {reason}")
        return 1
    payload = {
        "ok": True,
        "passId": pass_id,
        "currentPass": current,
        "acceptance": pass_acceptance(plan, pass_id),
    }
    if as_json:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
    else:
        print(f"OK: pass {pass_id} unlocked")
        for item in payload["acceptance"]:
            print(f"- {item}")
    return 0


def cmd_sync(plan: dict, plan_path: Path, in_place: bool, as_json: bool) -> int:
    sync_pipeline_state(plan)
    if in_place:
        write_json(plan_path, plan)
    payload = plan.get("sculptPipeline", {})
    if as_json:
        print(json.dumps({"sculptPipeline": payload, "written": in_place, "path": str(plan_path)}, indent=2, ensure_ascii=False))
    else:
        print(f"current: {payload.get('current')}")
        print(f"completed: {', '.join(payload.get('completed') or []) or '(none)'}")
        if in_place:
            print(f"wrote: {plan_path}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_status = sub.add_parser("status", help="Show current unlocked pass")
    p_status.add_argument("plan", type=Path)
    p_status.add_argument("--json", action="store_true")

    p_check = sub.add_parser("check", help="Non-zero unless pass is unlocked")
    p_check.add_argument("plan", type=Path)
    p_check.add_argument("--pass-id", required=True)
    p_check.add_argument("--json", action="store_true")

    p_sync = sub.add_parser("sync", help="Recompute sculptPipeline from reviewHistory")
    p_sync.add_argument("plan", type=Path)
    p_sync.add_argument("--in-place", action="store_true")
    p_sync.add_argument("--json", action="store_true")

    args = parser.parse_args(argv)
    plan_path = args.plan.expanduser().resolve()
    try:
        plan = load_json(plan_path)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.command == "status":
        return cmd_status(plan, plan_path, args.json)
    if args.command == "check":
        return cmd_check(plan, args.pass_id, args.json)
    if args.command == "sync":
        return cmd_sync(plan, plan_path, args.in_place, args.json)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
