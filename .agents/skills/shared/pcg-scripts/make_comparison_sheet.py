#!/usr/bin/env python3
"""Create a side-by-side reference vs SceneView/GameView comparison sheet.

Evidence packaging only — does not score images. Inspect the sheet with agent
vision, then record scores via append_review.py.

Pure Python 3.10+ stdlib. On macOS, non-PNG inputs are converted via `sips`.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS))

from _shared.png_io import load_image, write_png_rgb  # noqa: E402


def composite_over_checker(pixel: tuple[int, int, int, int], x: int, y: int) -> tuple[int, int, int]:
    red, green, blue, alpha = pixel
    background = 238 if ((x // 12 + y // 12) % 2 == 0) else 210
    mix = alpha / 255.0
    return (
        round(red * mix + background * (1 - mix)),
        round(green * mix + background * (1 - mix)),
        round(blue * mix + background * (1 - mix)),
    )


def resize_cover(
    width: int,
    height: int,
    pixels: list[tuple[int, int, int, int]],
    target_w: int,
    target_h: int,
) -> list[tuple[int, int, int]]:
    scale = max(target_w / width, target_h / height)
    scaled_w = max(1, round(width * scale))
    scaled_h = max(1, round(height * scale))
    offset_x = max(0, (scaled_w - target_w) // 2)
    offset_y = max(0, (scaled_h - target_h) // 2)
    output: list[tuple[int, int, int]] = []
    for y in range(target_h):
        source_y = min(height - 1, max(0, int((y + offset_y) / scale)))
        for x in range(target_w):
            source_x = min(width - 1, max(0, int((x + offset_x) / scale)))
            output.append(composite_over_checker(pixels[source_y * width + source_x], x, y))
    return output


def fill_rect(
    canvas: list[tuple[int, int, int]],
    width: int,
    x0: int,
    y0: int,
    rect_w: int,
    rect_h: int,
    color: tuple[int, int, int],
) -> None:
    height = len(canvas) // width
    for y in range(max(0, y0), min(height, y0 + rect_h)):
        row = y * width
        for x in range(max(0, x0), min(width, x0 + rect_w)):
            canvas[row + x] = color


def blit(
    canvas: list[tuple[int, int, int]],
    width: int,
    image: list[tuple[int, int, int]],
    image_w: int,
    x0: int,
    y0: int,
) -> None:
    image_h = len(image) // image_w
    height = len(canvas) // width
    for y in range(image_h):
        target_y = y0 + y
        if target_y < 0 or target_y >= height:
            continue
        for x in range(image_w):
            target_x = x0 + x
            if 0 <= target_x < width:
                canvas[target_y * width + target_x] = image[y * image_w + x]


def create_sheet(
    reference: Path,
    render: Path,
    out: Path,
    width: int,
    height: int,
    gutter: int,
) -> dict:
    ref_w, ref_h, ref_pixels = load_image(reference)
    ren_w, ren_h, ren_pixels = load_image(render)
    panel_w = width
    panel_h = height
    canvas_w = panel_w * 2 + gutter * 3
    header_h = 28
    canvas_h = panel_h + gutter * 2 + header_h
    canvas = [(246, 242, 236)] * (canvas_w * canvas_h)
    fill_rect(canvas, canvas_w, gutter, gutter, panel_w, header_h, (40, 45, 48))
    fill_rect(canvas, canvas_w, gutter * 2 + panel_w, gutter, panel_w, header_h, (40, 45, 48))
    fill_rect(canvas, canvas_w, gutter, gutter + header_h, panel_w, panel_h, (230, 230, 230))
    fill_rect(canvas, canvas_w, gutter * 2 + panel_w, gutter + header_h, panel_w, panel_h, (230, 230, 230))
    ref_panel = resize_cover(ref_w, ref_h, ref_pixels, panel_w, panel_h)
    ren_panel = resize_cover(ren_w, ren_h, ren_pixels, panel_w, panel_h)
    blit(canvas, canvas_w, ref_panel, panel_w, gutter, gutter + header_h)
    blit(canvas, canvas_w, ren_panel, panel_w, gutter * 2 + panel_w, gutter + header_h)
    fill_rect(
        canvas,
        canvas_w,
        panel_w + gutter + gutter // 2,
        gutter,
        max(2, gutter // 5),
        canvas_h - gutter * 2,
        (170, 146, 92),
    )
    write_png_rgb(out, canvas_w, canvas_h, canvas)
    return {
        "comparisonImage": str(out.resolve()),
        "referenceImage": str(reference.resolve()),
        "renderScreenshot": str(render.resolve()),
        "layout": "left=reference,right=render",
        "panelWidth": panel_w,
        "panelHeight": panel_h,
        "note": (
            "Send this image to agent vision for fidelity scores; "
            "this script does not score. Record via append_review.py."
        ),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True, help="Reference photo / concept art")
    parser.add_argument("--render", type=Path, required=True, help="Unity SceneView / GameView screenshot")
    parser.add_argument("--out", type=Path, required=True, help="Output comparison PNG")
    parser.add_argument("--panel-width", type=int, default=720)
    parser.add_argument("--panel-height", type=int, default=720)
    parser.add_argument("--gutter", type=int, default=24)
    parser.add_argument("--view-id", default="", help="Optional view id stored in the JSON receipt")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        payload = create_sheet(
            args.reference.expanduser().resolve(),
            args.render.expanduser().resolve(),
            args.out.expanduser().resolve(),
            max(128, args.panel_width),
            max(128, args.panel_height),
            max(6, args.gutter),
        )
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.view_id:
        payload["viewId"] = args.view_id
    print(json.dumps(payload, indent=2, ensure_ascii=False) if args.json else payload["comparisonImage"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
