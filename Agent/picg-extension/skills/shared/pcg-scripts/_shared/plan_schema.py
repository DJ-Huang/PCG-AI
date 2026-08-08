"""Shared Graph Authoring Plan helpers for PCG orchestration scripts.

Scripts enforce structure and package evidence; they never score visuals.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

DEFAULT_PASS_ORDER: list[str] = [
    "module-plan",
    "blockout",
    "structural",
    "form-refinement",
    "bevel-pass",
    "assembly",
    "material-pass",
    "parameters",
    "validation",
]

VISUAL_PASS_IDS: set[str] = {
    "blockout",
    "structural",
    "form-refinement",
    "bevel-pass",
    "assembly",
    "material-pass",
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
    "material-pass": [
        "AssignMaterial / VertexColor / UV on named parts",
        "Identity finish zones mapped (not flat memory colors)",
    ],
    "parameters": [
        "parameters[] synced with target node data defaults",
        "recommended Graph Parameters auto-applied when signals fire (or [] / user-listed)",
    ],
    "validation": [
        "validate_pcg.py exits 0",
        "Layout / __nodeTitle / Merge→Bevel warnings addressed",
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


def visual_threshold(plan: dict[str, Any]) -> float:
    loop = plan.get("selfCorrectLoop")
    if isinstance(loop, dict):
        acceptance = loop.get("visualAcceptance")
        if isinstance(acceptance, dict) and has_number(acceptance.get("threshold")):
            return max(0.0, min(1.0, float(acceptance["threshold"])))
    return 0.7


def review_completes_pass(plan: dict[str, Any], entry: dict[str, Any], pass_id: str) -> bool:
    if entry.get("passId") != pass_id or entry.get("action") != "continue":
        return False
    if pass_id not in VISUAL_PASS_IDS:
        return True
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
    threshold = entry.get("visualAcceptanceThreshold", visual_threshold(plan))
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
        passes.append(
            {
                "id": pass_id,
                "status": "in_progress" if index == 0 else "pending",
                "acceptance": list(PASS_ACCEPTANCE.get(pass_id, [])),
                "componentRefs": [],
            }
        )
    return passes


def make_starter_plan(
    target_name: str,
    *,
    image: str = "",
    complexity: str = "moderate",
    pcg_path: str = "",
) -> dict[str, Any]:
    mins = COMPLEXITY_MINIMUMS.get(complexity, COMPLEXITY_MINIMUMS["moderate"])
    return {
        "schemaVersion": 1,
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
            "reviewViewpoints": ["primary", "three-quarter"][: mins["reviewViewpoints"]],
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
        "unknownsToResolve": [],
        "localRuleHits": [],
        "buildPasses": default_build_passes(),
        "selfCorrectLoop": {
            "visualAcceptance": {"threshold": 0.7},
            "maxCyclesPerPass": 6,
        },
        "reviewHistory": [],
        "sculptPipeline": {
            "passOrder": list(DEFAULT_PASS_ORDER),
            "completed": [],
            "current": DEFAULT_PASS_ORDER[0],
        },
        "authoringInstruction": (
            "Fill objectClass, qualityContract, detailInventory, and modules from the reference "
            "before writing .pcg nodes. Use report_pass.py for the next command."
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
    return issues


def reference_persistence_issues(plan: dict[str, Any]) -> list[str]:
    """A reference-image job must keep the reference on disk, not in chat memory."""
    source = str(plan.get("sourceImage") or "")
    original = ""
    archive = plan.get("referenceArchive")
    if isinstance(archive, dict):
        original = str(archive.get("originalSource") or "")
    if not source and not original:
        return []
    issues: list[str] = []
    archived = str(archive.get("archivedPath") or "") if isinstance(archive, dict) else ""
    if not archived:
        issues.append("reference image not archived: run archive_reference.py so it survives compaction")
    elif not Path(archived).expanduser().is_file():
        issues.append(f"referenceArchive.archivedPath missing on disk: {archived}")
    if source.startswith("data:") or "://" in source:
        issues.append("sourceImage still points at a URL/data URI; point it at the archived local file")
    observation = plan.get("observation")
    layers = observation.get("layers") if isinstance(observation, dict) else None
    if not isinstance(layers, dict):
        issues.append("observation.layers missing; the layered observation must live in the plan, not in chat")
    else:
        filled = sum(1 for value in layers.values() if str(value).strip())
        if filled < 6:
            issues.append(f"observation.layers has {filled}/8 layers filled; need at least 6 before authoring")
    return issues
