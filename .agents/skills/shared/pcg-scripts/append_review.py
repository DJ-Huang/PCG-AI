#!/usr/bin/env python3
"""Append a self-correction review entry to a Graph Authoring Plan (*-plan.json).

Agent vision supplies fidelity / action; this script records evidence and
updates sculptPipeline state. It does not score pixels. Visual continue requires
an evaluation receipt from capture_review_receipt.py and named feature reviews.
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
    VALID_ACTIONS, VISUAL_PASS_IDS, current_pass, has_number, load_json,
    pass_continue_blockers, pass_order, required_reference_view_ids,
    sync_pipeline_state, visual_threshold, write_json,
)
from _shared.review_receipt import digest, finite_score, validate_receipts  # noqa: E402


def split_items(value: str | None) -> list[str]:
    return [item.strip() for item in (value or '').split(';') if item.strip()]


def clamp_score(value: float) -> float:
    return finite_score(value)


def is_remote_or_virtual_path(value: str) -> bool:
    return '://' in value or value.startswith(('data:', 'blob:'))


def validate_optional_file(value: str | None, label: str) -> None:
    if value and not is_remote_or_virtual_path(value) and not Path(value).expanduser().is_file():
        raise FileNotFoundError(f'{label} does not exist: {value}')


def load_json_argument(value: str | None, label: str) -> object | None:
    if not value:
        return None
    # Parse inline JSON before treating a large JSON string as an OS filename.
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        try:
            return json.loads(Path(value).expanduser().read_text(encoding='utf-8'))
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError(f'{label} must be valid inline JSON or a JSON file path') from exc


def parse_view_evidence(value: str | None) -> list[dict]:
    payload = load_json_argument(value, 'view-evidence-json')
    if payload is None:
        return []
    if not isinstance(payload, list):
        raise ValueError('--view-evidence-json must be a JSON array')
    ids = set()
    for index, item in enumerate(payload):
        if not isinstance(item, dict):
            raise ValueError(f'viewEvidence[{index}] must be an object')
        view_id = str(item.get('viewId') or '').strip()
        if not view_id or view_id in ids:
            raise ValueError(f'viewEvidence[{index}] missing or duplicate viewId')
        ids.add(view_id)
    return payload


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path, help='Path to *-plan.json')
    parser.add_argument('--pass-id', required=True)
    parser.add_argument('--fidelity', type=float, required=True, help='Agent-asserted fidelity 0-1')
    parser.add_argument('--action', required=True, choices=sorted(VALID_ACTIONS))
    parser.add_argument('--summary', required=True)
    for name in ('matched', 'mismatches', 'plan-fixes', 'graph-fixes', 'cook-fixes',
                 'reference-screenshot', 'render-screenshot', 'comparison-image',
                 'ai-vision-notes', 'camera-view', 'layer-scores-json', 'feature-reviews-json',
                 'view-evidence-json'):
        parser.add_argument('--' + name, default='')
    parser.add_argument('--ai-vision-score', type=float, default=None)
    parser.add_argument('--visual-threshold', type=float, default=None)
    parser.add_argument('--evaluation-receipt', type=Path,
                        help='JSON from capture_review_receipt.py; required for visual continue')
    parser.add_argument('--require-screenshot-files', action='store_true')
    parser.add_argument('--in-place', action='store_true')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    plan_path = args.plan.expanduser().resolve()
    try:
        args.fidelity = clamp_score(args.fidelity)
        if args.ai_vision_score is not None:
            args.ai_vision_score = clamp_score(args.ai_vision_score)
        plan = load_json(plan_path)
        sync_pipeline_state(plan)
        ids = pass_order(plan)
        if args.pass_id not in ids:
            raise ValueError(f'unknown pass {args.pass_id!r}; expected one of: {", ".join(ids)}')
        unlocked = current_pass(plan)
        if args.pass_id != unlocked:
            raise ValueError(f'{args.action} blocked: current unlocked pass is {unlocked}, not {args.pass_id}. '
                             'Complete passes in order (report_pass.py --resume).')
        if args.require_screenshot_files:
            for label, value in (('reference', args.reference_screenshot), ('render', args.render_screenshot),
                                 ('comparison', args.comparison_image)):
                validate_optional_file(value, label)
        view_evidence = parse_view_evidence(args.view_evidence_json)
        feature_reviews = load_json_argument(args.feature_reviews_json, 'feature-reviews-json')
        layer_scores = load_json_argument(args.layer_scores_json, 'layer-scores-json')
        threshold = clamp_score(args.visual_threshold if args.visual_threshold is not None
                                else visual_threshold(plan, args.pass_id))
        required_ids = required_reference_view_ids(plan)
        evaluation_receipt = None
        evidence_hashes = []
        if args.action == 'continue':
            blockers = pass_continue_blockers(plan, args.pass_id)
            if blockers:
                raise ValueError('continue blocked:\n- ' + '\n- '.join(blockers))
            if args.pass_id in VISUAL_PASS_IDS:
                if len(required_ids) > 1 or view_evidence:
                    by_view = {str(item['viewId']).strip(): item for item in view_evidence}
                    missing = [view_id for view_id in required_ids if view_id not in by_view]
                    if missing:
                        raise ValueError(f'view evidence missing required views: {", ".join(missing)}')
                    reviewed_views = [by_view[view_id] for view_id in required_ids] if required_ids else view_evidence
                    scores = []
                    for item in reviewed_views:
                        if not str(item.get('aiVisionNotes') or '').strip() or not has_number(item.get('aiVisionScore')):
                            raise ValueError(f'view {item["viewId"]} needs finite aiVisionScore and aiVisionNotes')
                        if not isinstance(item.get('cameraReceipt'), dict):
                            raise ValueError(f'view {item["viewId"]} needs cameraReceipt')
                        scores.append(clamp_score(item['aiVisionScore']))
                    args.fidelity = args.ai_vision_score = min(scores)
                else:
                    reviewed_views = [{'referenceScreenshot': args.reference_screenshot,
                                       'renderScreenshot': args.render_screenshot,
                                       'comparisonImage': args.comparison_image}]
                    if not args.ai_vision_notes.strip():
                        raise ValueError('Visual continue requires durable --ai-vision-notes')
                score = args.ai_vision_score if args.ai_vision_score is not None else args.fidelity
                if clamp_score(score) < threshold:
                    raise ValueError(f'continue blocked: score {score} < threshold {threshold}')
                # Final evidence must be archived local files, not a URL or a promise.
                for view in reviewed_views:
                    for field in ('referenceScreenshot', 'renderScreenshot', 'comparisonImage'):
                        value = view.get(field)
                        if not value or is_remote_or_virtual_path(value):
                            raise ValueError(f'Visual continue needs an archived local {field}')
                        artifact = Path(value).expanduser().resolve()
                        if not artifact.is_file():
                            raise ValueError(f'Missing evidence: {artifact}')
                        evidence_hashes.append({'kind': field, 'path': str(artifact), 'sha256': digest(artifact)})
                if args.evaluation_receipt is None:
                    raise ValueError('Visual continue requires --evaluation-receipt from capture_review_receipt.py')
                evaluation_receipt = validate_receipts(
                    args.evaluation_receipt, [view['renderScreenshot'] for view in reviewed_views], feature_reviews,
                    view_evidence=reviewed_views if view_evidence else None)
        entry = {
            'passId': args.pass_id, 'timestamp': datetime.now(timezone.utc).isoformat(),
            'fidelity': clamp_score(args.fidelity), 'action': args.action, 'summary': args.summary,
            'matched': split_items(args.matched), 'mismatches': split_items(args.mismatches),
            'planFixes': split_items(args.plan_fixes), 'graphFixes': split_items(args.graph_fixes),
            'cookFixes': split_items(args.cook_fixes),
            'aiVisionScore': clamp_score(args.ai_vision_score if args.ai_vision_score is not None else args.fidelity),
            'aiVisionNotes': args.ai_vision_notes, 'visualAcceptanceThreshold': threshold,
            'cameraView': args.camera_view,
            'visualEvidence': {'referenceScreenshot': args.reference_screenshot or None,
                               'renderScreenshot': args.render_screenshot or None,
                               'comparisonImage': args.comparison_image or None},
            'scoreSource': 'agent-asserted', 'visualFidelityIndependentlyMeasured': False,
        }
        if view_evidence:
            entry['viewEvidence'] = view_evidence
            entry['visualEvidence'] = {key: view_evidence[0].get(key)
                                       for key in ('referenceScreenshot', 'renderScreenshot', 'comparisonImage')}
            entry['aiVisionNotes'] = args.ai_vision_notes or ' | '.join(
                str(item.get('aiVisionNotes') or '').strip() for item in view_evidence)
        if layer_scores is not None:
            entry['layerScores'] = layer_scores
        if feature_reviews is not None:
            entry['featureReviews'] = feature_reviews
        if evaluation_receipt:
            entry['evaluationReceipt'] = evaluation_receipt
            entry['evidenceHashes'] = evidence_hashes
        history = plan.get('reviewHistory')
        if not isinstance(history, list):
            history = plan['reviewHistory'] = []
        history.append(entry)
        sync_pipeline_state(plan)
        out_path = args.out.expanduser().resolve() if args.out else plan_path
        if args.in_place or args.out:
            write_json(out_path, plan)
        elif not args.json:
            print('warning: plan not written; pass --in-place or --out', file=sys.stderr)
        payload = {'written': bool(args.in_place or args.out), 'path': str(out_path),
                   'entry': entry, 'sculptPipeline': plan.get('sculptPipeline')}
        if args.json:
            print(json.dumps(payload, indent=2, ensure_ascii=False, allow_nan=False))
        else:
            print(f'action={args.action} pass={args.pass_id} fidelity={entry["fidelity"]:.2f}')
            print(f'pipeline current={plan["sculptPipeline"]["current"]}')
            if payload['written']:
                print(f'wrote: {out_path}')
        return 0
    except Exception as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
