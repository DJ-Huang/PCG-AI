#!/usr/bin/env python3
"""Capture a screenshot of a web review page using Playwright (headless Chromium).

Opens the Vite /review route, waits for window.__pcgReady, optionally switches
a deterministic camera (front/side/top/three-quarter), then captures the WebGL
canvas. Use --cameras to capture an orthographic triplet in one page load.

Requires Playwright: pip install playwright && playwright install chromium
Requires Vite dev server running: cd web/pcg-editor && npm run dev
Requires pcg-server running: ./scripts/run-pcg-server.sh
"""

from __future__ import annotations

import argparse
import base64
import json
import sys
from datetime import datetime
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit

VALID_CAMERAS = ("front", "side", "top", "three-quarter")


def with_query(url: str, extra: dict[str, str]) -> str:
    parts = urlsplit(url)
    query = dict(parse_qsl(parts.query, keep_blank_values=True))
    query.update({key: value for key, value in extra.items() if value})
    return urlunsplit((parts.scheme, parts.netloc, parts.path, urlencode(query), parts.fragment))


def parse_cameras(single: str, cameras: str) -> list[str]:
    raw = cameras.strip() if cameras else (single.strip() if single else "")
    if not raw:
        return []
    values = [item.strip() for item in raw.split(",") if item.strip()]
    invalid = [item for item in values if item not in VALID_CAMERAS]
    if invalid:
        raise ValueError(f"unsupported camera(s) {invalid}; expected {list(VALID_CAMERAS)}")
    return values


def capture_canvas(page, out_path: Path, width: int, height: int) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        capture = page.evaluate(
            "(size) => window.__pcgReview?.capture?.(size) ?? null",
            {"width": width, "height": height},
        )
        if capture and capture.get("pngBase64"):
            out_path.write_bytes(base64.b64decode(capture["pngBase64"]))
            return
    except Exception as exc:
        print(f"WARN: review capture API failed: {exc}", file=sys.stderr)
    canvas = page.locator(".pcg-preview__canvas canvas").first
    if canvas.count() > 0:
        canvas.screenshot(path=str(out_path))
        return
    page.screenshot(path=str(out_path), full_page=False)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Review URL (from setup_web_review.py)")
    parser.add_argument("--out", type=Path, default=None,
                        help="Output PNG path for a single camera (default: screenshots/Webview_<stamp>.png)")
    parser.add_argument("--out-dir", type=Path, default=None,
                        help="Directory for --cameras captures (default: screenshots/)")
    parser.add_argument("--slug", default="", help="Filename prefix for --cameras")
    parser.add_argument("--camera", default="", help="Single review camera: front, side, top, three-quarter")
    parser.add_argument("--cameras", default="", help="Comma-separated cameras captured in one page load")
    parser.add_argument("--front-axis", default="+z", choices=["+x", "-x", "+z", "-z"])
    parser.add_argument("--side-view", default="right", choices=["right", "left"])
    parser.add_argument("--quality", default="", choices=["", "adaptive", "full", "medium", "low"],
                        help="Override SDF review quality; use full for final pixel acceptance")
    parser.add_argument("--width", type=int, default=1280, help="Viewport width")
    parser.add_argument("--height", type=int, default=720, help="Viewport height")
    parser.add_argument("--wait", type=float, default=0.25,
                        help="Extra wait after each camera apply (seconds)")
    parser.add_argument("--animation", default="", help="Animation clip to play or seek before capture")
    parser.add_argument("--animation-time", type=float, default=None,
                        help="Deterministically seek --animation to this time in seconds")
    parser.add_argument("--stop-animation", action="store_true",
                        help="Restore the bind pose before capture")
    parser.add_argument("--explode", type=float, default=None,
                        help="Apply a component explode amount before capture")
    parser.add_argument("--export-glb", type=Path, default=None,
                        help="Save the review page's exported GLB after captures")
    parser.add_argument("--timeout", type=int, default=30000,
                        help="Navigation timeout (ms)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        cameras = parse_cameras(args.camera, args.cameras)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    extra = {
        "frontAxis": args.front_axis,
        "sideView": args.side_view,
        "quality": args.quality,
    }
    if len(cameras) == 1:
        extra["camera"] = cameras[0]
    url = with_query(args.url, extra)

    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = (args.out_dir or Path("screenshots")).expanduser()
    slug = args.slug.strip() or "Webview"

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print(
            "ERROR: playwright not installed.\n"
            "Install: pip install playwright && playwright install chromium",
            file=sys.stderr,
        )
        return 1

    captures: list[dict] = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": args.width, "height": args.height})
        # The live editor bridge keeps polling pcg-server. DOM readiness plus
        # __pcgReady is the deterministic contract; networkidle would time out.
        page.goto(url, timeout=args.timeout, wait_until="domcontentloaded")
        try:
            page.wait_for_function("() => window.__pcgReady === true", timeout=args.timeout)
        except Exception:
            print("WARN: __pcgReady signal not detected; capturing anyway", file=sys.stderr)

        action_receipt = page.evaluate(
            """(payload) => {
              const api = window.__pcgReview;
              if (!api) return null;
              if (payload.stop) api.stopAnimation?.();
              let animationApplied = null;
              if (payload.animation) {
                animationApplied = payload.time == null
                  ? api.playAnimation?.(payload.animation)
                  : api.seekAnimation?.(payload.animation, payload.time);
              }
              if (payload.explode != null) api.setExplode?.(payload.explode);
              return {
                animations: api.animations ?? [],
                components: api.components ?? [],
                animation: payload.animation || null,
                animationTime: payload.time,
                animationApplied,
                stopped: payload.stop,
                explode: payload.explode,
                diagnostics: api.inspectActionRuntime?.() ?? null,
              };
            }""",
            {
                "stop": args.stop_animation,
                "animation": args.animation,
                "time": args.animation_time,
                "explode": args.explode,
            },
        )

        views = cameras or [""]
        for view in views:
            receipt = None
            if view:
                try:
                    receipt = page.evaluate(
                        """(payload) => {
                          const api = window.__pcgReview;
                          if (!api || typeof api.setCamera !== 'function') return null;
                          return api.setCamera(payload.view);
                        }""",
                        {"view": view},
                    )
                except Exception as exc:
                    print(f"WARN: setCamera({view}) failed: {exc}", file=sys.stderr)
            page.wait_for_timeout(int(args.wait * 1000))
            if view:
                try:
                    applied = page.evaluate(
                        "() => window.__pcgReview?.camera ?? null"
                    )
                    if applied is not None:
                        receipt = applied
                except Exception as exc:
                    print(f"WARN: camera receipt read failed: {exc}", file=sys.stderr)
            if len(views) == 1 and args.out is not None:
                out_path = args.out.expanduser()
            elif view:
                out_path = out_dir / f"{slug}_{view}.png"
            else:
                out_path = args.out.expanduser() if args.out else out_dir / f"{slug}_{stamp}.png"
            capture_canvas(page, out_path, args.width, args.height)
            captures.append({
                "view": view or (receipt or {}).get("view") or "default",
                "screenshot": str(out_path.resolve()),
                "cameraReceipt": receipt,
                "actionReceipt": action_receipt,
            })
        exported_glb = None
        if args.export_glb is not None:
            export_path = args.export_glb.expanduser().resolve()
            export_path.parent.mkdir(parents=True, exist_ok=True)
            with page.expect_download(timeout=args.timeout) as download_info:
                page.evaluate("() => window.__pcgReview?.downloadGlb?.() ?? null")
            download_info.value.save_as(str(export_path))
            exported_glb = str(export_path)
        browser.close()

    result = {
        "url": url,
        "frontAxis": args.front_axis,
        "sideView": args.side_view,
        "quality": args.quality or None,
        "captures": captures,
        "screenshot": captures[0]["screenshot"] if captures else None,
        "exportedGlb": exported_glb,
    }
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        for item in captures:
            print(f"{item['view']}: {item['screenshot']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
