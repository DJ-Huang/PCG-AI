#!/usr/bin/env python3
"""Export the cooked clean-review asset to GLB through the browser runtime."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Material clean-review URL from setup_web_review.py")
    parser.add_argument("--out", required=True, type=Path, help="Deterministic output .glb path")
    parser.add_argument("--timeout", type=int, default=60000, help="Navigation/readiness timeout (ms)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("ERROR: playwright not installed", file=sys.stderr)
        return 1

    out_path = args.out.expanduser().resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        page = browser.new_page()
        # The editor bridge intentionally long-polls pcg-server, so networkidle
        # is not a reachable lifecycle state on a healthy review page.
        page.goto(args.url, timeout=args.timeout, wait_until="domcontentloaded")
        page.wait_for_function("() => window.__pcgReady === true", timeout=args.timeout)
        with page.expect_download(timeout=args.timeout) as pending:
            filename = page.evaluate("() => window.__pcgReview?.downloadGlb?.() ?? null")
        download = pending.value
        download.save_as(out_path)
        browser.close()

    payload = {
        "ok": out_path.is_file() and out_path.stat().st_size > 20,
        "asset": str(out_path),
        "suggestedFilename": filename,
        "bytes": out_path.stat().st_size if out_path.is_file() else 0,
    }
    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print(f"asset: {payload['asset']} ({payload['bytes']} bytes)")
    return 0 if payload["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
