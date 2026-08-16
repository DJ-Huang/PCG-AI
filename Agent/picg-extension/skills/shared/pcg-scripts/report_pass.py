#!/usr/bin/env python3
"""Report the current unlocked authoring pass and the exact next command.

Analogous to img2threejs forge/next.py. Does not score visuals.
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
    reference_views,
    review_history,
    sync_pipeline_state,
)


def skill_scripts_dir() -> Path:
    return Path(__file__).resolve().parent


def render_resume(plan_path: Path, plan: dict, completed: list[str], current: str) -> str:
    """Deterministic re-hydration file: rebuilds working memory after compaction."""
    from datetime import datetime, timezone

    archive = plan.get("referenceArchive") if isinstance(plan.get("referenceArchive"), dict) else {}
    history = review_history(plan)
    last = history[-1] if history else {}
    visual = last.get("visualEvidence") if isinstance(last.get("visualEvidence"), dict) else {}
    view_evidence = last.get("viewEvidence") if isinstance(last.get("viewEvidence"), list) else []
    trend = " → ".join(f"{entry.get('fidelity', 0):.2f}" for entry in history[-10:]) or "(no reviews yet)"
    unknowns = plan.get("unknownsToResolve") or []
    view_lines = []
    for item in reference_views(plan):
        view_id = str(item.get("id") or item.get("role") or "view")
        path = str(item.get("archivedPath") or "(not archived)")
        view_lines.append(f"- {view_id}: {path}")
    if not view_lines:
        view_lines.append(
            f"- archived reference: {archive.get('archivedPath') or plan.get('sourceImage') or '(not archived — run archive_reference.py')}"
        )
    last_view_lines = []
    for item in view_evidence:
        if not isinstance(item, dict):
            continue
        last_view_lines.append(
            f"- {item.get('viewId')}: score={item.get('aiVisionScore', '-')} cmp={item.get('comparisonImage') or '-'}"
        )
    lines = [
        f"# RESUME — {plan.get('targetName', '(unnamed)')}",
        "",
        f"regenerated: {datetime.now(timezone.utc).isoformat()} by report_pass.py --resume",
        "Do not hand-edit; rerun `report_pass.py <plan> --resume` to refresh.",
        "",
        "## Reference (re-read ALL of these before any visual/material/final decision)",
        *view_lines,
        f"- latest comparison sheet: {visual.get('comparisonImage') or '(none yet)'}",
        f"- latest render: {visual.get('renderScreenshot') or '(none yet)'}",
        "- observation.layers / viewObservations / crossViewConstraints / visualTokens: in this plan JSON",
        "",
        "## Pipeline",
        f"- plan: {plan_path}",
        f"- pcg: {plan.get('pcgPath') or '(not set)'}",
        f"- current pass: {current}",
        f"- completed: {', '.join(completed) or '(none)'}",
        f"- fidelity trend (last {min(len(history), 10)}): {trend}",
        "",
        "## Last review",
        f"- pass/action/fidelity: {last.get('passId', '-')} / {last.get('action', '-')} / {last.get('fidelity', '-')}",
        f"- summary: {last.get('summary', '-')}",
        f"- still mismatched: {'; '.join(last.get('mismatches') or []) or '-'}",
        f"- outstanding unknowns: {'; '.join(str(u) for u in unknowns) or '-'}",
        *(["", "## Last review by view", *last_view_lines] if last_view_lines else []),
        "",
        "## Next",
        f"- next command: python3 {skill_scripts_dir() / 'orchestrate_passes.py'} check {plan_path} --pass-id {current}",
        "- after context compaction: read this file, then the plan JSON, then every archived reference view",
        "",
    ]
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", type=Path, help="Path to *-plan.json")
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--resume",
        nargs="?",
        const="",
        default=None,
        help="Also write a RESUME.md re-hydration file (default: <plan-stem>-RESUME.md next to the plan)",
    )
    args = parser.parse_args(argv)

    plan_path = args.plan.expanduser().resolve()
    plan = load_json(plan_path)
    sync_pipeline_state(plan)
    ids = pass_order(plan)
    completed = completed_passes(plan, ids)
    current = current_pass(plan)
    scripts = skill_scripts_dir()

    resume_path: Path | None = None
    if args.resume is not None:
        resume_path = (
            Path(args.resume).expanduser().resolve()
            if args.resume
            else plan_path.with_name(f"{plan_path.stem}-RESUME.md")
        )
        resume_path.write_text(render_resume(plan_path, plan, completed, current), encoding="utf-8")

    if current == "complete":
        payload = {
            "pipeline": "complete",
            "currentPass": "complete",
            "completed": completed,
            "nextCommand": None,
            "unmetAcceptanceCriteria": [],
            "pcgPath": plan.get("pcgPath") or None,
        }
        if args.json:
            print(json.dumps(payload, indent=2, ensure_ascii=False))
        else:
            print("pipeline: complete")
            print(f"completed passes: {', '.join(completed)}")
        if resume_path:
            print(f"resume: {resume_path}")
        return 0

    acceptance = pass_acceptance(plan, current)
    check_cmd = (
        f"python3 {scripts / 'orchestrate_passes.py'} check {plan_path} --pass-id {current}"
    )
    validate_hint = ""
    pcg = plan.get("pcgPath")
    if isinstance(pcg, str) and pcg.strip():
        validate_hint = f"python3 {scripts / 'validate_pcg.py'} {pcg}"
    append_hint = (
        f"python3 {scripts / 'append_review.py'} {plan_path} --pass-id {current} "
        f"--fidelity 0.0-1.0 --action continue|refine-plan|refine-graph|refine-cook|"
        f"request-input|stop --summary \"...\" --in-place"
    )

    payload = {
        "pipeline": "in_progress",
        "currentPass": current,
        "completed": completed,
        "nextCommand": check_cmd,
        "unmetAcceptanceCriteria": acceptance,
        "suggestedValidate": validate_hint or None,
        "suggestedAppendReview": append_hint,
        "pcgPath": pcg or None,
        "targetName": plan.get("targetName"),
        "resumePath": str(resume_path) if resume_path else None,
    }

    if args.json:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
    else:
        print(f"target: {plan.get('targetName', '(unnamed)')}")
        print(f"current pass: {current}")
        print(f"completed: {', '.join(completed) or '(none)'}")
        print(f"next command: {check_cmd}")
        if validate_hint:
            print(f"validate: {validate_hint}")
        print("unmet acceptance criteria:")
        for item in acceptance:
            print(f"- {item}")
        print(f"after review: {append_hint}")
        if resume_path:
            print(f"resume: {resume_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
