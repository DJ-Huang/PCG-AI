#!/usr/bin/env python3
"""Capture full-quality Web review PNGs and a graph/cook/camera receipt.

Requires a running Web editor + rebuilt pcg-server and Playwright Chromium.
Unlike diagnostic screenshots, this command never falls back to capturing an
error page. Pass the resulting --out JSON to append_review.py --evaluation-receipt.
"""
from __future__ import annotations

import argparse
import base64
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit

from _shared.review_receipt import digest, validate_evaluation


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('url', help='The /review?graph=... URL for the same source file')
    parser.add_argument('--graph', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path, help='Receipt JSON; PNGs use the same filename prefix')
    parser.add_argument('--cameras', default='front,side,top,three-quarter')
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--timeout', type=int, default=60000)
    args = parser.parse_args(argv)
    try:
        views = args.cameras.split(',')
        allowed = {'front', 'side', 'top', 'three-quarter'}
        if not views or len(set(views)) != len(views) or set(views) - allowed:
            raise ValueError('Use unique front, side, top, or three-quarter cameras')
        if not (0 < args.width <= 8192 and 0 < args.height <= 8192):
            raise ValueError('Capture dimensions must be in 1..8192')
        source_path = args.graph.expanduser().resolve()
        source_hash = digest(source_path)
        source = json.loads(source_path.read_text(encoding='utf-8'))
        out = args.out.expanduser().resolve()
        if out.exists():
            raise ValueError('Receipt already exists; use a new revision filename')
        parts = urlsplit(args.url)
        query = dict(parse_qsl(parts.query, keep_blank_values=True))
        query['quality'] = 'full'
        url = urlunsplit((parts.scheme, parts.netloc, parts.path, urlencode(query), parts.fragment))
        from playwright.sync_api import sync_playwright
        receipt = {'version': 1, 'createdAt': datetime.now(timezone.utc).isoformat(),
                   'source': {'path': str(source_path), 'sha256': source_hash}, 'views': []}
        out.parent.mkdir(parents=True, exist_ok=True)
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(headless=True)
            try:
                page = browser.new_page(viewport={'width': args.width, 'height': args.height})
                page.goto(url, timeout=args.timeout, wait_until='domcontentloaded')
                page.wait_for_function('() => window.__pcgReady === true', timeout=args.timeout)
                for view in views:
                    page.evaluate('''view => {
                        const api = window.__pcgReview;
                        api.stopAnimation(); api.setExplode(0); api.setCamera(view);
                    }''', view)
                    # Let React/camera application and the WebGL frame complete.
                    page.evaluate('() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))')
                    captured = page.evaluate('''size => {
                        const api = window.__pcgReview;
                        const evaluation = api.getEvaluationEvidence();
                        const before = JSON.stringify(evaluation);
                        const frame = api.capture(size);
                        if (!frame?.pngBase64) throw new Error('No rendered PNG');
                        if (JSON.stringify(api.getEvaluationEvidence()) !== before)
                            throw new Error('Evaluation changed during capture');
                        return {evaluation, camera: api.camera, frame};
                    }''', {'width': args.width, 'height': args.height})
                    validate_evaluation(captured['evaluation'])
                    if captured['evaluation']['sourceGraph'] != source:
                        raise ValueError('Review page source differs from --graph; reload before capturing')
                    frame = captured['frame']
                    png = base64.b64decode(frame.pop('pngBase64'), validate=True)
                    if not png.startswith(b'\x89PNG\r\n\x1a\n'):
                        raise ValueError('Capture API did not return a PNG')
                    screenshot = out.with_name(f'{out.stem}_{view}.png')
                    with screenshot.open('xb') as stream:
                        stream.write(png)
                    receipt['views'].append({
                        'viewId': view, 'evaluation': captured['evaluation'],
                        'camera': captured['camera'],
                        'render': {'width': args.width, 'height': args.height, 'quality': 'full',
                                   'shading': query.get('shading', 'material'),
                                   'animation': 'stopped', 'explode': 0, 'capture': frame},
                        'screenshot': {'path': str(screenshot), 'sha256': digest(screenshot)},
                    })
            finally:
                browser.close()
        if digest(source_path) != source_hash:
            raise ValueError('Graph changed during capture; no acceptance receipt written')
        with out.open('x', encoding='utf-8') as stream:
            json.dump(receipt, stream, indent=2, ensure_ascii=False, allow_nan=False)
            stream.write('\n')
        print(str(out))
        return 0
    except Exception as error:
        print(f'error: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
