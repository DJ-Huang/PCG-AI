# Orchestration scripts cheatsheet (web)

All scripts are pure Python 3.10+ **stdlib** (PNG via `struct`/`zlib`; macOS `sips` for non-PNG`).
Web review scripts may require Playwright (`pip install playwright && playwright install chromium`).
Run from anywhere; prefer absolute paths. Non-zero exit = gate failed.

**Division of labor:** scripts enforce plan structure and package evidence; they **never** score
visuals and **never** emit the product graph. Agent vision inspects comparison sheets. Agent MCP
creates nodes and wires on the open Web editor page.

Shared scripts: `../shared/pcg-scripts/` (generic Python helpers, shared across Unity/web variants).
Web review scripts: `scripts/web/` (this skill directory).

**Web gate (P0):** before any visual `continue`, follow [web-review.md](web-review.md) — check Vite dev server + pcg-server → `/review` route → Playwright screenshot.

## Prerequisites

The PCG web editor must be running:

```bash
cd web/pcg-editor && npm run dev   # Vite dev server at :5173
./scripts/run-pcg-server.sh         # pcg-server at :17890
```

## Shared scripts (from `../shared/pcg-scripts/`)

### new_authoring_plan.py

```bash
python3 ../shared/pcg-scripts/new_authoring_plan.py "Cabin" \
  --front /path/front.png --side /path/side.png --top /path/top.png \
  --complexity complex \
  --pcg examples/cabin.pcg \
  --out examples/cabin-plan.json --force
```

Single-image jobs still use `--image`. `--front/--side/--top` must be supplied together.
After the first attached image, ask the user in order for front → side → top (see `triview.md`); do not crop a composite board.

### archive_reference.py (P0 — run immediately after new_authoring_plan.py)

```bash
python3 ../shared/pcg-scripts/archive_reference.py \
  --plan examples/cabin-plan.json --from-plan --require-triview
```

Or label views explicitly: `--view front=... --view side=... --view top=... --require-triview`.
Single-image: `--image /path/ref.png --plan …` (omit `--require-triview`).

### validate_plan.py

```bash
python3 ../shared/pcg-scripts/validate_plan.py plan.json --strict-quality
```

### report_pass.py

```bash
python3 ../shared/pcg-scripts/report_pass.py plan.json --resume
```

### orchestrate_passes.py

```bash
python3 ../shared/pcg-scripts/orchestrate_passes.py status plan.json
python3 ../shared/pcg-scripts/orchestrate_passes.py check plan.json --pass-id blockout
```

### make_comparison_sheet.py

```bash
python3 ../shared/pcg-scripts/make_comparison_sheet.py \
  --reference /path/ref_cabin_front.png \
  --render screenshots/cabin_front.png \
  --view-id front \
  --out screenshots/cmp_cabin_front.png
```

### append_review.py

Triplet visual `continue` (worst required view unlocks the pass):

```bash
python3 ../shared/pcg-scripts/append_review.py plan.json \
  --pass-id blockout \
  --fidelity 0.91 \
  --action continue \
  --summary "Front/top lock; side depth still short" \
  --view-evidence-json views.json \
  --in-place
```

`views.json` is an array of `{viewId, referenceScreenshot, renderScreenshot, comparisonImage, aiVisionScore, aiVisionNotes, cameraReceipt}`. Single-image jobs still use `--reference-screenshot` / `--render-screenshot` / `--comparison-image` / `--ai-vision-notes`.

### layout_pcg.py & validate_pcg.py

```bash
python3 ../shared/pcg-scripts/layout_pcg.py path/to/graph.pcg --out graph-layout.pcg
python3 ../shared/pcg-scripts/validate_pcg.py graph-layout.pcg
python3 ../shared/pcg-scripts/validate_pcg.py graph-layout.pcg --check-server http://127.0.0.1:17890
```

`--check-server` POSTs the graph to pcg-server `/v1/validate` after local manifest checks — catches **Unknown node type** when the running binary is stale (rebuild after pcg-core changes).

Warns on: `ImportMesh` with empty `path` (placeholder), `ProjectTexture` with unconnected `texture` pin.

### parse_pcgr.py

```bash
META='{"seed":42,"api_version":1}'
curl -sS -X POST http://127.0.0.1:17890/v1/cook \
  -F "meta=<(printf '%s' "$META");type=application/json" \
  -F "graph=@graph.pcg;type=application/json" \
  -o /tmp/cook.pcgr
python3 ../shared/pcg-scripts/parse_pcgr.py /tmp/cook.pcgr
python3 ../shared/pcg-scripts/parse_pcgr.py /tmp/cook.pcgr --json
```

Decodes the PCGR cook envelope (ported from `web/pcg-editor/src/cookResult.ts`). Use instead of manual `xxd` + struct probing.

## Web-specific scripts (`scripts/web/`)

### check_server.py

```bash
python3 scripts/web/check_server.py
python3 scripts/web/check_server.py --json
```

Checks both Vite dev server (:5173) and pcg-server (:17890). Exit 0 = both healthy.

### setup_web_review.py

```bash
python3 scripts/web/setup_web_review.py path/to/graph.pcg \
  --slug cabin --camera front --front-axis +z --side-view right \
  --quality adaptive --json
```

Resolves the graph path relative to the workspace root and returns the review URL:
`http://localhost:5173/review?graph=<relative-path>`

The Vite dev server's `/review` route loads the graph, cooks it via pcg-server,
and renders it in a clean `PreviewViewport` (same Three.js as the full editor).
Adaptive is the browser-safe default for SDF assets; final pixel acceptance
must pass `--quality full`. GLB export always runs a separate full-quality cook.

### capture_webview_png.py

```bash
python3 scripts/web/capture_webview_png.py "<review-url>" \
  --cameras front,side,top,three-quarter \
  --front-axis +z --side-view right \
  --quality full \
  --slug cabin --out-dir screenshots --json
```

Opens the review URL in headless Chromium via Playwright, waits for
`window.__pcgReady`, switches `window.__pcgReview.setCamera`, then captures the
WebGL canvas (not editor chrome) as a PNG. Keep each `cameraReceipt`.

Requires: `pip install playwright && playwright install chromium`

### export_web_asset.py

```bash
python3 scripts/web/export_web_asset.py "<material-review-url>" \
  --out examples/cabin/cabin.glb --json
```

Re-cooks the saved graph in the clean review page and downloads the resulting
materialized GLB. The export preserves mesh attributes, material slots/PBR
factors, a stable root hierarchy, and the source `.pcg` path in glTF extras.

## Suggested agent loop

```text
0. Start Vite + pcg-server (Shell block_until_ms: 0) → check_server.py (repeat every cycle)
1. After the first image, ask front → side → top unless already supplied; then new_authoring_plan.py (--front/--side/--top when given) → archive_reference.py --from-plan
2. Layered observation → write observation.layers + viewObservations + crossViewConstraints + visualTokens INTO plan.json
3. pcg_kb_search (rules + kb, including pcg/triview) → record localRuleHits in plan
4. validate_plan.py --strict-quality
5. report_pass.py --resume → unlock current pass (+ RESUME.md)
6. Auto params + auto saveDir → author CURRENT PASS on the live Web page via PCG MCP (nodes + edges). Forbidden: generator script / full .pcg Write
7. pcg_validate / pcg_cook / pcg_capture_preview, then pcg_save_graph; layout_pcg.py on the saved file; push positions back via MCP
8. validate_pcg.py --check-server http://127.0.0.1:17890
9. setup_web_review.py → review URL (saved-file ortho lock)
10. capture_webview_png.py --cameras front,side,top,three-quarter (or one camera for a single-image job)
11. make_comparison_sheet.py --view-id <view> --reference <archived ref> --render …
12. Agent vision per view → append_review.py (--view-evidence-json on triplets)
13. If worst required view < pass threshold or DoD unmet → refine-* **on the same MCP page** → goto 6 (no user gate)
14. Else report_pass.py --resume → next pass. Do not stop the job until FINAL_ACCEPTED, 12-cycle plateau, or GAP.
```

Do **not** insert "confirm params / save dir / continue?" between steps 5–13.
Do **not** write a custom script to emit the product graph.
After any context compaction: read `<plan-stem>-RESUME.md` → plan.json → archived reference → latest cmp sheet, then continue on the live MCP page.
