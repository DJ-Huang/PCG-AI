#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY_PATH = ROOT / "schema" / "script-operation-policy-v1.json"
MANIFEST_PATH = ROOT / "schema" / "node-manifest.json"
BRIDGE_HEADER = ROOT / "pcg-core" / "src" / "scripting" / "operation_bridge.hpp"
REGISTRY_SOURCE = ROOT / "pcg-core" / "src" / "elements" / "element_registry.cpp"


class ScriptOperationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.policy = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.manifest_by_type = {
            node["type"]: node for node in cls.manifest["nodes"] if "type" in node
        }

    def test_generated_artifacts_are_current(self) -> None:
        subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "generate-script-operation-contract.py"), "--check"],
            cwd=ROOT,
            check=True,
        )

    def test_allowlist_is_explicit_and_manifest_backed(self) -> None:
        self.assertFalse(self.policy["defaultPolicy"]["scriptCallable"])
        allowed = {
            name
            for name, entry in self.policy["operations"].items()
            if entry["scriptCallable"]
        }
        self.assertTrue(allowed)
        for name in allowed:
            self.assertIn(name, self.manifest_by_type)
            node = self.manifest_by_type[name]
            self.assertIsInstance(node.get("inputs"), list)
            self.assertIsInstance(node.get("outputs"), list)
            self.assertIsInstance(node.get("properties", {}), dict)

    def test_dangerous_capabilities_are_not_script_callable(self) -> None:
        denied = {"graph-control", "host-bound", "side-effecting"}
        for name, entry in self.policy["operations"].items():
            if entry["capability"] in denied:
                self.assertFalse(entry["scriptCallable"], name)

        for required_name in ("Output", "ForEachBegin", "ForEachEnd", "CustomFunction", "PlaceInScene"):
            self.assertIn(required_name, self.policy["operations"])
            self.assertFalse(self.policy["operations"][required_name]["scriptCallable"])

    def test_cancellation_metadata_matches_known_cooperative_operations(self) -> None:
        self.assertTrue(self.policy["operations"]["BevelMesh"]["cancellationSupported"])
        self.assertTrue(self.policy["operations"]["BooleanMesh"]["cancellationSupported"])

    def test_bridge_contract_carries_execution_contexts(self) -> None:
        header = BRIDGE_HEADER.read_text(encoding="utf-8")
        for token in (
            "graph_seed",
            "textures",
            "meshes",
            "splines",
            "heightfields",
            "dependencies",
            "is_cancel_requested",
            "statistics",
        ):
            self.assertIn(token, header)

        source = REGISTRY_SOURCE.read_text(encoding="utf-8")
        for statement in (
            "ctx.graph_seed = request.context.graph_seed",
            "ctx.graph = request.context.graph",
            "ctx.textures = request.context.textures",
            "ctx.meshes = request.context.meshes",
            "ctx.splines = request.context.splines",
            "ctx.heightfields = request.context.heightfields",
            "ctx.is_cancel_requested = request.context.is_cancel_requested",
            "ctx.dependencies = request.context.dependencies",
            "ctx.statistics = &result.statistics",
        ):
            self.assertIn(statement, source)

    def test_bridge_has_structured_failure_classes_and_thread_safe_registry(self) -> None:
        source = REGISTRY_SOURCE.read_text(encoding="utf-8")
        for code in (
            "unknown_operation",
            "operation_disallowed",
            "invalid_parameters",
            "incompatible_input",
            "incompatible_output",
            "missing_resource_context",
            "cancelled",
            "operation_failed",
        ):
            self.assertIn(code, source)
        self.assertIn("std::call_once", source)
        self.assertIn("operation_discovery_json", source)
        self.assertIn("operation_declarations", source)


if __name__ == "__main__":
    unittest.main()
