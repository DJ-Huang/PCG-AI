#!/usr/bin/env python3
"""Generate/check the scripting operation policy artifacts for issue #23.

The ordinary node manifest owns parameter and pin metadata. The scripting policy
only classifies capabilities and explicitly opts safe operations into script
invocation. Runtime discovery combines both sources in pcg-core.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "schema" / "node-manifest.json"
POLICY_PATH = ROOT / "schema" / "script-operation-policy-v1.json"
HEADER_PATH = ROOT / "pcg-core" / "src" / "scripting" / "generated_operation_policy.hpp"
DTS_PATH = ROOT / "schema" / "generated" / "pcg-script-operations-v1.d.ts"

DENIED_CAPABILITIES = {"graph-control", "host-bound", "side-effecting"}
ALLOWED_CAPABILITIES = {"pure", "resource-read", *DENIED_CAPABILITIES}


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_sources(manifest: dict, policy: dict) -> None:
    if policy.get("version") != 1:
        raise ValueError("script operation policy version must be 1")

    default = policy.get("defaultPolicy")
    if not isinstance(default, dict):
        raise ValueError("defaultPolicy must be an object")
    if default.get("scriptCallable") is not False:
        raise ValueError("default policy must be deny-by-default")
    if default.get("capability") != "host-bound":
        raise ValueError("default capability must remain host-bound")

    manifest_types = {node.get("type") for node in manifest.get("nodes", [])}
    reserved = {"CustomFunction"}
    for operation, entry in policy.get("operations", {}).items():
        capability = entry.get("capability")
        if capability not in ALLOWED_CAPABILITIES:
            raise ValueError(f"{operation}: invalid capability {capability!r}")
        if operation not in manifest_types and operation not in reserved:
            raise ValueError(f"{operation}: policy entry is not present in node-manifest.json")
        if entry.get("scriptCallable") and capability in DENIED_CAPABILITIES:
            raise ValueError(f"{operation}: denied capability cannot be script-callable")
        resources = entry.get("resourceDependencies")
        if not isinstance(resources, list) or not all(isinstance(item, str) for item in resources):
            raise ValueError(f"{operation}: resourceDependencies must be a string array")


def symbol_for(operation: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]", "", operation)
    return f"k{cleaned}Resources"


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def render_header(policy: dict) -> str:
    default = policy["defaultPolicy"]
    operations = policy["operations"]
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "",
        "namespace pcg::internal::scripting::generated {",
        "",
        "struct OperationPolicyEntry {",
        "    const char* operation;",
        "    const char* capability;",
        "    bool script_callable;",
        "    bool cancellation_supported;",
        "    const char* const* resource_dependencies;",
        "    std::size_t resource_dependency_count;",
        "};",
        "",
        f'inline constexpr const char* kDefaultCapability = "{default["capability"]}";',
        f'inline constexpr bool kDefaultScriptCallable = {cpp_bool(default["scriptCallable"])};',
        f'inline constexpr bool kDefaultCancellationSupported = {cpp_bool(default["cancellationSupported"])};',
        "",
    ]

    resource_symbols: dict[str, str] = {}
    for operation in sorted(operations):
        resources = operations[operation].get("resourceDependencies", [])
        if resources:
            symbol = symbol_for(operation)
            resource_symbols[operation] = symbol
            quoted = ", ".join(json.dumps(value) for value in resources)
            lines.append(f"inline constexpr const char* {symbol}[] = {{{quoted}}};")
    if resource_symbols:
        lines.append("")

    lines.append("inline constexpr OperationPolicyEntry kOperationPolicies[] = {")
    for operation in sorted(operations):
        entry = operations[operation]
        resources = entry.get("resourceDependencies", [])
        symbol = resource_symbols.get(operation, "nullptr")
        lines.append(
            "    {"
            + json.dumps(operation)
            + ", "
            + json.dumps(entry["capability"])
            + f", {cpp_bool(entry['scriptCallable'])}, {cpp_bool(entry['cancellationSupported'])}, "
            + symbol
            + f", {len(resources)}}},"
        )
    lines += [
        "};",
        "",
        "inline constexpr std::size_t kOperationPolicyCount =",
        "    sizeof(kOperationPolicies) / sizeof(kOperationPolicies[0]);",
        "",
        "} // namespace pcg::internal::scripting::generated",
        "",
    ]
    return "\n".join(lines)


def render_dts(policy: dict) -> str:
    allowed = sorted(
        operation
        for operation, entry in policy["operations"].items()
        if entry.get("scriptCallable")
    )
    lines = [
        "// Generated from node-manifest.json + script-operation-policy-v1.json.",
        "export type PcgScriptOperationName =",
    ]
    if allowed:
        for index, operation in enumerate(allowed):
            suffix = ";" if index == len(allowed) - 1 else ""
            lines.append(f'  | "{operation}"{suffix}')
    else:
        lines.append("  never;")
    lines += [
        "",
        "export type PcgOperationParameters = Readonly<Record<string, unknown>>;",
        "export type PcgOperationInputs = Readonly<Record<string, unknown>>;",
        "export type PcgOperationOutputs = Readonly<Record<string, unknown>>;",
        "",
        "export interface PcgOperationInvoker {",
        "  invoke(operation: PcgScriptOperationName, parameters?: PcgOperationParameters, inputs?: PcgOperationInputs): PcgOperationOutputs;",
        "}",
        "",
    ]
    return "\n".join(lines)


def check_file(path: Path, expected: str) -> None:
    actual = path.read_text(encoding="utf-8")
    if actual != expected:
        raise SystemExit(f"generated artifact drift: {path.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if generated artifacts are stale")
    args = parser.parse_args()

    manifest = load_json(MANIFEST_PATH)
    policy = load_json(POLICY_PATH)
    validate_sources(manifest, policy)

    header = render_header(policy)
    dts = render_dts(policy)
    if args.check:
        check_file(HEADER_PATH, header)
        check_file(DTS_PATH, dts)
        return 0

    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    DTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.write_text(header, encoding="utf-8")
    DTS_PATH.write_text(dts, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
