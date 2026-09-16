# Web visual review

Use live `pcg_capture_preview` for rapid edit feedback. Use the saved graph's clean `/review` route for reproducible visual acceptance; an editor screenshot with chrome or unrelated content is not equivalent evidence.

## Prepare the target

Confirm the intended graph was saved and validated. The Vite review page loads it through `/api/load-graph`, cooks through `/api/cook` (pcg-server proxy) and renders the same PreviewViewport used by the editor. Default endpoints are Vite `http://127.0.0.1:5173` and pcg-server `http://127.0.0.1:17890`; honor discovered configuration.

Check service health once when establishing the review or after a failure. If permitted, start `npm run dev` from `web/pcg-editor` and `./scripts/run-pcg-server.sh` from the repository root using the host's supported persistent process mechanism. Do not assume a particular agent shell API. If unavailable, report visual review as blocked, not passed.

Use [setup/capture commands](scripts.md) to construct an encoded `/review?graph=...` URL. Set the camera, object front axis and side view explicitly when comparing references. `shading=solid` is geometry inspection; material acceptance uses `shading=material`. Adaptive SDF quality is for iteration; final-quality evidence uses `quality=full`.

## Capture and inspect

Wait for `window.__pcgReady` and successful cook state before capturing `.pcg-preview__canvas canvas`. Keep `window.__pcgReview` camera settings/receipts with the render. Match each required reference view, and include an integrity view when depth or assembly needs checking. Do not manufacture a three-view requirement for a single-image task.

Compare current output with its corresponding archived source. Check silhouette, scale, openings, thickness, assembly and the requested surface appearance. Re-capture after changes that invalidate the evidence; an old screenshot cannot validate a new graph. Scripts produce files and structural checks, not visual scores. Preserve per-view evidence when using the structured-plan helpers.

## Exported assets

The `/review` route renders the saved source graph, not automatically the downloaded GLB. For a complete-asset job, export with [export_web_asset.py](scripts/web/export_web_asset.py), then reopen and inspect the actual deliverable in the target renderer. Report this separately from source-graph review. If no export viewer is available, mark reload/visual export checks unverified rather than reusing a source screenshot as proof.

A transport/cook/capture failure needs diagnosis at that layer. Decode saved PCGR errors with [parse_pcgr.py](../shared/pcg-scripts/parse_pcgr.py) where relevant. Report graph path, seed, review quality/cameras, evidence paths and any failed/unavailable check.
