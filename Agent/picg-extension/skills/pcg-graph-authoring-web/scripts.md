# Orchestration scripts cheatsheet (web)

All scripts are pure Python 3.10+ **stdlib** (PNG via `struct`/`zlib`; macOS `sips` for non-PNG`).
Web review scripts may require Playwright (`pip install playwright && playwright install chromium`).
Run from anywhere; prefer absolute paths. Non-zero exit = gate failed.

**Division of labor:** scripts enforce plan structure and package evidence; they **never** score
visuals. Agent vision inspects the comparison sheet and supplies fidelity / action.

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
python3 ../shared/pcg-scripts/new_authoring_plan.py "Ghost Protocol Glock" \
  --image /path/ref.png --complexity complex \
  --pcg examples/ghost-protocol-glock.pcg \
  --out examples/ghost-protocol-glock-plan.json --force
```

### archive_reference.py (P0 — run immediately after new_authoring_plan.py)

```bash
python3 ../shared/pcg-scripts/archive_reference.py \
  --image /path/ref.png \
  --plan examples/ghost-protocol-glock-plan.json
```

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
  --reference /path/ref.png \
  --render /path/Webview.png \
  --out /tmp/cmp.png
```

### append_review.py

```bash
python3 ../shared/pcg-scripts/append_review.py plan.json \
  --pass-id blockout \
  --fidelity 0.72 \
  --action continue \
  --summary "Macro silhouette OK; grip taper still boxy" \
  --reference-screenshot examples/ref_ghost-protocol-glock.png \
  --render-screenshot /path/Webview.png \
  --comparison-image /tmp/cmp.png \
  --ai-vision-score 0.72 \
  --ai-vision-notes "slide stepped sight reads; grip candy gradient flat" \
  --mismatches "grip taper" \
  --in-place
```

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
  --slug ghost-protocol-glock --json
```

Resolves the graph path relative to the workspace root and returns the review URL:
`http://localhost:5173/review?graph=<relative-path>`

The Vite dev server's `/review` route loads the graph, cooks it via pcg-server,
and renders it in a clean `PreviewViewport` (same Three.js as the full editor).

### capture_webview_png.py

```bash
python3 scripts/web/capture_webview_png.py "<review-url>" \
  --out screenshots/Webview_2026-08-08_14-00-00.png
```

Opens the review URL in headless Chromium via Playwright, waits for
`window.__pcgReady`, then captures the WebGL canvas as a PNG.

Requires: `pip install playwright && playwright install chromium`

## Suggested agent loop

```text
0. Start Vite + pcg-server (Shell block_until_ms: 0) → check_server.py (repeat every cycle)
1. new_authoring_plan.py → archive_reference.py (reference to local disk, plan rebound)
2. Layered observation → write observation.layers + visualTokens INTO plan.json
3. vault_search + rule_search → record localRuleHits in plan
4. validate_plan.py --strict-quality
5. report_pass.py --resume → unlock current pass (+ RESUME.md)
6. Auto params + auto saveDir → author .pcg for that pass (no ask_user)
7. layout_pcg.py → relayout root + every inline Subgraph definition
8. validate_pcg.py --check-server http://127.0.0.1:17890
9. setup_web_review.py → review URL (fixed route; no ask)
10. capture_webview_png.py "<review-url>" → screenshots/Webview_<stamp>.png
11. make_comparison_sheet.py --reference <archived ref_<slug>> --render …
12. Agent vision → append_review.py (one action; reference + vision notes mandatory)
13. If fidelity < 0.9 or DoD unmet → refine-* → goto 6 (no user gate)
14. Else report_pass.py --resume → next pass / stop at ceiling (final stop ≥ 0.9)
```

Do **not** insert "confirm params / save dir / continue?" between steps 5–13.
After any context compaction: read `<plan-stem>-RESUME.md` → plan.json → archived reference → latest cmp sheet, then continue.
