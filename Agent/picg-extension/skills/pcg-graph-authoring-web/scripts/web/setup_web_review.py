#!/usr/bin/env python3
"""Build the review URL for a .pcg graph using the running Vite dev server.

The Vite dev server hosts a /review route that:
  1. Loads the .pcg file from disk via GET /api/load-graph?path=...
  2. Cooks it via pcg-server through the /api/cook proxy
  3. Renders the result in a clean PreviewViewport (same Three.js as the editor)
  4. Sets window.__pcgReady = true when render is complete

This script resolves the graph path relative to the workspace root and
returns the full review URL. Use capture_webview_png.py to screenshot it.

Requires the Vite dev server to be running (cd web/pcg-editor && npm run dev)
and pcg-server to be reachable (./scripts/run-pcg-server.sh).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

DEFAULT_VITE_URL = "http://127.0.0.1:5173"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graph", type=Path, help=".pcg graph file to review")
    parser.add_argument("--slug", default="", help="Review slug (default: graph stem)")
    parser.add_argument("--vite-url", default=DEFAULT_VITE_URL, help="Vite dev server base URL")
    parser.add_argument("--workspace", type=Path, default=None,
                         help="Workspace root for path resolution (default: auto-detect)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    graph_path = args.graph.resolve()
    if not graph_path.is_file():
        print(f"ERROR: graph file not found: {graph_path}", file=sys.stderr)
        return 1

    slug = args.slug.strip() or graph_path.stem

    # Resolve graph path relative to workspace root
    # The Vite /api/load-graph endpoint resolves relative to the workspace root
    # (parent of web/pcg-editor/).
    if args.workspace:
        workspace = args.workspace.resolve()
    else:
        # Auto-detect: walk up from graph file to find a directory containing web/pcg-editor/
        workspace = graph_path.parent
        for parent in [graph_path.parent, *graph_path.parent.parents]:
            if (parent / "web" / "pcg-editor").is_dir():
                workspace = parent
                break

    try:
        rel_path = graph_path.relative_to(workspace)
    except ValueError:
        # Graph is outside workspace — use absolute path
        rel_path = graph_path

    review_url = f"{args.vite_url}/review?graph={str(rel_path)}"

    if args.json:
        print(json.dumps({
            "reviewUrl": review_url,
            "slug": slug,
            "graphPath": str(graph_path),
            "relativePath": str(rel_path),
            "workspace": str(workspace),
        }, indent=2))
    else:
        print(f"review URL: {review_url}")
        print(f"slug: {slug}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
