#!/usr/bin/env python3
"""Deterministic embedded-Agent smoke test using a local fake Provider."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "pcg-server" / "build" / "pcg-server"


class FakeProvider(BaseHTTPRequestHandler):
    def log_message(self, _format: str, *_args: object) -> None:
        return

    def _json(self, value: object) -> None:
        body = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _sse(self, values: list[object], *, include_done: bool = True) -> None:
        body = "".join(f"data: {json.dumps(value)}\n\n" for value in values)
        if include_done:
            body += "data: [DONE]\n\n"
        encoded = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/v1/models":
            self._json({"data": [{"id": "fake-tool-model", "name": "Fake Tool Model"}]})
            return
        self.send_error(404)

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/v1/chat/completions":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(length))
        messages = request.get("messages", [])
        user_text = " ".join(
            part.get("text", "")
            for message in messages
            if message.get("role") == "user" and isinstance(message.get("content"), list)
            for part in message["content"]
            if isinstance(part, dict)
        )
        has_tool_result = any(message.get("role") == "tool" for message in messages)
        if "truncate" in user_text:
            self._sse([{"choices": [{
                "delta": {"reasoning_content": "I will also inspect"},
                "finish_reason": "length",
            }]}])
            return
        if "disconnect" in user_text:
            self._sse([{"choices": [{
                "delta": {"reasoning_content": "I will also inspect"},
                "finish_reason": None,
            }]}], include_done=False)
            return
        if has_tool_result:
            self._sse([
                {"choices": [{"delta": {"content": "Fake Provider "}}]},
                {"choices": [{"delta": {"content": "completed the tool loop."}}]},
            ])
            return
        elif "approval" in user_text:
            self._sse([{"choices": [{"delta": {"tool_calls": [{
                "index": 0, "id": "write-1",
                "function": {"name": "pcg_save_graph", "arguments": '{"ifGraphHash":"fake-hash"}'},
            }]}}]}])
            return
        else:
            self._sse([
                {"choices": [{"delta": {"reasoning_content": "I will inspect the live graph first."}}]},
                {"choices": [{"delta": {"tool_calls": [{
                    "index": 0, "id": "read-1",
                    "function": {"name": "pcg_get_editor_context", "arguments": "{"},
                }]}}]},
                {"choices": [{"delta": {"tool_calls": [{
                    "index": 0, "function": {"arguments": "}"},
                }]}}]},
            ])


def request(url: str, *, method: str = "GET", body: bytes | None = None, content_type: str | None = None) -> tuple[int, bytes]:
    headers = {"Content-Type": content_type} if content_type else {}
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=body, headers=headers, method=method), timeout=10) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def multipart_turn(values: dict[str, object]) -> tuple[bytes, str]:
    boundary = f"pcg-agent-{uuid.uuid4().hex}"
    payload = json.dumps(values).encode()
    body = (
        f"--{boundary}\r\n"
        'Content-Disposition: form-data; name="request"; filename="request.json"\r\n'
        "Content-Type: application/json\r\n\r\n"
    ).encode() + payload + f"\r\n--{boundary}--\r\n".encode()
    return body, f"multipart/form-data; boundary={boundary}"


def event_data(stream: bytes, event: str) -> list[dict[str, object]]:
    decoded = stream.decode()
    result: list[dict[str, object]] = []
    for block in decoded.split("\n\n"):
        lines = block.splitlines()
        if f"event: {event}" not in lines:
            continue
        data = next(line[6:] for line in lines if line.startswith("data: "))
        result.append(json.loads(data))
    return result


def event_types(stream: bytes) -> list[str]:
    result: list[str] = []
    for block in stream.decode().split("\n\n"):
        for line in block.splitlines():
            if line.startswith("event: "):
                result.append(line[7:])
                break
    return result


def sync_editor(base: str, editor_session_id: str, client_revision: int) -> None:
    status, response = request(
        f"{base}/session",
        method="PUT",
        body=json.dumps({
            "sessionId": editor_session_id,
            "clientRevision": client_revision,
            "graphPath": "agent-runtime-test.pcg",
            "editPath": [],
            "selectedNodeId": None,
            "previewTargetNodeId": None,
            "graphHash": "agent-runtime-test-hash",
            "graph": {"version": "1.0", "nodes": [], "edges": [], "parameters": [], "subgraphs": []},
            "nodeManifest": {"version": "agent-runtime-test", "nodeTypes": []},
            "updatedAt": int(time.time() * 1000),
        }).encode(),
        content_type="application/json",
    )
    assert status == 200, response


def main() -> None:
    if not SERVER.exists():
        raise SystemExit(f"Build pcg-server first: {SERVER}")
    provider = ThreadingHTTPServer(("127.0.0.1", 0), FakeProvider)
    threading.Thread(target=provider.serve_forever, daemon=True).start()
    provider_port = provider.server_address[1]
    with tempfile.TemporaryDirectory(prefix="pcg-agent-test-") as temp:
        env = os.environ.copy()
        config_path = Path(temp) / "agent.json"
        credential_path = Path(temp) / "secret-store" / "credentials.json"
        config_path.write_text(json.dumps({
            "providers": {
                "openai-compatible": {
                    "credentialStored": True,
                    "status": "connected",
                    "authType": "api",
                },
            },
        }))
        env["PCG_AGENT_CONFIG_PATH"] = str(config_path)
        env["PCG_AGENT_CREDENTIALS_PATH"] = str(credential_path)
        env["PCG_AGENT_SESSIONS_PATH"] = str(Path(temp) / "sessions")
        process = subprocess.Popen(
            [str(SERVER), "--port", "17892"], cwd=ROOT / "pcg-server", env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        server_base = "http://127.0.0.1:17892/v1"
        base = f"{server_base}/agent"
        editor_session_id = f"agent-runtime-editor-{uuid.uuid4()}"
        try:
            for _ in range(50):
                try:
                    if request(f"{base}/providers")[0] == 200:
                        break
                except urllib.error.URLError:
                    pass
                time.sleep(0.1)
            else:
                raise AssertionError("pcg-server did not start")

            status, response = request(f"{base}/providers")
            assert status == 200, response
            providers = json.loads(response)["providers"]
            status, response = request("http://127.0.0.1:17892/v1/agent/health")
            assert status == 200, response
            assert json.loads(response)["credentialStore"] == "protected-file"
            kimi = next(item for item in providers if item["id"] == "kimi-coding")
            assert kimi["name"] == "Kimi for Coding"
            assert kimi["baseUrl"] == ""
            assert kimi["authMethods"] == [{"type": "api", "label": "API Key", "available": True}]
            stale = next(item for item in providers if item["id"] == "openai-compatible")
            assert stale["connection"]["status"] == "unavailable"

            status, response = request(
                f"{base}/providers/openai-compatible/connect/key",
                method="POST",
                body=json.dumps({"apiKey": "test-secret", "baseUrl": f"http://127.0.0.1:{provider_port}/v1"}).encode(),
                content_type="application/json",
            )
            assert status == 200, response
            assert credential_path.exists()
            assert credential_path.stat().st_mode & 0o777 == 0o600
            assert credential_path.parent.stat().st_mode & 0o777 == 0o700
            assert "test-secret" not in config_path.read_text()

            status, response = request(
                f"{base}/settings", method="PUT",
                body=json.dumps({
                    "providerId": "openai-compatible", "modelId": "fake-tool-model",
                    "reasoningEffort": "max",
                }).encode(),
                content_type="application/json",
            )
            assert status == 200, response
            status, response = request(f"{base}/settings")
            assert status == 200, response
            assert json.loads(response)["reasoningEffort"] == "max"

            sync_editor(server_base, editor_session_id, 1)

            body, content_type = multipart_turn({
                "message": "run read tool", "sessionId": "read-session",
                "editorSessionId": editor_session_id,
                "providerId": "openai-compatible", "modelId": "fake-tool-model",
            })
            status, stream = request(f"{base}/turns", method="POST", body=body, content_type=content_type)
            assert status == 200
            created = event_data(stream, "turn.created")[0]
            read_session_id = str(created["sessionId"])
            sequence = event_types(stream)
            expected = [
                "reasoning.started", "reasoning.delta", "reasoning.completed",
                "tool.call", "tool.result", "message.started", "message.delta",
                "message.completed", "turn.completed",
            ]
            positions = [sequence.index(item) for item in expected]
            assert positions == sorted(positions), sequence
            assert event_data(stream, "tool.call")[0]["name"] == "pcg_get_editor_context"
            tool_result = event_data(stream, "tool.result")[0]
            assert "structuredContent" in tool_result["result"]
            assert "durationMs" in tool_result
            assert len(event_data(stream, "message.delta")) == 2
            assert event_data(stream, "turn.completed")

            for prompt, expected_code in [
                ("truncate during reasoning", "provider_output_truncated"),
                ("disconnect during reasoning", "provider_stream_interrupted"),
            ]:
                body, content_type = multipart_turn({
                    "message": prompt, "sessionId": f"{expected_code}-session",
                    "editorSessionId": editor_session_id,
                    "providerId": "openai-compatible", "modelId": "fake-tool-model",
                })
                status, interrupted = request(
                    f"{base}/turns", method="POST", body=body, content_type=content_type,
                )
                assert status == 200
                errors = event_data(interrupted, "turn.error")
                assert errors and errors[0]["error"]["code"] == expected_code, errors
                assert errors[0]["error"]["retryable"] is True

            status, response = request(f"{base}/sessions?limit=invalid&cursor=invalid")
            assert status == 200, response
            listed = json.loads(response)["sessions"]
            assert any(item["id"] == read_session_id for item in listed)
            status, response = request(f"{base}/sessions/{read_session_id}")
            assert status == 200, response
            saved = json.loads(response)["session"]
            saved_tool = next(
                part for message in saved["messages"] for part in message["parts"]
                if part["type"] == "tool"
            )
            assert "structuredContent" in saved_tool["result"]
            assert "_historyStart" not in json.dumps(saved)
            status, response = request(
                f"{base}/sessions/{read_session_id}", method="PATCH",
                body=json.dumps({"title": "Renamed runtime test"}).encode(),
                content_type="application/json",
            )
            assert status == 200 and json.loads(response)["session"]["title"] == "Renamed runtime test"

            body, content_type = multipart_turn({
                "message": "approval", "sessionId": "approval-session",
                "editorSessionId": editor_session_id,
                "providerId": "openai-compatible", "modelId": "fake-tool-model",
            })
            status, stream = request(f"{base}/turns", method="POST", body=body, content_type=content_type)
            assert status == 200
            assert not event_data(stream, "approval.required")
            tool_error = event_data(stream, "tool.result")[0]["result"]["structuredContent"]
            assert tool_error.get("error") != "user_rejected"
            assert event_data(stream, "turn.completed")

            process.terminate()
            process.wait(timeout=5)
            process = subprocess.Popen(
                [str(SERVER), "--port", "17892"], cwd=ROOT / "pcg-server", env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            for _ in range(50):
                try:
                    if request(f"{base}/providers")[0] == 200:
                        break
                except urllib.error.URLError:
                    pass
                time.sleep(0.1)
            status, response = request(f"{base}/sessions/{read_session_id}")
            assert status == 200, response
            assert json.loads(response)["session"]["title"] == "Renamed runtime test"
            status, response = request(f"{base}/settings")
            assert status == 200, response
            assert json.loads(response)["reasoningEffort"] == "max"
            status, response = request(f"{base}/providers")
            assert status == 200, response
            connected = next(
                item for item in json.loads(response)["providers"]
                if item["id"] == "openai-compatible"
            )
            assert connected["connection"]["status"] == "connected"
            sync_editor(server_base, editor_session_id, 2)
            body, content_type = multipart_turn({
                "message": "run after restart", "sessionId": "restart-session",
                "editorSessionId": editor_session_id,
                "providerId": "openai-compatible", "modelId": "fake-tool-model",
            })
            status, stream = request(f"{base}/turns", method="POST", body=body, content_type=content_type)
            assert status == 200
            assert event_data(stream, "turn.completed")
            status, _ = request(f"{base}/sessions/{read_session_id}", method="DELETE")
            assert status == 200
            assert request(f"{base}/sessions/{read_session_id}")[0] == 404
            print("agent runtime validation: ok")
        finally:
            request(f"{base}/providers/openai-compatible/connection", method="DELETE")
            process.terminate()
            process.wait(timeout=5)
            provider.shutdown()


if __name__ == "__main__":
    main()
