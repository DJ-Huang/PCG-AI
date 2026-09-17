#!/usr/bin/env python3
"""Build and measure issue #21's native spike, without installing GitHub CI.

Requires Python 3.10+, CMake 3.20+, Git, and a C11/C++17 native toolchain.
No script is sent to a running pcg-server or editor session.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
METRICS = (
    "runtime_context_create_destroy",
    "fresh_module_compile_call_teardown",
    "fresh_module_10000_iterations_total",
)


def sample_count(value: str) -> int:
    count = int(value)
    if not 5 <= count <= 10000:
        raise argparse.ArgumentTypeError("samples must be between 5 and 10000")
    return count


def validate_measurement(data: dict[str, Any], samples: int, commit: str) -> None:
    """Reject stale binaries and malformed/partial benchmark output."""
    if not isinstance(data, dict):
        raise ValueError("benchmark output must be a JSON object")
    if data.get("engine") != "QuickJS-NG" or data.get("commit") != commit:
        raise ValueError("benchmark does not match the pinned runtime")
    if data.get("samples") != samples or data.get("warmup_samples") != 20:
        raise ValueError("benchmark sample counts do not match the invocation")
    metrics = data.get("metrics")
    if not isinstance(metrics, dict):
        raise ValueError("benchmark metrics must be a JSON object")
    for name in METRICS:
        metric = metrics.get(name)
        if not isinstance(metric, dict):
            raise ValueError(f"missing metric: {name}")
        values = [metric.get(k) for k in ("min_us", "p50_us", "p95_us", "max_us", "mean_us")]
        if any(isinstance(v, bool) or not isinstance(v, (float, int)) or not math.isfinite(v) or v < 0 for v in values):
            raise ValueError(f"missing or invalid timing data for {name}")
        low, p50, p95, high, mean = values
        if not low <= p50 <= p95 <= high or not low <= mean <= high:
            raise ValueError(f"inconsistent timing distribution for {name}")


def cpu_description() -> str:
    path = Path("/proc/cpuinfo")
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("model name"):
                return line.partition(":")[2].strip()
    return platform.processor() or platform.machine()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=("standalone", "core", "server"), default="standalone")
    parser.add_argument("--config", choices=("Release", "Debug", "RelWithDebInfo"), default="Release")
    parser.add_argument("--sanitizer", choices=("none", "address", "thread"), default="none")
    parser.add_argument("--generator", help="e.g. Ninja or Visual Studio 17 2022")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--quickjs-source", type=Path, help="offline, clean Git checkout at the pinned commit")
    parser.add_argument("--samples", type=sample_count, default=200)
    parser.add_argument("--parallel", type=int, default=min(os.cpu_count() or 2, 4))
    parser.add_argument("--command-timeout", type=int, default=1800, help="per-stage timeout in seconds")
    args = parser.parse_args(argv)
    if args.parallel < 1 or args.command_timeout < 1:
        parser.error("parallel and command-timeout must be positive")
    build = (args.build_dir or ROOT / "pcg-core/build/scripting" /
             f"{args.host}-{args.config}-{args.sanitizer}").resolve()
    build.mkdir(parents=True, exist_ok=True)
    report: dict[str, Any] = {
        "status": "running", "started_at_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(), "machine": platform.machine(), "cpu": cpu_description(),
        "python": platform.python_version(), "host": args.host, "config": args.config,
        "sanitizer": args.sanitizer, "commands": [], "benchmarks": {},
        "scope": "runtime spike; not production graph execution or OS sandbox acceptance",
    }
    report_path = build / "runtime-validation.json"

    def run(stage: str, command: list[str]) -> str:
        print(f"[{stage}] {shlex.join(command)}", flush=True)
        entry: dict[str, Any] = {"stage": stage, "command": command}
        report["commands"].append(entry)
        log_path = build / f"validation-{stage}.log"
        entry["log"] = str(log_path)
        try:
            completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                       encoding="utf-8", errors="replace", timeout=args.command_timeout)
        except subprocess.TimeoutExpired as error:
            entry["timed_out"] = True
            output = error.stdout or b""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            log_path.write_text(output + "\nStage timed out.\n", encoding="utf-8")
            raise RuntimeError(f"{stage} timed out; see {log_path}") from error
        log_path.write_text(completed.stdout + "\n--- stderr ---\n" + completed.stderr, encoding="utf-8")
        entry["returncode"] = completed.returncode
        if completed.returncode:
            raise RuntimeError(f"{stage} failed ({completed.returncode}); see {log_path}")
        return completed.stdout

    try:
        cmake, ctest = shutil.which("cmake"), shutil.which("ctest")
        if not cmake or not ctest or not shutil.which("git"):
            raise RuntimeError("CMake, CTest, and Git must be available on PATH")
        pin = re.search(r'set\(PCG_QUICKJS_COMMIT "([0-9a-f]{40})"\)',
                        (ROOT / "cmake/PcgQuickJS.cmake").read_text(encoding="utf-8"))
        if not pin:
            raise RuntimeError("could not read the runtime pin")
        report["runtime_commit"] = pin.group(1)
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
                                  text=True, timeout=10)
        report["repository_commit"] = revision.stdout.strip() if revision.returncode == 0 else None
        configure = [cmake, "-S", str(ROOT / "pcg-core/tests/scripting"), "-B", str(build),
                     f"-DCMAKE_BUILD_TYPE={args.config}", f"-DPCG_SCRIPTING_HOST={args.host}",
                     f"-DPCG_SCRIPTING_SANITIZER={args.sanitizer}"]
        if args.generator:
            configure += ["-G", args.generator]
        if args.quickjs_source:
            configure += [f"-DFETCHCONTENT_SOURCE_DIR_PCG_QUICKJS_SOURCE={args.quickjs_source.resolve()}"]
        run("configure", configure)
        targets = ["test_scripting_static", "test_scripting_shared"]
        if args.host != "standalone":
            targets += ["test_scripting_core_static", "test_scripting_core_shared"]
        if args.host == "server":
            targets += ["pcg-server"]
        run("build", [cmake, "--build", str(build), "--config", args.config,
                      "--parallel", str(args.parallel), "--target", *targets])
        ctest_base = [ctest, "--test-dir", str(build), "-C", args.config, "-L", "scripting"]
        inventory = json.loads(run("inventory", [*ctest_base, "--show-only=json-v1"]))
        tests = inventory.get("tests", [])
        expected_count = 26 if args.host == "standalone" else 28
        if len(tests) < expected_count:
            raise RuntimeError(f"expected at least {expected_count} native probes, discovered {len(tests)}")
        report["test_count"] = len(tests)
        run("ctest", [*ctest_base, "--output-on-failure", "--timeout", "30"])
        # CTest supplies the actual executable path for single- and multi-config
        # generators; do not guess Windows/Release output directories.
        by_name = {test["name"]: test for test in tests}
        for linkage in ("static", "shared"):
            executable = by_name[f"scripting_{linkage}_smoke"]["command"][0]
            data = json.loads(run(f"benchmark-{linkage}", [executable, "--benchmark", str(args.samples)]))
            validate_measurement(data, args.samples, pin.group(1))
            report["benchmarks"][linkage] = data
        report["status"] = "passed"
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report["status"] = "failed"
        report["error"] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        report["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
        report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Report: {report_path}")
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
