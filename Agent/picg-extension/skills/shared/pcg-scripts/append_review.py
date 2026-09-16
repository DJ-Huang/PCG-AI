#!/usr/bin/env python3
"""Append a self-correction review entry to a Graph Authoring Plan (*-plan.json).

Agent vision supplies fidelity / action; this script only records evidence and
updates sculptPipeline state. It does not score pixels.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.plan_schema import (  # noqa: E402
    VALID_ACTIONS,
    VISUAL_PASS_IDS,
    current_pass,
    has_number,
    load_json,
    pass_continue_blockers,
    pass_order,
    required_reference_view_ids,
    sync_pipeline_state,
    visual_threshold,
    write_json,
)


def split_items(value: str | None) -> list[str]:
    if not value:
        return []
    return [item.strip() for item in value.split(";") if item.strip()]


def clamp_score(value: float) -> float:
    return max(0.0, min(1.0, float(value)))


def is_remote_or_virtual_path(value: str) -> bool:
    return "://" in value or value.startswith("data:") or value.startswith("blob:")


def validate_optional_file(value: str | None, label: str) -> None:
    if not value or is_remote_or_virtual_path(value):
        return
    if not Path(value).expanduser().exists():
        raise FileNotFoundError(f"{label} does not exist: {value}")


def parse_view_evidence(value: str | None) -> list[dict]:
    payload = load_json_argument(value, "view-evidence-json")
    if payload is None:
        return []
    if not isinstance(payload, list):
        raise ValueError("--view-evidence-json must be a JSON array")
    evidence: list[dict] = []
    for index, item in enumerate(payload):
        if not isinstance(item, dict):
            raise ValueError(f"viewEvidence[{index}] must be an object")
        view_id = str(item.get("viewId") or "").strip()
        if not view_id:
            raise ValueError(f"viewEvidence[{index}] missing viewId")
        evidence.append(item)
    return evidence


def load_json_argument(value: str | None, label: str) -> object | None:
    if not value:
        return None
    candidate = Path(value).expanduser()
    text = candidate.read_text(encoding="utf-8") if candidate.is_file() else value
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"{label} must be valid inline JSON or a JSON file path") from exc


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", type=Path, help="Path to *-plan.json")
    parser.add_argument("--pass-id", required=True)
    parser.add_argument("--fidelity", type=float, required=True, help="Overall fidelity 0-1")
    parser.add_argument("--action", required=True, choices=sorted(VALID_ACTIONS))
    parser.add_argument("--summary", required=True, help="What changed + what still mismatches")
    parser.add_argument("--matched", default="", help="Semicolon-separated matched features")
    parser.add_argument("--mismatches", default="", help="Semicolon-separated mismatches")
    parser.add_argument("--plan-fixes", default="", help="Semicolon-separated plan fixes")
    parser.add_argument("--graph-fixes", default="", help="Semicolon-separated graph fixes")
    parser.add_argument("--cook-fixes", default="", help="Semicolon-separated cook fixes")
    parser.add_argument("--reference-screenshot", default="")
    parser.add_argument("--render-screenshot", default="")
    parser.add_argument("--comparison-image", default="")
    parser.add_argument("--ai-vision-score", type=float, default=None)
    parser.add_argument("--ai-vision-notes", default="")
    parser.add_argument("--camera-view", default="")
    parser.add_argument("--layer-scores-json", default="")
    parser.add_argument("--feature-reviews-json", default="")
    parser.add_argument("--view-evidence-json", default="", help="JSON array of per-view evidence objects")
    parser.add_argument("--visual-threshold", type=float, default=None)
    parser.add_argument(
        "--require-screenshot-files",
        action="store_true",
        help="Fail if local screenshot/comparison paths are missing",
    )
    parser.add_argument("--in-place", action="store_true", help="Write back into the plan file")
    parser.add_argument("--out", type=Path, help="Write updated plan to a new path")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    plan_path = args.plan.expanduser().resolve()
    try:
        plan = load_json(plan_path)
        sync_pipeline_state(plan)
        ids = pass_order(plan)
        if args.pass_id not in ids:
            raise ValueError(f"unknown pass {args.pass_id!r}; expected one of: {', '.join(ids)}")
        unlocked = current_pass(plan)
        if args.pass_id != unlocked:
            raise ValueError(
                f"{args.action} blocked: current unlocked pass is {unlocked}, not {args.pass_id}. "
                "Complete passes in order (report_pass.py --resume)."
            )

        if args.require_screenshot_files:
            for label, value in (
                ("reference", args.reference_screenshot),
                ("render", args.render_screenshot),
                ("comparison", args.comparison_image),
            ):
                validate_optional_file(value or None, label)

        view_evidence = parse_view_evidence(args.view_evidence_json)
        threshold = (
            args.visual_threshold
            if args.visual_threshold is not None
            else visual_threshold(plan, args.pass_id)
        )
        required_ids = required_reference_view_ids(plan)

        if args.action == "continue":
            blockers = pass_continue_blockers(plan, args.pass_id)
            if blockers:
                raise ValueError("continue blocked:\n- " + "\n- ".join(blockers))
            if args.pass_id in VISUAL_PASS_IDS:
                if len(required_ids) > 1:
                    if not view_evidence:
                        raise ValueError(
                            "multi-view continue requires --view-evidence-json covering every required view"
                        )
                    by_view = {
                        str(item.get("viewId") or "").strip(): item for item in view_evidence
                    }
                    missing = [view_id for view_id in required_ids if view_id not in by_view]
                    if missing:
                        raise ValueError(f"view evidence missing required views: {', '.join(missing)}")
                    scores: list[float] = []
                    for view_id in required_ids:
                        item = by_view[view_id]
                        if not item.get("referenceScreenshot") or not item.get("renderScreenshot"):
                            raise ValueError(f"view {view_id} needs referenceScreenshot and renderScreenshot")
                        if not item.get("comparisonImage") or not str(item.get("aiVisionNotes") or "").strip():
                            raise ValueError(f"view {view_id} needs comparisonImage and aiVisionNotes")
                        if not has_number(item.get("aiVisionScore")):
                            raise ValueError(f"view {view_id} needs numeric aiVisionScore")
                        if len(required_ids) > 1 and not isinstance(item.get("cameraReceipt"), dict):
                            raise ValueError(f"view {view_id} needs cameraReceipt from capture_webview_png.py")
                        scores.append(clamp_score(float(item["aiVisionScore"])))
                    worst = min(scores)
                    if worst < clamp_score(threshold):
                        raise ValueError(
                            f"continue blocked: worst required-view score {worst} < threshold {threshold}"
                        )
                    args.fidelity = worst
                    args.ai_vision_score = worst
                else:
                    if not args.render_screenshot or not args.comparison_image:
                        raise ValueError(
                            "continue on a visual pass requires --render-screenshot and --comparison-image"
                        )
                    if not args.reference_screenshot:
                        raise ValueError(
                            "continue on a visual pass requires --reference-screenshot "
                            "(use the archived ref_<slug> file, not a chat attachment)"
                        )
                    if not (args.ai_vision_notes or "").strip():
                        raise ValueError(
                            "continue on a visual pass requires --ai-vision-notes "
                            "(durable vision memory; chat context is not)"
                        )
                    score = args.ai_vision_score if args.ai_vision_score is not None else args.fidelity
                    if clamp_score(score) < clamp_score(threshold):
                        raise ValueError(
                            f"continue blocked: aiVisionScore/fidelity {score} < threshold {threshold}"
                        )

        entry = {
            "passId": args.pass_id,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "fidelity": clamp_score(args.fidelity),
            "action": args.action,
            "summary": args.summary,
            "matched": split_items(args.matched),
            "mismatches": split_items(args.mismatches),
            "planFixes": split_items(args.plan_fixes),
            "graphFixes": split_items(args.graph_fixes),
            "cookFixes": split_items(args.cook_fixes),
            "aiVisionScore": clamp_score(
                args.ai_vision_score if args.ai_vision_score is not None else args.fidelity
            ),
            "aiVisionNotes": args.ai_vision_notes,
            "visualAcceptanceThreshold": clamp_score(threshold),
            "cameraView": args.camera_view,
            "visualEvidence": {
                "referenceScreenshot": args.reference_screenshot or None,
                "renderScreenshot": args.render_screenshot or None,
                "comparisonImage": args.comparison_image or None,
            },
        }
        if view_evidence:
            entry["viewEvidence"] = view_evidence
            first = view_evidence[0]
            entry["visualEvidence"] = {
                "referenceScreenshot": first.get("referenceScreenshot"),
                "renderScreenshot": first.get("renderScreenshot"),
                "comparisonImage": first.get("comparisonImage"),
            }
            notes = [str(item.get("aiVisionNotes") or "").strip() for item in view_evidence]
            entry["aiVisionNotes"] = args.ai_vision_notes or " | ".join(item for item in notes if item)
        layer_scores = load_json_argument(args.layer_scores_json, "layer-scores-json")
        if layer_scores is not None:
            entry["layerScores"] = layer_scores
        feature_reviews = load_json_argument(args.feature_reviews_json, "feature-reviews-json")
        if feature_reviews is not None:
            entry["featureReviews"] = feature_reviews

        history = plan.get("reviewHistory")
        if not isinstance(history, list):
            history = []
            plan["reviewHistory"] = history
        history.append(entry)
        sync_pipeline_state(plan)

        out_path = args.out.expanduser().resolve() if args.out else plan_path
        if args.in_place or args.out:
            write_json(out_path, plan)
        elif not args.json:
            print(
                "warning: plan not written; pass --in-place or --out",
                file=sys.stderr,
            )

        payload = {
            "written": bool(args.in_place or args.out),
            "path": str(out_path if (args.in_place or args.out) else plan_path),
            "entry": entry,
            "sculptPipeline": plan.get("sculptPipeline"),
        }
        if args.json:
            print(json.dumps(payload, indent=2, ensure_ascii=False))
        else:
            print(f"action={args.action} pass={args.pass_id} fidelity={entry['fidelity']:.2f}")
            print(f"pipeline current={plan['sculptPipeline']['current']}")
            if payload["written"]:
                print(f"wrote: {payload['path']}")
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
