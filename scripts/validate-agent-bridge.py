#!/usr/bin/env python3
"""End-to-end validation for pcg-server REST + embedded MCP bridge."""

from __future__ import annotations

import argparse
import base64
import json
import threading
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


PNG_1X1 = base64.b64encode(
    b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
    b"\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\rIDAT\x08\xd7c\xf8\xcf\xc0\xf0\x1f\x00\x05\x00\x01\xff\x89\x99=\x1d\x00\x00\x00\x00IEND\xaeB`\x82"
).decode("ascii")


def request(base: str, path: str, method: str = "GET", body=None, headers=None):
    encoded = None if body is None else json.dumps(body).encode("utf-8")
    merged = {"Accept": "application/json", **(headers or {})}
    if encoded is not None:
        merged["Content-Type"] = "application/json"
    req = urllib.request.Request(base + path, data=encoded, method=method, headers=merged)
    try:
        with urllib.request.urlopen(req, timeout=40) as response:
            raw = response.read()
            content_type = response.headers.get("Content-Type", "")
            return response.status, content_type, raw
    except urllib.error.HTTPError as error:
        return error.code, error.headers.get("Content-Type", ""), error.read()


def json_request(base: str, path: str, method: str = "GET", body=None):
    status, _, raw = request(base, path, method, body)
    return status, json.loads(raw or b"{}")


def mcp(base: str, request_id: int, method: str, params=None, accept="application/json"):
    payload = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        payload["params"] = params
    status, content_type, raw = request(
        base, "/mcp", "POST", payload, {"Accept": accept}
    )
    if content_type.startswith("text/event-stream"):
        data_line = next(line for line in raw.decode().splitlines() if line.startswith("data: "))
        result = json.loads(data_line[6:])
    else:
        result = json.loads(raw)
    assert status == 200 and "error" not in result, result
    return result["result"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:17890")
    parser.add_argument(
        "--graph",
        default=str(Path(__file__).resolve().parents[1] / "examples/boolean-subtract-box.pcg"),
    )
    args = parser.parse_args()
    graph = json.loads(Path(args.graph).read_text(encoding="utf-8"))
    manifest = json.loads(
        (Path(__file__).resolve().parents[1] / "schema/node-manifest.json").read_text(encoding="utf-8")
    )
    v2_subgraph = json.loads(
        (
            Path(__file__).resolve().parents[1]
            / "web/wooden-cabin.pcg"
        ).read_text(encoding="utf-8")
    )
    selected = graph["nodes"][0]["id"]

    status, health = json_request(args.base, "/v1/health")
    assert status == 200 and health["ok"] and health["mcp"]["enabled"]

    session = {
        "sessionId": f"bridge-integration-test-{uuid.uuid4()}",
        "clientRevision": 1,
        "graphPath": args.graph,
        "editPath": [],
        "selectedNodeId": selected,
        "previewTargetNodeId": None,
        "graphHash": "integration-hash-v1",
        "graph": graph,
        "nodeManifest": manifest,
        "updatedAt": int(time.time() * 1000),
    }
    status, synced = json_request(args.base, "/v1/session", "PUT", session)
    assert status == 200 and synced["online"] is True
    status, stale = json_request(args.base, "/v1/session", "PUT", session)
    assert status == 409 and stale["error"] == "stale_session_update"

    initialized = mcp(
        args.base,
        1,
        "initialize",
        {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "bridge-test", "version": "1"}},
        "application/json, text/event-stream",
    )
    assert initialized["capabilities"]["tools"] == {"listChanged": False}
    tools = mcp(args.base, 2, "tools/list")
    names = {tool["name"] for tool in tools["tools"]}
    expected = {
        "pcg_get_editor_context", "pcg_get_node", "pcg_list_nodes", "pcg_capture_preview",
        "pcg_get_graph", "pcg_get_node_types", "pcg_patch_node", "pcg_apply_graph_ops",
        "pcg_replace_graph", "pcg_save_graph", "pcg_validate", "pcg_cook",
    }
    assert expected <= names, names

    context = mcp(args.base, 3, "tools/call", {"name": "pcg_get_editor_context", "arguments": {}})
    assert context["structuredContent"]["session"]["selectedNodeId"] == selected
    node = mcp(args.base, 4, "tools/call", {"name": "pcg_get_node", "arguments": {}})
    assert node["structuredContent"]["node"]["id"] == selected
    document = mcp(args.base, 5, "tools/call", {"name": "pcg_get_graph", "arguments": {}})
    assert document["structuredContent"]["graph"]["nodes"][0]["id"] == selected
    node_types = mcp(
        args.base, 6, "tools/call",
        {"name": "pcg_get_node_types", "arguments": {"nodeType": "CreateBoxMesh"}},
    )
    assert node_types["structuredContent"]["count"] == 1
    validation = mcp(args.base, 7, "tools/call", {"name": "pcg_validate", "arguments": {}})
    assert validation["structuredContent"]["ok"] is True, validation
    cook = mcp(args.base, 8, "tools/call", {"name": "pcg_cook", "arguments": {"seed": 42}})
    assert cook["structuredContent"]["ok"] is True, cook

    status, conflict = json_request(
        args.base, f"/v1/graph/nodes/{selected}", "PATCH",
        {"patch": {"width": 4.5}, "ifGraphHash": "stale"},
    )
    assert status == 409 and conflict["error"] == "graph_conflict"
    stale_ops = mcp(
        args.base, 9, "tools/call",
        {
            "name": "pcg_apply_graph_ops",
            "arguments": {
                "operations": [{"op": "move_node", "nodeId": selected, "position": {"x": 1, "y": 2}}],
                "ifGraphHash": "stale",
            },
        },
    )
    assert stale_ops["isError"] is True
    assert stale_ops["structuredContent"]["error"] == "graph_conflict"

    status, spoofed = json_request(
        args.base, "/v1/graph/patches/ack", "POST",
        {"ids": [999999], "results": [{"id": 999999, "ok": True}]},
    )
    assert status == 409 and spoofed["error"] == "unknown_command_ids"
    unsafe_save = mcp(
        args.base, 90, "tools/call",
        {
            "name": "pcg_save_graph",
            "arguments": {"path": "../escape.pcg", "ifGraphHash": "integration-hash-v1"},
        },
    )
    assert unsafe_save["isError"] is True
    assert "workspace-relative" in unsafe_save["structuredContent"]["error"]

    timed_out = mcp(
        args.base, 91, "tools/call",
        {
            "name": "pcg_apply_graph_ops",
            "arguments": {
                "operations": [{"op": "move_node", "nodeId": selected, "position": {"x": 3, "y": 4}}],
                "ifGraphHash": "integration-hash-v1",
                "timeoutMs": 1000,
            },
        },
    )
    assert timed_out["isError"] is True
    assert timed_out["structuredContent"]["error"] == "apply_timeout"
    assert timed_out["structuredContent"]["cancelled"] is True
    cancelled_id = timed_out["structuredContent"]["commandId"]
    _, pending_after_timeout = json_request(args.base, "/v1/graph/patches?after=0")
    assert all(command["id"] != cancelled_id for command in pending_after_timeout["patches"])
    status, queued_patch = json_request(
        args.base, f"/v1/graph/nodes/{selected}", "PATCH",
        {"patch": {"width": 4.5}, "ifGraphHash": "integration-hash-v1"},
    )
    assert status == 202 and queued_patch["accepted"] is True
    patch_id = queued_patch["patch"]["id"]
    status, queued = json_request(args.base, "/v1/graph/patches?after=0")
    assert status == 200 and queued["patches"][0]["id"] == patch_id
    status, ack = json_request(
        args.base, "/v1/graph/patches/ack", "POST",
        {"ids": [patch_id], "results": [{"id": patch_id, "ok": True}]},
    )
    assert status == 200 and ack["acknowledged"] == 1

    command_cursor = patch_id

    def call_and_ack(request_id, name, arguments, expected_type, detail=None):
        nonlocal command_cursor
        call_result = {}

        def invoke():
            call_result["value"] = mcp(
                args.base, request_id, "tools/call",
                {"name": name, "arguments": arguments},
            )

        command_thread = threading.Thread(target=invoke)
        command_thread.start()
        command = None
        deadline = time.time() + 5
        while time.time() < deadline:
            _, payload = json_request(args.base, f"/v1/graph/patches?after={command_cursor}")
            patches = payload.get("patches", [])
            if patches:
                command = patches[0]
                break
            time.sleep(0.05)
        assert command is not None and command["type"] == expected_type, command
        command_cursor = command["id"]
        result = {"id": command["id"], "ok": True}
        if detail is not None:
            result["detail"] = detail
        status, command_ack = json_request(
            args.base, "/v1/graph/patches/ack", "POST",
            {"ids": [command["id"]], "results": [result]},
        )
        assert status == 200 and command_ack["acknowledged"] == 1
        command_thread.join(timeout=7)
        assert not command_thread.is_alive()
        applied = call_result["value"]
        assert applied["structuredContent"]["applied"] is True, applied
        return applied, command

    call_and_ack(
        10,
        "pcg_patch_node",
        {"nodeId": selected, "patch": {"__nodeTitle": "Agent Patch"}, "ifGraphHash": "integration-hash-v1"},
        "setNodeParams",
    )
    _, graph_ops_command = call_and_ack(
        11,
        "pcg_apply_graph_ops",
        {
            "operations": [{"op": "move_node", "nodeId": selected, "position": {"x": 10, "y": 20}}],
            "ifGraphHash": "integration-hash-v1",
        },
        "applyGraphOps",
        {"operationCount": 1},
    )
    assert graph_ops_command["operations"][0]["op"] == "move_node"
    _, replace_command = call_and_ack(
        12,
        "pcg_replace_graph",
        {"graph": v2_subgraph, "ifGraphHash": "integration-hash-v1"},
        "replaceGraph",
    )
    assert replace_command["graph"]["version"] == "2.0"
    assert len(replace_command["graph"]["subgraphs"]) == 5
    _, save_command = call_and_ack(
        13,
        "pcg_save_graph",
        {"path": "tmp/agent-bridge-test.pcg", "ifGraphHash": "integration-hash-v1"},
        "saveGraph",
        {"path": "tmp/agent-bridge-test.pcg"},
    )
    assert save_command["path"] == "tmp/agent-bridge-test.pcg"

    _, capture_state = json_request(args.base, "/v1/session")
    baseline_capture_id = capture_state.get("captureRequestId", 0)
    capture_result = {}

    def capture_call():
        capture_result["value"] = mcp(
            args.base, 14, "tools/call",
            {"name": "pcg_capture_preview", "arguments": {"timeoutMs": 5000}},
        )

    thread = threading.Thread(target=capture_call)
    thread.start()
    request_id = 0
    deadline = time.time() + 3
    while time.time() < deadline:
        _, state = json_request(args.base, "/v1/session")
        request_id = state.get("captureRequestId", 0)
        if request_id > baseline_capture_id:
            break
        time.sleep(0.05)
    assert request_id > baseline_capture_id
    status, uploaded = json_request(
        args.base, "/v1/preview/screenshot", "PUT",
        {"requestId": request_id, "pngBase64": PNG_1X1, "metadata": {"source": "integration-test"}},
    )
    assert status == 200 and uploaded["requestId"] == request_id
    thread.join(timeout=7)
    assert not thread.is_alive()
    captured = capture_result["value"]
    assert captured["structuredContent"]["requestId"] == request_id
    assert any(item["type"] == "image" for item in captured["content"])

    print("Agent bridge OK: MCP read/schema/atomic graph ops/replace/save/validate/cook/preview + REST locking/ack")


if __name__ == "__main__":
    main()
