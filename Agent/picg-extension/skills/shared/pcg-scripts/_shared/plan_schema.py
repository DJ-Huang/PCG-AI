"""Shared Graph Authoring Plan helpers for PCG orchestration scripts.

Scripts enforce structure and package evidence; they never score visuals.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

DEFAULT_PASS_ORDER: list[str] = [
    "reference-calibration",
    "module-plan",
    "blockout",
    "structural",
    "form-refinement",
    "bevel-pass",
    "assembly",
    "parameters",
    "validation",
    "cross-view-geometry-lock",
]

VISUAL_PASS_IDS: set[str] = {
    "blockout",
    "structural",
    "form-refinement",
    "bevel-pass",
    "assembly",
    "parameters",
    "cross-view-geometry-lock",
}

PLANNING_PASS_IDS: set[str] = {
    "reference-calibration",
    "module-plan",
}

TRIVIEW_ROLES: tuple[str, str, str] = ("front", "side", "top")

VALID_REFERENCE_ROLES: set[str] = {
    "primary",
    "front",
    "side",
    "top",
    "rear",
    "bottom",
    "three-quarter",
    "detail",
}

VALID_FRONT_AXES: set[str] = {"+x", "-x", "+z", "-z"}
VALID_SIDE_VIEWS: set[str] = {"right", "left"}

PASS_VISUAL_THRESHOLDS: dict[str, float] = {
    "blockout": 0.75,
    "structural": 0.82,
    "form-refinement": 0.88,
    "bevel-pass": 0.88,
    "assembly": 0.90,
    "parameters": 0.90,
    "cross-view-geometry-lock": 0.90,
}

VALID_ACTIONS: set[str] = {
    "continue",
    "refine-plan",
    "refine-graph",
    "refine-cook",
    "request-input",
    "stop",
}

COMPLEXITY_MINIMUMS: dict[str, dict[str, int]] = {
    "simple": {"macroParts": 1, "mesoParts": 0, "minDetails": 3, "reviewViewpoints": 2},
    "moderate": {"macroParts": 2, "mesoParts": 3, "minDetails": 6, "reviewViewpoints": 3},
    "complex": {"macroParts": 3, "mesoParts": 8, "minDetails": 10, "reviewViewpoints": 3},
    "ultra-complex": {"macroParts": 5, "mesoParts": 16, "minDetails": 16, "reviewViewpoints": 4},
}

PASS_ACCEPTANCE: dict[str, list[str]] = {
    "reference-calibration": [
        "Every required reference view is archived and labelled with projection/camera semantics",
        "PCG object-space coordinate frame and width/height/depth constraints are recorded",
        "Per-view silhouettes, landmarks, visibility, and conflicts are persisted in the plan",
    ],
    "module-plan": [
        "Module table recorded (nameable parts, Subgraph yes/no)",
        "qualityContract.definitionOfDone is reference-specific (not generic)",
    ],
    "blockout": [
        "Macro parts present as node chains or Subgraph stubs",
        "Overall proportions stated in meters",
        "Render or SceneView screenshot for silhouette review",
    ],
    "structural": [
        "Meso parts and attachments wired",
        "No floating parts without TransformMesh placement",
    ],
    "form-refinement": [
        "Correct primitive family per part (Sweep/Revolve/Box)",
        "Profiles match reference cross-sections",
    ],
    "bevel-pass": [
        "Per-part BevelMesh before MergeMesh",
        "Bevel amounts scaled to each part (not one post-merge amount)",
    ],
    "assembly": [
        "MergeMesh → Output complete",
        "Lane layout / Subgraph packaging if soft triggers fire",
    ],
    "parameters": [
        "parameters[] synced with target node data defaults",
        "recommended Graph Parameters auto-applied when signals fire (or [] / user-listed)",
    ],
    "validation": [
        "validate_pcg.py exits 0",
        "Layout / __nodeTitle / Merge→Bevel warnings addressed",
    ],
    "cross-view-geometry-lock": [
        "Every mandatory reference view has current deterministic camera evidence",
        "Worst required-view score meets the pass threshold; an average cannot hide a failed view",
        "Width/height/depth constraints and critical landmarks pass across views",
        "A novel three-quarter integrity view shows no collapsed depth or hidden assembly defect",
    ],
}


def load_json(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path} must be a JSON object")
    return payload


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def pass_order(plan: dict[str, Any]) -> list[str]:
    ids: list[str] = []
    for item in plan.get("buildPasses", []):
        if isinstance(item, dict) and isinstance(item.get("id"), str) and item["id"].strip():
            ids.append(item["id"].strip())
    return ids or list(DEFAULT_PASS_ORDER)


def review_history(plan: dict[str, Any]) -> list[dict[str, Any]]:
    history = plan.get("reviewHistory", [])
    if not isinstance(history, list):
        return []
    return [item for item in history if isinstance(item, dict)]


def has_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def visual_threshold(plan: dict[str, Any], pass_id: str | None = None) -> float:
    if pass_id:
        for item in plan.get("buildPasses", []):
            if not isinstance(item, dict) or item.get("id") != pass_id:
                continue
            value = item.get("visualThreshold")
            if has_number(value):
                return max(0.0, min(1.0, float(value)))
    loop = plan.get("selfCorrectLoop")
    if isinstance(loop, dict):
        acceptance = loop.get("visualAcceptance")
        if isinstance(acceptance, dict) and has_number(acceptance.get("threshold")):
            return max(0.0, min(1.0, float(acceptance["threshold"])))
    if pass_id and pass_id in PASS_VISUAL_THRESHOLDS:
        return PASS_VISUAL_THRESHOLDS[pass_id]
    return 0.7


def reference_views(plan: dict[str, Any]) -> list[dict[str, Any]]:
    """Return normalized reference views, including a v1 single-image fallback."""
    reference_set = plan.get("referenceSet")
    if isinstance(reference_set, dict) and isinstance(reference_set.get("views"), list):
        return [item for item in reference_set["views"] if isinstance(item, dict)]

    source = str(plan.get("sourceImage") or "")
    archive = plan.get("referenceArchive")
    archived = str(archive.get("archivedPath") or "") if isinstance(archive, dict) else ""
    original = str(archive.get("originalSource") or source) if isinstance(archive, dict) else source
    if not source and not archived and not original:
        return []
    return [
        {
            "id": "primary",
            "role": "primary",
            "originalSource": original,
            "archivedPath": archived or source,
            "projection": "unknown",
            "required": True,
        }
    ]


def required_reference_views(plan: dict[str, Any]) -> list[dict[str, Any]]:
    views = reference_views(plan)
    required = [item for item in views if item.get("required") is not False]
    return required or views


def required_reference_view_ids(plan: dict[str, Any]) -> list[str]:
    ids: list[str] = []
    for index, item in enumerate(required_reference_views(plan)):
        view_id = str(item.get("id") or item.get("role") or f"view-{index + 1}").strip()
        if view_id and view_id not in ids:
            ids.append(view_id)
    return ids


def reference_mode(plan: dict[str, Any]) -> str:
    reference_set = plan.get("referenceSet")
    if isinstance(reference_set, dict) and str(reference_set.get("mode") or "").strip():
        return str(reference_set["mode"])
    roles = {str(item.get("role") or item.get("id") or "") for item in reference_views(plan)}
    if set(TRIVIEW_ROLES).issubset(roles):
        return "orthographic-triplet"
    return "single" if roles else "none"


def evidence_by_view(entry: dict[str, Any]) -> dict[str, dict[str, Any]]:
    values = entry.get("viewEvidence")
    if not isinstance(values, list):
        return {}
    result: dict[str, dict[str, Any]] = {}
    for item in values:
        if not isinstance(item, dict):
            continue
        view_id = str(item.get("viewId") or "").strip()
        if view_id:
            result[view_id] = item
    return result


def view_evidence_completes_pass(
    plan: dict[str, Any],
    entry: dict[str, Any],
    pass_id: str,
) -> bool:
    required_ids = required_reference_view_ids(plan)
    by_view = evidence_by_view(entry)
    if not required_ids or not by_view:
        return False
    threshold = entry.get("visualAcceptanceThreshold", visual_threshold(plan, pass_id))
    if not has_number(threshold):
        return False
    for view_id in required_ids:
        evidence = by_view.get(view_id)
        if not evidence:
            return False
        if not evidence.get("referenceScreenshot") or not evidence.get("renderScreenshot"):
            return False
        if not evidence.get("comparisonImage") or not str(evidence.get("aiVisionNotes") or "").strip():
            return False
        if not has_number(evidence.get("aiVisionScore")):
            return False
        if float(evidence["aiVisionScore"]) < float(threshold):
            return False
        if len(required_ids) > 1 and not isinstance(evidence.get("cameraReceipt"), dict):
            return False
    return True


def review_completes_pass(plan: dict[str, Any], entry: dict[str, Any], pass_id: str) -> bool:
    if entry.get("passId") != pass_id or entry.get("action") != "continue":
        return False
    if pass_id not in VISUAL_PASS_IDS:
        return True
    if evidence_by_view(entry):
        return view_evidence_completes_pass(plan, entry, pass_id)
    visual = entry.get("visualEvidence")
    if not isinstance(visual, dict):
        return False
    if (
        not visual.get("renderScreenshot")
        or not visual.get("comparisonImage")
        or not visual.get("referenceScreenshot")
    ):
        return False
    if not str(entry.get("aiVisionNotes") or "").strip():
        return False
    score = entry.get("aiVisionScore")
    threshold = entry.get("visualAcceptanceThreshold", visual_threshold(plan, pass_id))
    if not has_number(score) or not has_number(threshold):
        return False
    return float(score) >= float(threshold)


def completed_passes(plan: dict[str, Any], ids: list[str] | None = None) -> list[str]:
    order = ids or pass_order(plan)
    history = review_history(plan)
    completed: list[str] = []
    for pass_id in order:
        if any(review_completes_pass(plan, entry, pass_id) for entry in history):
            # Also honor explicit status on buildPasses
            completed.append(pass_id)
        else:
            # Explicit status=done without visual evidence only for non-visual passes
            for item in plan.get("buildPasses", []):
                if (
                    isinstance(item, dict)
                    and item.get("id") == pass_id
                    and item.get("status") == "done"
                    and pass_id not in VISUAL_PASS_IDS
                ):
                    completed.append(pass_id)
                    break
            else:
                break
    return completed


def current_pass(plan: dict[str, Any]) -> str:
    ids = pass_order(plan)
    completed = completed_passes(plan, ids)
    if len(completed) >= len(ids):
        return "complete"
    return ids[len(completed)]


def pass_acceptance(plan: dict[str, Any], pass_id: str) -> list[str]:
    for item in plan.get("buildPasses", []):
        if isinstance(item, dict) and item.get("id") == pass_id:
            acceptance = item.get("acceptance", [])
            if isinstance(acceptance, list) and acceptance:
                return [str(value) for value in acceptance if str(value).strip()]
    return list(PASS_ACCEPTANCE.get(pass_id, ["pass-specific evidence and reviewHistory continue"]))


def sync_pipeline_state(plan: dict[str, Any]) -> dict[str, Any]:
    ids = pass_order(plan)
    completed = completed_passes(plan, ids)
    current = "complete" if len(completed) >= len(ids) else ids[len(completed)]
    for item in plan.get("buildPasses", []):
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            continue
        pass_id = item["id"]
        if pass_id in completed:
            item["status"] = "done"
        elif pass_id == current:
            item["status"] = "in_progress"
        else:
            item["status"] = item.get("status") if item.get("status") == "skipped" else "pending"
    plan["sculptPipeline"] = {
        "passOrder": ids,
        "completed": completed,
        "current": current,
    }
    return plan


def default_build_passes() -> list[dict[str, Any]]:
    passes: list[dict[str, Any]] = []
    for index, pass_id in enumerate(DEFAULT_PASS_ORDER):
        item: dict[str, Any] = {
            "id": pass_id,
            "status": "in_progress" if index == 0 else "pending",
            "acceptance": list(PASS_ACCEPTANCE.get(pass_id, [])),
            "componentRefs": [],
        }
        if pass_id in PASS_VISUAL_THRESHOLDS:
            item["visualThreshold"] = PASS_VISUAL_THRESHOLDS[pass_id]
        passes.append(item)
    return passes


def make_reference_view(role: str, source: str) -> dict[str, Any]:
    projection = "orthographic" if role in TRIVIEW_ROLES else "unknown"
    return {
        "id": role,
        "role": role,
        "originalSource": source,
        "archivedPath": "",
        "projection": projection,
        "required": True,
        "crop": None,
        "confidence": 1.0,
    }


def default_view_observation() -> dict[str, Any]:
    return {
        "silhouette": "",
        "landmarks": [],
        "visibleComponents": [],
        "occlusionNotes": "",
        "confidence": 0.0,
    }


def default_cross_view_constraints() -> list[dict[str, Any]]:
    return [
        {
            "id": "overall-width",
            "dimension": "width",
            "views": ["front", "top"],
            "value": None,
            "unit": "m",
            "tolerance": 0.02,
            "driver": "",
            "status": "unmeasured",
        },
        {
            "id": "overall-height",
            "dimension": "height",
            "views": ["front", "side"],
            "value": None,
            "unit": "m",
            "tolerance": 0.02,
            "driver": "",
            "status": "unmeasured",
        },
        {
            "id": "overall-depth",
            "dimension": "depth",
            "views": ["side", "top"],
            "value": None,
            "unit": "m",
            "tolerance": 0.02,
            "driver": "",
            "status": "unmeasured",
        },
    ]


def make_starter_plan(
    target_name: str,
    *,
    image: str = "",
    references: dict[str, str] | None = None,
    complexity: str = "moderate",
    pcg_path: str = "",
    front_axis: str = "+z",
    side_view: str = "right",
) -> dict[str, Any]:
    mins = COMPLEXITY_MINIMUMS.get(complexity, COMPLEXITY_MINIMUMS["moderate"])
    sources: dict[str, str] = {}
    if image:
        sources["primary"] = image
    for role, source in (references or {}).items():
        if source:
            sources[role] = source
    views = [make_reference_view(role, source) for role, source in sources.items()]
    roles = {item["role"] for item in views}
    triplet = set(TRIVIEW_ROLES).issubset(roles)
    reference_mode_value = "orthographic-triplet" if triplet else ("single" if views else "none")
    view_observations = {
        str(item["id"]): default_view_observation()
        for item in views
    }
    required_view_ids = [str(item["id"]) for item in views]
    review_viewpoints = list(required_view_ids)
    if "three-quarter" not in review_viewpoints:
        review_viewpoints.append("three-quarter-integrity")
    return {
        "schemaVersion": 2,
        "targetName": target_name,
        "sourceImage": image,
        "pcgPath": pcg_path,
        "complexity": complexity,
        "referenceArchive": {
            "archivedPath": "",
            "originalSource": image,
            "note": (
                "Run archive_reference.py right after plan creation. A chat attachment or URL "
                "is not durable memory: compaction drops pasted images and URLs rot."
            ),
        },
        "referenceSet": {
            "mode": reference_mode_value,
            "views": views,
            "note": (
                "Use front/side/top for an orthographic triplet. Keep every required view "
                "archived locally and compare it only with its deterministic camera preset."
            ),
        },
        "coordinateFrame": {
            "space": "PCG object space",
            "handedness": "left-handed",
            "upAxis": "+y",
            "frontAxis": front_axis,
            "sideView": side_view,
            "origin": "asset pivot",
        },
        "observation": {
            "layers": {
                "identification": "",
                "formSilhouette": "",
                "macroMesoMicro": "",
                "spatialRelationships": "",
                "materialsSurface": "",
                "colorFinish": "",
                "identityFeatures": "",
                "uncertainty": "",
            },
            "shapeAnalysis": "",
            "viewObservations": view_observations,
            "crossViewConstraints": default_cross_view_constraints() if triplet else [],
            "conflictResolutions": [],
            "note": (
                "Fill every layer from the reference BEFORE authoring. This prose is the "
                "durable memory of the reference; the conversation attachment is not."
            ),
        },
        "visualTokens": {
            "namedDimensions": [],
            "proportions": [],
            "materialPalette": [],
            "note": (
                "Distill hex colors, roughness/metalness ranges, and key ratios so material "
                "and final-acceptance stages consult data instead of recalling the image."
            ),
        },
        "objectClass": {
            "primaryType": "unassessed",
            "primaryDomain": "object",
            "notes": "Fill from layered image observation before authoring nodes.",
        },
        "qualityContract": {
            "qualityBar": complexity,
            "definitionOfDone": [
                "Replace with reference-specific silhouette, part coverage, and finish criteria.",
            ],
            "minimumMacroParts": mins["macroParts"],
            "minimumMesoParts": mins["mesoParts"],
            "minimumDetails": mins["minDetails"],
            "requiredReferenceViews": required_view_ids,
            "reviewViewpoints": review_viewpoints,
            "worstRequiredViewThreshold": 0.90,
        },
        "detailInventory": {
            "targetMinDetails": mins["minDetails"],
            "details": [],
            "note": (
                "Enumerate identity-defining details; each must mapsTo a real node id/property, "
                "never prose only."
            ),
        },
        "modules": [],
        "componentHypotheses": [],
        "unknownsToResolve": [],
        "localRuleHits": [],
        "buildPasses": default_build_passes(),
        "selfCorrectLoop": {
            "visualAcceptance": {"threshold": 0.90, "aggregation": "worst-required-view"},
            "maxGeometryCycles": 12,
            "maxCyclesPerPass": 12,
            "plateau": {"window": 3, "minimumWorstViewDelta": 0.01},
        },
        "reviewHistory": [],
        "sculptPipeline": {
            "passOrder": list(DEFAULT_PASS_ORDER),
            "completed": [],
            "current": DEFAULT_PASS_ORDER[0],
        },
        "authoringInstruction": (
            "Calibrate references, fill per-view observations/cross-view constraints and "
            "manifest-backed component hypotheses before writing .pcg nodes. Use "
            "report_pass.py for the next command."
        ),
    }


def count_mapped_details(plan: dict[str, Any]) -> int:
    inventory = plan.get("detailInventory")
    if isinstance(inventory, dict):
        details = inventory.get("details", [])
    else:
        details = plan.get("detailInventory", [])
    if not isinstance(details, list):
        return 0
    mapped = 0
    for detail in details:
        if not isinstance(detail, dict):
            continue
        maps_to = detail.get("mapsTo")
        if isinstance(maps_to, dict) and (
            maps_to.get("nodeId") or maps_to.get("subgraphId") or maps_to.get("property")
        ):
            mapped += 1
        elif isinstance(maps_to, str) and maps_to.strip():
            mapped += 1
    return mapped


def strict_quality_issues(plan: dict[str, Any]) -> list[str]:
    issues: list[str] = []
    complexity = str(plan.get("complexity") or "moderate")
    mins = COMPLEXITY_MINIMUMS.get(complexity, COMPLEXITY_MINIMUMS["moderate"])
    contract = plan.get("qualityContract")
    if not isinstance(contract, dict):
        issues.append("missing qualityContract")
        return issues
    dod = contract.get("definitionOfDone")
    if not isinstance(dod, list) or not dod:
        issues.append("qualityContract.definitionOfDone is empty")
    elif len(dod) == 1 and "Replace with reference-specific" in str(dod[0]):
        issues.append("qualityContract.definitionOfDone is still the generic starter text")
    macro = int(contract.get("minimumMacroParts") or 0)
    meso = int(contract.get("minimumMesoParts") or 0)
    if macro < mins["macroParts"]:
        issues.append(f"minimumMacroParts {macro} < {mins['macroParts']} for {complexity}")
    if meso < mins["mesoParts"]:
        issues.append(f"minimumMesoParts {meso} < {mins['mesoParts']} for {complexity}")
    inventory = plan.get("detailInventory")
    target = mins["minDetails"]
    if isinstance(inventory, dict):
        target = int(inventory.get("targetMinDetails") or target)
    mapped = count_mapped_details(plan)
    if mapped < target:
        issues.append(f"mapped detailInventory entries {mapped} < targetMinDetails {target}")
    modules = plan.get("modules")
    if complexity in {"complex", "ultra-complex"} and (
        not isinstance(modules, list) or len(modules) < 2
    ):
        issues.append(f"{complexity} plans should list at least 2 modules")
    obj = plan.get("objectClass")
    if isinstance(obj, dict) and obj.get("primaryType") in {None, "", "unassessed"}:
        issues.append("objectClass.primaryType is still unassessed")
    issues.extend(reference_persistence_issues(plan))
    if int(plan.get("schemaVersion") or 1) >= 2 and reference_views(plan):
        issues.extend(reference_calibration_issues(plan))
        issues.extend(component_hypothesis_issues(plan, macro))
    return issues


def reference_calibration_issues(plan: dict[str, Any]) -> list[str]:
    issues: list[str] = []
    frame = plan.get("coordinateFrame")
    if not isinstance(frame, dict):
        issues.append("missing coordinateFrame")
    else:
        if frame.get("frontAxis") not in VALID_FRONT_AXES:
            issues.append(f"coordinateFrame.frontAxis must be one of {sorted(VALID_FRONT_AXES)}")
        if frame.get("upAxis") != "+y":
            issues.append("coordinateFrame.upAxis must be +y in PCG object space")
        if frame.get("sideView") not in VALID_SIDE_VIEWS:
            issues.append(f"coordinateFrame.sideView must be one of {sorted(VALID_SIDE_VIEWS)}")

    views = reference_views(plan)
    ids = [str(item.get("id") or item.get("role") or "").strip() for item in views]
    if any(not view_id for view_id in ids):
        issues.append("every referenceSet.views[] entry needs a non-empty id")
    if len(ids) != len(set(ids)):
        issues.append("referenceSet.views[] ids must be unique")

    mode = reference_mode(plan)
    if mode == "orthographic-triplet":
        by_role = {str(item.get("role") or item.get("id") or ""): item for item in views}
        for role in TRIVIEW_ROLES:
            item = by_role.get(role)
            if not item:
                issues.append(f"orthographic-triplet missing {role} reference")
                continue
            if item.get("projection") != "orthographic":
                issues.append(f"reference view {role} projection must be orthographic")

        observation = plan.get("observation")
        per_view = observation.get("viewObservations") if isinstance(observation, dict) else None
        if not isinstance(per_view, dict):
            issues.append("observation.viewObservations missing for orthographic-triplet")
        else:
            for role in TRIVIEW_ROLES:
                value = per_view.get(role)
                if not isinstance(value, dict):
                    issues.append(f"observation.viewObservations.{role} missing")
                    continue
                if not str(value.get("silhouette") or "").strip():
                    issues.append(f"view observation {role} missing silhouette description")
                landmarks = value.get("landmarks")
                if not isinstance(landmarks, list) or len(landmarks) < 2:
                    issues.append(f"view observation {role} needs at least 2 calibrated landmarks")

        constraints = observation.get("crossViewConstraints") if isinstance(observation, dict) else None
        if not isinstance(constraints, list):
            issues.append("observation.crossViewConstraints missing for orthographic-triplet")
        else:
            by_dimension = {
                str(item.get("dimension") or ""): item
                for item in constraints
                if isinstance(item, dict)
            }
            for dimension in ("width", "height", "depth"):
                item = by_dimension.get(dimension)
                if not item:
                    issues.append(f"cross-view {dimension} constraint missing")
                    continue
                if not has_number(item.get("value")) or float(item["value"]) <= 0:
                    issues.append(f"cross-view {dimension} constraint needs a positive numeric value")
                if not str(item.get("driver") or "").strip():
                    issues.append(f"cross-view {dimension} constraint missing owner/driver")
                tolerance = item.get("tolerance")
                if not has_number(tolerance) or not 0 < float(tolerance) <= 0.1:
                    issues.append(f"cross-view {dimension} tolerance must be in (0, 0.1]")
    return issues


def component_hypothesis_issues(plan: dict[str, Any], minimum_macro_parts: int) -> list[str]:
    hypotheses = plan.get("componentHypotheses")
    if not isinstance(hypotheses, list) or len(hypotheses) < minimum_macro_parts:
        return [
            "componentHypotheses must cover every macro part with manifest-backed candidates "
            f"(need at least {minimum_macro_parts})"
        ]
    issues: list[str] = []
    for index, item in enumerate(hypotheses):
        if not isinstance(item, dict):
            issues.append(f"componentHypotheses[{index}] must be an object")
            continue
        label = str(item.get("component") or f"[{index}]")
        if not str(item.get("chosenNodeType") or "").strip():
            issues.append(f"component hypothesis {label} missing chosenNodeType")
        if not str(item.get("manifestEvidence") or "").strip():
            issues.append(f"component hypothesis {label} missing live/static manifestEvidence")
        if not str(item.get("crossViewEvidence") or "").strip():
            issues.append(f"component hypothesis {label} missing crossViewEvidence")
        drivers = item.get("dimensionDrivers")
        if not isinstance(drivers, list) or not drivers:
            issues.append(f"component hypothesis {label} missing dimensionDrivers")
    return issues


def reference_persistence_issues(plan: dict[str, Any]) -> list[str]:
    """A reference-image job must keep the reference on disk, not in chat memory."""
    views = reference_views(plan)
    if not views:
        return []
    issues: list[str] = []
    for index, item in enumerate(required_reference_views(plan)):
        view_id = str(item.get("id") or item.get("role") or f"view-{index + 1}")
        archived = str(item.get("archivedPath") or "")
        if not archived:
            issues.append(f"reference view {view_id} not archived: run archive_reference.py")
        elif is_remote_reference(archived):
            issues.append(f"reference view {view_id} archivedPath must be a durable local file")
        elif not Path(archived).expanduser().is_file():
            issues.append(f"reference view {view_id} archivedPath missing on disk: {archived}")
    observation = plan.get("observation")
    layers = observation.get("layers") if isinstance(observation, dict) else None
    if not isinstance(layers, dict):
        issues.append("observation.layers missing; the layered observation must live in the plan, not in chat")
    else:
        filled = sum(1 for value in layers.values() if str(value).strip())
        if filled < 6:
            issues.append(f"observation.layers has {filled}/8 layers filled; need at least 6 before authoring")
    return issues


def is_remote_reference(value: str) -> bool:
    return value.startswith(("data:", "blob:")) or "://" in value


def pass_continue_blockers(plan: dict[str, Any], pass_id: str) -> list[str]:
    """Extra gates that must be empty before action=continue unlocks the next pass."""
    if pass_id == "reference-calibration":
        return reference_persistence_issues(plan) + reference_calibration_issues(plan)
    return []
