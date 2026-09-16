#!/usr/bin/env python3
"""Regression tests for orthographic-triplet authoring plans."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

from _shared.plan_schema import (  # noqa: E402
    make_starter_plan,
    required_reference_view_ids,
    strict_quality_issues,
    view_evidence_completes_pass,
)


def run_script(name: str, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPTS / name), *args],
        capture_output=True,
        text=True,
        check=False,
    )


def fill_triplet_calibration(plan_path: Path) -> None:
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    layers = plan["observation"]["layers"]
    for index, key in enumerate(layers):
        if index < 6:
            layers[key] = f"observed {key}"
    for role in ("front", "side", "top"):
        plan["observation"]["viewObservations"][role] = {
            "silhouette": f"{role} cabin rectangle",
            "landmarks": [
                {"name": "eave-left", "uv": [0.1, 0.3]},
                {"name": "eave-right", "uv": [0.9, 0.3]},
            ],
            "visibleComponents": ["cabin_body"],
            "occlusionNotes": "",
            "confidence": 0.9,
        }
    for item in plan["observation"]["crossViewConstraints"]:
        item["value"] = 4.0
        item["driver"] = "cabin_body.size"
        item["status"] = "measured"
    plan["objectClass"]["primaryType"] = "cabin"
    plan["qualityContract"]["definitionOfDone"] = [
        "Cabin width 4 m from front+top; height 3 m from front+side; depth 5 m from side+top",
    ]
    plan_path.write_text(json.dumps(plan, indent=2), encoding="utf-8")


class TriViewPlanTests(unittest.TestCase):
    def test_starter_plan_requires_complete_triplet_flags(self) -> None:
        result = run_script(
            "new_authoring_plan.py",
            ["Cabin", "--front", "front.png", "--out", "/tmp/should-not-write.json"],
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing side, top", result.stderr)

    def test_starter_plan_records_front_side_top(self) -> None:
        plan = make_starter_plan(
            "Cabin",
            references={"front": "front.png", "side": "side.png", "top": "top.png"},
            front_axis="+z",
            side_view="right",
        )
        self.assertEqual(plan["referenceSet"]["mode"], "orthographic-triplet")
        self.assertEqual(required_reference_view_ids(plan), ["front", "side", "top"])
        self.assertEqual(plan["sculptPipeline"]["current"], "reference-calibration")
        self.assertIn("cross-view-geometry-lock", [item["id"] for item in plan["buildPasses"]])

    def test_strict_quality_blocks_empty_triplet_observations(self) -> None:
        plan = make_starter_plan(
            "Cabin",
            references={"front": "front.png", "side": "side.png", "top": "top.png"},
        )
        issues = "\n".join(strict_quality_issues(plan))
        self.assertIn("not archived", issues)
        self.assertIn("silhouette", issues)
        self.assertIn("cross-view", issues)

    def test_worst_view_cannot_hide_a_failed_view(self) -> None:
        plan = make_starter_plan(
            "Cabin",
            references={"front": "front.png", "side": "side.png", "top": "top.png"},
        )
        entry = {
            "passId": "blockout",
            "action": "continue",
            "visualAcceptanceThreshold": 0.9,
            "viewEvidence": [
                {
                    "viewId": "front",
                    "referenceScreenshot": "a.png",
                    "renderScreenshot": "b.png",
                    "comparisonImage": "c.png",
                    "aiVisionScore": 0.96,
                    "aiVisionNotes": "front ok",
                    "cameraReceipt": {"view": "front"},
                },
                {
                    "viewId": "side",
                    "referenceScreenshot": "a.png",
                    "renderScreenshot": "b.png",
                    "comparisonImage": "c.png",
                    "aiVisionScore": 0.61,
                    "aiVisionNotes": "depth collapsed",
                    "cameraReceipt": {"view": "side"},
                },
                {
                    "viewId": "top",
                    "referenceScreenshot": "a.png",
                    "renderScreenshot": "b.png",
                    "comparisonImage": "c.png",
                    "aiVisionScore": 0.94,
                    "aiVisionNotes": "top ok",
                    "cameraReceipt": {"view": "top"},
                },
            ],
        }
        self.assertFalse(view_evidence_completes_pass(plan, entry, "blockout"))

    def test_archive_require_triview_and_append_review_worst_view(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for role in ("front", "side", "top"):
                (root / f"{role}.png").write_bytes(
                    b"\x89PNG\r\n\x1a\n" + b"\x00" * 8
                )
            plan_path = root / "cabin-plan.json"
            created = run_script(
                "new_authoring_plan.py",
                [
                    "Cabin",
                    "--front", str(root / "front.png"),
                    "--side", str(root / "side.png"),
                    "--top", str(root / "top.png"),
                    "--out", str(plan_path),
                    "--force",
                ],
            )
            self.assertEqual(created.returncode, 0, created.stderr)
            archived = run_script(
                "archive_reference.py",
                [
                    "--plan", str(plan_path),
                    "--from-plan",
                    "--require-triview",
                    "--json",
                ],
            )
            self.assertEqual(archived.returncode, 0, archived.stderr)
            payload = json.loads(archived.stdout)
            self.assertEqual(len(payload["archives"]), 3)

            skipped = run_script(
                "append_review.py",
                [
                    str(plan_path),
                    "--pass-id", "blockout",
                    "--fidelity", "0.93",
                    "--action", "continue",
                    "--summary", "too early",
                    "--in-place",
                ],
            )
            self.assertEqual(skipped.returncode, 2)
            self.assertIn("current unlocked pass is reference-calibration", skipped.stderr)

            fill_triplet_calibration(plan_path)
            calibrated = run_script(
                "append_review.py",
                [
                    str(plan_path),
                    "--pass-id", "reference-calibration",
                    "--fidelity", "1",
                    "--action", "continue",
                    "--summary", "triplet archived and constrained",
                    "--in-place",
                ],
            )
            self.assertEqual(calibrated.returncode, 0, calibrated.stderr)
            planned = run_script(
                "append_review.py",
                [
                    str(plan_path),
                    "--pass-id", "module-plan",
                    "--fidelity", "1",
                    "--action", "continue",
                    "--summary", "cabin body module",
                    "--in-place",
                ],
            )
            self.assertEqual(planned.returncode, 0, planned.stderr)

            evidence = [
                {
                    "viewId": role,
                    "referenceScreenshot": str(root / f"{role}.png"),
                    "renderScreenshot": str(root / f"{role}.png"),
                    "comparisonImage": str(root / f"{role}.png"),
                    "aiVisionScore": 0.93 if role != "side" else 0.4,
                    "aiVisionNotes": f"{role} notes",
                    "cameraReceipt": {"view": role, "projection": "orthographic"},
                }
                for role in ("front", "side", "top")
            ]
            evidence_path = root / "views.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")
            blocked = run_script(
                "append_review.py",
                [
                    str(plan_path),
                    "--pass-id", "blockout",
                    "--fidelity", "0.93",
                    "--action", "continue",
                    "--summary", "should fail on side view",
                    "--view-evidence-json", str(evidence_path),
                    "--in-place",
                ],
            )
            self.assertEqual(blocked.returncode, 2)
            self.assertIn("worst required-view", blocked.stderr)


if __name__ == "__main__":
    unittest.main()
