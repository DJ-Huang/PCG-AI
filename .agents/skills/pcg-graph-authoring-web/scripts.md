# Web helper commands

Shared planning, layout, validation and evidence commands are in [script reference](../shared/script-reference.md). Read only the helper needed now. Web capture/export uses Playwright and an installed Chromium browser; check the active environment before installing dependencies.

The following commands run from the repository root:

```bash
W=.agents/skills/pcg-graph-authoring-web/scripts/web
python3 "$W/check_server.py" --json
python3 "$W/setup_web_review.py" path/to/graph.pcg --slug cabin --quality full --json
```

Use discovered service endpoints/configuration when they differ from local defaults. Check health at setup or after a transport problem, not before every successful cook. The setup helper returns the encoded saved-graph review URL.

```bash
python3 "$W/capture_webview_png.py" "<review-url>" \
  --cameras front,side,top,three-quarter --front-axis +z --side-view right \
  --quality full --slug cabin --out-dir screenshots --json
```

Select cameras for the actual required views; do not request a triplet merely because the example shows one. Capture waits for review readiness and saves the WebGL canvas plus camera receipts. Use `adaptive` quality for bounded iteration when appropriate, and `full` for final-quality evidence.

```bash
python3 "$W/export_web_asset.py" "<material-review-url>" \
  --out path/to/cabin.glb --json
```

Export re-cooks the saved graph through the review page. Verify the downloaded file by reopening it; successful download alone does not establish geometry/material correctness. Use a fresh output path or an explicitly intended replacement.

See [Web review](web-review.md) for evidence requirements and [three-view reconstruction](triview.md) only when orthographic constraints apply. A command example is not a mandatory production sequence.
