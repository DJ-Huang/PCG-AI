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
        "pcg_patch_node", "pcg_validate", "pcg_cook",
    }
    assert expected <= names, names

    context = mcp(args.base, 3, "tools/call", {"name": "pcg_get_editor_context", "arguments": {}})
    assert context["structuredContent"]["session"]["selectedNodeId"] == selected
    node = mcp(args.base, 4, "tools/call", {"name": "pcg_get_node", "arguments": {}})
    assert node["structuredContent"]["node"]["id"] == selected
    validation = mcp(args.base, 5, "tools/call", {"name": "pcg_validate", "arguments": {}})
    assert validation["structuredContent"]["ok"] is True, validation
    cook = mcp(args.base, 6, "tools/call", {"name": "pcg_cook", "arguments": {"seed": 42}})
    assert cook["structuredContent"]["ok"] is True, cook

    status, conflict = json_request(
        args.base, f"/v1/graph/nodes/{selected}", "PATCH",
        {"patch": {"width": 4.5}, "ifGraphHash": "stale"},
    )
    assert status == 409 and conflict["error"] == "graph_conflict"
    patch = mcp(
        args.base, 7, "tools/call",
        {"name": "pcg_patch_node", "arguments": {"nodeId": selected, "patch": {"width": 4.5}, "ifGraphHash": "integration-hash-v1"}},
    )
    assert patch["structuredContent"]["accepted"] is True
    patch_id = patch["structuredContent"]["patch"]["id"]
    status, queued = json_request(args.base, "/v1/graph/patches?after=0")
    assert status == 200 and queued["patches"][0]["id"] == patch_id
    status, ack = json_request(args.base, "/v1/graph/patches/ack", "POST", {"ids": [patch_id]})
    assert status == 200 and ack["acknowledged"] == 1

    _, capture_state = json_request(args.base, "/v1/session")
    baseline_capture_id = capture_state.get("captureRequestId", 0)
    capture_result = {}

    def capture_call():
        capture_result["value"] = mcp(
            args.base, 8, "tools/call",
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

    print("Agent bridge OK: REST session/patch/preview + MCP JSON/SSE/tools/validate/cook")


if __name__ == "__main__":
    main()
