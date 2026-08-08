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
    load_json,
    pass_order,
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
        ids = pass_order(plan)
        if args.pass_id not in ids:
            raise ValueError(f"unknown pass {args.pass_id!r}; expected one of: {', '.join(ids)}")

        if args.require_screenshot_files:
            for label, value in (
                ("reference", args.reference_screenshot),
                ("render", args.render_screenshot),
                ("comparison", args.comparison_image),
            ):
                validate_optional_file(value or None, label)

        if args.action == "continue" and args.pass_id in VISUAL_PASS_IDS:
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
            threshold = (
                args.visual_threshold
                if args.visual_threshold is not None
                else visual_threshold(plan)
            )
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
            "visualAcceptanceThreshold": clamp_score(
                args.visual_threshold
                if args.visual_threshold is not None
                else visual_threshold(plan)
            ),
            "cameraView": args.camera_view,
            "visualEvidence": {
                "referenceScreenshot": args.reference_screenshot or None,
                "renderScreenshot": args.render_screenshot or None,
                "comparisonImage": args.comparison_image or None,
            },
        }
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
