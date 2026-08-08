#!/usr/bin/env python3
"""Scoped cook cancellation contract for pcg-server.

Requires a running pcg-server (scripts/run-pcg-server.sh).
Run: python3 scripts/validate-cook-cancel.py [fast-graph.pcg]

Environment:
  PCG_SERVER_PORT        server port (default 17890)
  PCG_CANCEL_SLOW_GRAPH  heavier graph used for active/queued cancel tests
"""

from __future__ import annotations

import json
import os
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PORT = os.environ.get("PCG_SERVER_PORT", "17890")
BASE = f"http://127.0.0.1:{PORT}"
FAST_GRAPH = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "examples/boolean-subtract-box.pcg")
SLOW_GRAPH = Path(
    os.environ.get("PCG_CANCEL_SLOW_GRAPH", str(ROOT / "examples/lot-city-demo.pcg"))
)


def post_cook(job_id: str, graph_path: Path) -> tuple[int, bytes]:
    boundary = f"----pcg-cancel-{uuid.uuid4().hex}"
    meta = json.dumps({"seed": 42, "api_version": 1, "job_id": job_id}).encode("utf-8")
    graph = graph_path.read_bytes()

    def part(name: str, filename: str, content: bytes, content_type: str) -> bytes:
        header = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
            f"Content-Type: {content_type}\r\n\r\n"
        ).encode("utf-8")
        return header + content + b"\r\n"

    body = b"".join(
        [
            part("meta", "meta.json", meta, "application/json"),
            part("graph", "graph.json", graph, "application/json"),
            f"--{boundary}--\r\n".encode("utf-8"),
        ]
    )
    req = urllib.request.Request(
        f"{BASE}/v1/cook",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=300) as resp:
        return resp.status, resp.read()


def post_cancel(job_id: str) -> dict:
    payload = json.dumps({"job_id": job_id}).encode("utf-8")
    req = urllib.request.Request(
        f"{BASE}/v1/cancel",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


def parse_cook_result(body: bytes) -> tuple[int, str]:
    if len(body) < 12:
        raise AssertionError(f"cook result too short ({len(body)} bytes)")
    magic, version, code, _kind = struct.unpack_from("<IIiI", body, 0)
    if magic != 0x52474350:
        raise AssertionError(f"bad cook result magic 0x{magic:08x}")
    if version != 1:
        raise AssertionError(f"unexpected cook result version {version}")

    offset = 56

    def read_blob() -> bytes:
        nonlocal offset
        (size,) = struct.unpack_from("<I", body, offset)
        offset += 4
        chunk = body[offset : offset + size]
        offset += size
        return chunk

    error = read_blob().decode("utf-8", errors="replace")
    return code, error


def health_check() -> None:
    with urllib.request.urlopen(f"{BASE}/v1/health", timeout=5) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    if not data.get("ok"):
        raise AssertionError(f"health check failed: {data}")


def wait_for_cook_start(thread: threading.Thread, delay_s: float = 0.25) -> None:
    time.sleep(delay_s)
    if not thread.is_alive():
        raise AssertionError("cook finished before it could be interrupted")


def test_stale_cancel_is_noop() -> None:
    job_id = uuid.uuid4().hex
    status, body = post_cook(job_id, FAST_GRAPH)
    assert status == 200, f"cook HTTP {status}"
    code, _error = parse_cook_result(body)
    assert code == 0, "baseline cook failed"

    cancel = post_cancel(job_id)
    assert cancel.get("ok") is True
    assert cancel.get("canceled") is False, "stale cancel should not touch native cook state"


def test_wrong_job_does_not_cancel_active_cook() -> None:
    active_job = uuid.uuid4().hex
    stale_job = uuid.uuid4().hex
    result: dict[str, object] = {"code": -1, "error": ""}

    def worker() -> None:
        _status, body = post_cook(active_job, SLOW_GRAPH)
        code, error = parse_cook_result(body)
        result["code"] = code
        result["error"] = error

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    wait_for_cook_start(thread)
    cancel = post_cancel(stale_job)
    thread.join(timeout=300)

    assert cancel.get("ok") is True
    assert cancel.get("canceled") is False
    assert result["code"] == 0, f"active cook should succeed despite unrelated cancel ({result['error']})"


def test_active_matching_cancel_interrupts_running_cook() -> None:
    first_job = uuid.uuid4().hex
    second_job = uuid.uuid4().hex
    first_result: dict[str, object] = {"code": -1, "error": ""}

    def first_cook() -> None:
        _status, body = post_cook(first_job, SLOW_GRAPH)
        code, error = parse_cook_result(body)
        first_result["code"] = code
        first_result["error"] = error

    thread = threading.Thread(target=first_cook, daemon=True)
    thread.start()
    wait_for_cook_start(thread)

    started = time.monotonic()
    cancel = post_cancel(first_job)
    elapsed = time.monotonic() - started
    thread.join(timeout=300)

    assert cancel.get("canceled") is True, "matching cancel should interrupt active cook"
    assert elapsed < 2.0, f"cancel blocked for {elapsed:.2f}s"
    assert first_result["code"] != 0, "cancelled cook should not succeed"
    assert "cancel" in str(first_result["error"]).lower(), first_result["error"]

    status, body = post_cook(second_job, FAST_GRAPH)
    assert status == 200
    code, _error = parse_cook_result(body)
    assert code == 0, "replacement cook should succeed after scoped cancel"


def test_queued_job_cancelled_before_execute() -> None:
    active_job = uuid.uuid4().hex
    queued_job = uuid.uuid4().hex
    queued_result: dict[str, object] = {"code": -1, "error": ""}

    blocker = threading.Thread(
        target=lambda: post_cook(active_job, SLOW_GRAPH),
        daemon=True,
    )
    blocker.start()
    wait_for_cook_start(blocker)

    def queued_cook() -> None:
        _status, body = post_cook(queued_job, FAST_GRAPH)
        code, error = parse_cook_result(body)
        queued_result["code"] = code
        queued_result["error"] = error

    queued_thread = threading.Thread(target=queued_cook, daemon=True)
    queued_thread.start()
    time.sleep(0.05)

    cancel = post_cancel(queued_job)
    queued_thread.join(timeout=10)
    post_cancel(active_job)
    blocker.join(timeout=300)

    assert cancel.get("canceled") is True, "queued job cancel should be accepted"
    assert queued_result["code"] != 0, "queued cook should not succeed after cancel"
    assert "cancel" in str(queued_result["error"]).lower(), queued_result["error"]


def main() -> int:
    for graph in (FAST_GRAPH, SLOW_GRAPH):
        if not graph.is_file():
            print(f"ERROR: graph not found: {graph}", file=sys.stderr)
            return 1
    try:
        health_check()
        test_stale_cancel_is_noop()
        test_wrong_job_does_not_cancel_active_cook()
        test_active_matching_cancel_interrupts_running_cook()
        test_queued_job_cancelled_before_execute()
    except urllib.error.URLError as exc:
        print(f"ERROR: pcg-server unreachable at {BASE}: {exc}", file=sys.stderr)
        print("Start it with scripts/run-pcg-server.sh", file=sys.stderr)
        return 1
    except AssertionError as exc:
        print(f"COOK CANCEL CHECK FAILED: {exc}", file=sys.stderr)
        return 1

    print("Cook cancel check OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
