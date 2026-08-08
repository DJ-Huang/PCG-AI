#!/usr/bin/env python3
"""Capture a screenshot of a web review page using Playwright (headless Chromium).

Opens the Vite dev server's /review route, waits for the Three.js render to
complete (window.__pcgReady === true), then captures the WebGL canvas as a PNG.

Requires Playwright: pip install playwright && playwright install chromium
Requires Vite dev server running: cd web/pcg-editor && npm run dev
Requires pcg-server running: ./scripts/run-pcg-server.sh
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Review URL (from setup_web_review.py)")
    parser.add_argument("--out", type=Path, default=None,
                        help="Output PNG path (default: screenshots/Webview_<stamp>.png)")
    parser.add_argument("--width", type=int, default=1280, help="Viewport width")
    parser.add_argument("--height", type=int, default=720, help="Viewport height")
    parser.add_argument("--wait", type=float, default=3.0,
                        help="Extra wait after render (seconds)")
    parser.add_argument("--timeout", type=int, default=30000,
                        help="Navigation timeout (ms)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    out_path = args.out
    if out_path is None:
        out_dir = Path("screenshots")
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        out_path = out_dir / f"Webview_{stamp}.png"

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print(
            "ERROR: playwright not installed.\n"
            "Install: pip install playwright && playwright install chromium",
            file=sys.stderr,
        )
        return 1

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": args.width, "height": args.height})

        page.goto(args.url, timeout=args.timeout, wait_until="networkidle")

        # Wait for Three.js render to complete
        try:
            page.wait_for_function(
                "() => window.__pcgReady === true",
                timeout=args.timeout,
            )
        except Exception:
            print("WARN: __pcgReady signal not detected; capturing anyway", file=sys.stderr)

        # Extra wait for GPU rendering
        page.wait_for_timeout(int(args.wait * 1000))

        out_path.parent.mkdir(parents=True, exist_ok=True)
        page.screenshot(path=str(out_path), full_page=False)
        browser.close()

    if args.json:
        print(json.dumps({"screenshot": str(out_path.resolve()), "url": args.url}, indent=2))
    else:
        print(f"screenshot: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
