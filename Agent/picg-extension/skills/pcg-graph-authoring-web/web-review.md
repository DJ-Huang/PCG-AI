# Web review gate (P0 — mandatory)

This skill uses the **running PCG web editor** (Vite dev server) as its review target — the same way the Unity skill uses the running Unity editor via MCP. Visual validation **must** go through the Vite dev server's `/review` route and the pcg-server cook API.
Do **not** judge fidelity from a cluttered editor page, a random screenshot, or memory.

Cheatsheet: [scripts.md](scripts.md) · web templates: [`scripts/web/`](scripts/web/)

## Architecture

```text
Skill scripts ──→ Vite dev server (:5173)
                     ├── /review?graph=<path>    ← clean ReviewPage (PreviewViewport only)
                     ├── /api/load-graph          ← reads .pcg from disk
                     └── /api/cook (proxy) ──→ pcg-server (:17890) ──→ PCGR binary
```

The Vite dev server is the review target. It proxies cook requests to pcg-server and renders results using the same `PreviewViewport.tsx` Three.js component as the full editor. The `/review` route loads a graph from disk, cooks it, and renders only the viewport — no editor UI, no other graphs.

PCG MCP may create, edit, validate, cook, capture, and save the live graph
before this gate. This gate deliberately reloads the saved `.pcg` from disk;
live Preview evidence does not replace saved-deliverable acceptance.

## Hard rules

1. **Verify both Vite dev server and pcg-server are running before any cook / screenshot / visual judgment.**
2. **Always use the `/review` route** — it renders only the graph under review (+ studio light + camera). Do not screenshot the main editor page.
3. **Screenshot the WebGL canvas from the review route**, then `make_comparison_sheet.py --view-id` vs the archived reference for **that same view**.
4. Scripts never score visuals — agent vision does, after each sheet exists. Unlock score = **worst required view**.
5. If either server is unreachable: **stop and ask the user** to start them. Do not guess.
6. Do not treat a live `pcg_capture_preview` or a single 3/4 shot as a substitute for front/side/top.

## Connect protocol (every session / every review cycle)

```text
1. python3 scripts/web/check_server.py
   → must report both Vite and pcg-server OK before any cook
   → re-run before EVERY cook/screenshot cycle (servers can die between rounds)
2. python3 ../shared/pcg-scripts/validate_pcg.py <graph>.pcg --check-server http://127.0.0.1:17890
   → manifest-server parity: running pcg-server must accept all node types in the graph
   → catches stale pcg-server binary (Unknown node type) before wasting a cook round
3. If Vite unreachable → start `cd web/pcg-editor && npm run dev` with Shell `block_until_ms: 0`, then re-check. Ask the user only if it still fails.
4. If pcg-server unreachable → start `./scripts/run-pcg-server.sh` with Shell `block_until_ms: 0`, then re-check. Ask only if it still fails.
5. Bind the live editor: `pcg_get_editor_context`. Authoring happens on that page via MCP. `/review` is saved-file ortho capture.
6. Record endpoints in the receipt.
```

### Starting long-lived servers (P0 — do not use nohup/disown)

Use the agent Shell tool with `block_until_ms: 0` so Vite and pcg-server stay alive across tool rounds:

```text
Shell: cd web/pcg-editor && npm run dev          (block_until_ms: 0)
Shell: ./scripts/run-pcg-server.sh               (block_until_ms: 0)
Shell: python3 scripts/web/check_server.py       (verify both OK)
```

Do **not** start servers with `&`, `nohup`, or `disown` in a disposable shell — they die when the session ends and cause `ERR_CONNECTION_REFUSED` on later screenshots.

Default endpoints:
- Vite dev server: **`http://127.0.0.1:5173`**
- pcg-server: **`http://127.0.0.1:17890`**

Receipt line after connect (required in the agent reply):

```text
Web editor: Vite=<url:port> OK | pcg-server=<url:port> OK
```

If either check fails: report `Web editor: unavailable` and **do not** claim visual pass/`continue` on a visual build pass.

## Clean review page (no interference)

### Fixed review route (P0 — never ask)

| Item | Value |
|------|-------|
| Review URL | `http://localhost:5173/review?graph=<workspace-relative-path>&camera=front|side|top|three-quarter&frontAxis=+z&sideView=right` |
| Screenshot | `screenshots/<slug>_<view>.png` (canvas only) |
| Comparison | `screenshots/cmp_<graph-slug>_<view>.png` |

The `/review` route is a dedicated React component (`ReviewPage.tsx`) that:
1. Reads the `?graph=` query param (workspace-relative path to `.pcg` file)
2. Fetches the graph JSON via `GET /api/load-graph?path=...`
3. Cooks it via `POST /api/cook` (proxied to pcg-server)
4. Renders only `PreviewViewport` (same Three.js scene, lighting, and coordinate conversion as the full editor)
5. Sets `window.__pcgReady = true` and `window.__pcgReview` when the framed render is complete (for Playwright camera switches)

**Do not** `ask_user` for the review URL. **Do not** use the main editor page unless the user explicitly overrides.

### Procedure

```text
A. python3 scripts/web/check_server.py  (verify Vite + pcg-server — repeat every cycle)
B. python3 ../shared/pcg-scripts/validate_pcg.py <graph>.pcg --check-server http://127.0.0.1:17890
C. python3 scripts/web/setup_web_review.py <graph>.pcg --slug <slug> --json
   → returns review URL
D. python3 scripts/web/capture_webview_png.py "<review-url>" --cameras front,side,top,three-quarter --slug <slug> --out-dir screenshots --json
   → Playwright waits for __pcgReady, setCamera each view, captures `.pcg-preview__canvas canvas`
E. python3 ../shared/pcg-scripts/make_comparison_sheet.py --reference ref_<slug>_<view> --render screenshots/<slug>_<view>.png --view-id <view> --out screenshots/cmp_<slug>_<view>.png
F. Agent vision on each sheet → append_review.py (--view-evidence-json for triplets)
```

If cook returns a PCGR binary with an error, decode without manual `xxd`:

```bash
META='{"seed":42,"api_version":1}'
curl -sS -X POST http://127.0.0.1:17890/v1/cook \
  -F "meta=<(printf '%s' "$META");type=application/json" \
  -F "graph=@<graph>.pcg;type=application/json" \
  -o /tmp/cook.pcgr
python3 ../shared/pcg-scripts/parse_pcgr.py /tmp/cook.pcgr
```

### Naming

| Item | Pattern |
|------|---------|
| Review URL | `http://localhost:5173/review?graph=<path>&camera=<view>&frontAxis=+z&sideView=right` |
| Reference archive | `ref_<graph-slug>_<view>.<ext>` next to `*-plan.json` (via `archive_reference.py`; never a URL/chat attachment) |
| Screenshot | `screenshots/<slug>_<view>.png` |
| Comparison | `screenshots/cmp_<graph-slug>_<view>.png` |

`<graph-slug>` = `.pcg` basename without extension (e.g. `ghost-protocol-glock`).

### Forbidden shortcuts

| Anti-pattern | Why |
|--------------|-----|
| Ask where to create the review URL | Route is fixed: `/review?graph=...` |
| Screenshot the editor's main page with UI/other graphs loaded | Other meshes and UI dominate framing |
| Score from an old screenshot without recook | Stale geometry |
| `continue` on visual pass without per-required-view comparison sheets + server health | Violates orchestration gate |
| Average three view scores so a failed side/top still `continue`s | Worst required view is the unlock |
| Use a 3/4 perspective shot as the only evidence | Integrity check only; cannot lock width/height/depth |
| Feed the comparison sheet a URL / chat attachment as reference | Not durable; archive first (`archive_reference.py`) |
| Cook without verifying Vite + pcg-server health first | Silent failure or stale cache |
| Skip `validate_pcg.py --check-server` before first cook | Stale pcg-server binary → Unknown node type after wasted cook |
| Start servers with `nohup`/`&`/`disown` | Process dies between tool rounds → ERR_CONNECTION_REFUSED |

## Interaction with authoring passes

| Pass | Servers required? |
|------|---------------------|
| `module-plan` / `reference-calibration` | No (plan/docs only) |
| `blockout` … `cross-view-geometry-lock` | **Yes** — every required view + sheet before `continue` |
| `parameters` | Yes if visual defaults change |
| `validation` | `validate_pcg.py` + cook smoke if graph ships in web |

`append_review.py` requires `--view-evidence-json` (every required view, each with `cameraReceipt`) for multi-view visual-pass `continue`. Single-image jobs still use `--reference-screenshot` (archived file), `--render-screenshot`, `--comparison-image`, and `--ai-vision-notes`.

## Minimal tool sequence (copy)

```text
Shell: python3 scripts/web/check_server.py
Shell: python3 scripts/web/setup_web_review.py <graph>.pcg --slug <slug> --json
Shell: python3 scripts/web/capture_webview_png.py "<review-url>" --cameras front,side,top,three-quarter --slug <slug> --out-dir screenshots --json
Shell: python3 ../shared/pcg-scripts/make_comparison_sheet.py --reference … --render … --view-id <view> --out …
# agent vision per view → append_review.py --view-evidence-json
```
