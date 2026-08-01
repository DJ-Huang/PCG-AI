# PCG U6/U7 + Demo Hygiene Working-Tree Review

- Date: 2026-08-01
- Source: working tree at `dd72a10a9b5744a83a4e49b7b7a29cba892f44d0`
- Scope: staged/unstaged changes and non-ignored untracked files in `PCG-AI-cursor`
- Rules: `pcg/project-engineering`, `pcg/graph-contract`
- Related notes: `pit-multi-path-node-explicit-input-mode`, `pit-manifest-serializer-native-json-contract`, `pit-native-plugin-build-target-mismatch`
- Conclusion: **remediated_with_one_scope_note**

## Findings

### F1 [Resolved] Closed spline seam breaks when smoothing an explicit closing duplicate

- File: `pcg-core/src/elements/spline_algorithms.cpp:278-306`, `pcg-core/src/elements/spline_algorithms.cpp:444-455`
- Trigger: feed a closed `CreateSpline(mode=polyline)` into `ConditionOutline` with `win=3`. `CreateSpline` currently appends the first point as a closing duplicate in `pcg-core/src/geometry/spline_geometry.cpp:103-107`.
- Evidence: an HTTP cook of a closed square returned five points with first `{x: 0.6666666666666666, y: 0, z: 0}` and last `{x: 0, y: 0.3333333333333333, z: 0}`; the endpoints were not equal although `closed=true`.
- Impact: the output advertises a closed spline but has a discontinuous explicit seam. Downstream outline/solid consumers can receive a malformed ring or an unintended seam edge. This also conflicts with the plan contract that closed curves use `closed=true` without repeating the first point.
- Fix: normalize an explicit closing duplicate before circular smoothing/RDP, or process the logical `n-1` points and restore one documented representation consistently. Keep `protectSpans` index semantics explicit when normalizing.
- Regression: add a closed polyline `win=3, eps=0` case and assert `closed` plus seam equality/no duplicate according to the chosen contract. The current graph fixture uses `win=1`, so it does not catch this.

### F2 [Scope note] The crop review scene is in the Changes area despite being explicitly read-only

- File: `Unity/Assets/Scenes/PCGTest/PcgReview_classic-knife-crop.scene:403,755,850`
- Initial evidence: the diff contained `482` added and `7` deleted lines; the scene was about `9.97 MB`. The added serialized data included `m_LastResultJson`, a generated `PCG Preview`, an embedded `PCG Generated Mesh`, and a material subasset.
- Contract: the plan explicitly says not to modify this already-dirty scene and requires no new scene hunk for AC6/AC7.
- Impact: committing the remaining crop hunk without knowing the user's baseline could mix generated preview state with pre-existing scene edits and create a large stale binary-like YAML payload.
- Fix: separate the pre-existing dirty state from the plan execution and remove only the plan-generated scene additions; do not blindly discard the user's original changes. Re-run the final scope audit until this path has no new hunk attributable to the plan.

### F3 [Resolved] U7 screenshots contradict the recorded “no left/right flip” acceptance

- Initial evidence: `tmp/pcg-demos/classic-knife/shots/U7-front-reference-albedo.png` placed the handle on the right and the old `U7-sceneview-front.png` placed it on the left.
- File: `tmp/pcg-demos/classic-knife/shots/U7-notes.md:54-65`
- Impact: the checklist item “No left/right flip vs FRONT reference silhouette” and the conclusion that the front matches are not supported by the supplied evidence. AC7 is therefore not demonstrated.
- Fix: recapture with a documented camera/view orientation and a stable landmark check (handle side, blade tip, jimping), then update the notes only after the two images agree. Keep this as a U7 evidence correction rather than changing `ConditionOutline` or the knife `columnsJson` path speculatively.

### F4 [Resolved] Graph-level validation does not compare the tracked graph to the golden output

- Files: `pcg-core/tests/test_phase45_spline.cpp:261-325`; `tmp/pcg-demos/classic-knife/scripts/condition_outline_cli.py:96-146,214-232`
- Initial evidence: the C++ graph test checked only JSON presence, `closed`, and a point count of `7`. The Python CLI built a new graph from the golden case instead of cooking `examples/Test/test-condition-outline.pcg`; it also stripped one trailing duplicate before comparison.
- Impact: the tracked graph can drift in coordinates or topology while all current tests remain green. The permissive duplicate handling can also hide the seam representation problem in F1.
- Fix: compare the graph fixture's complete spline payload against an expected case, or make the CLI cook the tracked graph directly. Avoid silently trimming output unless duplicate-closing semantics are an explicit, tested contract.

## Validation performed

- `cmake --build pcg-core/build --target test_phase45_spline -j2` — pass.
- `ctest --test-dir pcg-core/build -R '^test_phase45_spline$' --output-on-failure` — pass.
- Python HTTP oracle — all seven golden cases pass; the explicit-duplicate case is the default.
- `python3 scripts/validate-manifest.py` — pass; 124 C++ elements and 124 manifest nodes.
- `validate_pcg.py` — both tracked graph copies pass.
- Unity MCP clean-scene cook — `13381` verts / `8600` tris; no Unity cook errors; front and edge screenshots captured.
- IDE lints for changed C++/manifest/doc files — no errors.
- `git diff --check` — pass after crop-scene cleanup.
- Deleted authoring GUIDs were searched in the working tree with no remaining references.

## Remediation applied

- F1: `ConditionOutline` now processes the logical `n-1` points when a closed input has an explicit closing duplicate. Input-index `protectSpans` are marked before normalization, the duplicate's protection maps to logical index `0`, and active processing emits `closed=true` without a duplicate endpoint. `closed_duplicate_smooth` covers smoothing plus cross-seam protection.
- F2: removed only the appended preview GameObject/mesh/material documents (`1190816601`, `1190816602`, `1190816603`, `1190816604`, `1190816605`, `1375199514`, `1466513444`) from the crop scene and removed its trailing whitespace. The original dirty crop payload was backed up under ignored `tmp/` and was not blindly reverted; the remaining existing `m_LastResultJson`/mesh hunk therefore still needs an explicit user scope decision before staging.
- F3: recaptured the U7 front and edge views in a new clean `PcgReview_classic-knife-u7-fix` scene with explicit review-only material bindings. The canonical front shot now places the handle on the right, matching the reference; `U7-comparison-fixed.png` records the comparison.
- F4: the C++ graph test now loads the tracked graph fixture and compares its complete spline payload against `closed_duplicate_smooth`. The Python oracle no longer strips a trailing duplicate and defaults to the explicit-duplicate regression case.

## Residual risks

- `tmp/` is intentionally ignored, so the CLI, U7 notes, screenshots, and moved authoring files are not part of the reviewable Git change. The tracked golden remains the reproducible contract.
- The clean U7 review scene is an 8.6 MB generated Unity YAML asset and is untracked until the user decides whether to keep it as rerunnable evidence.
- The crop scene still has a `73 added / 37 deleted` hunk after removing only the appended preview documents; its original baseline is unknown, so it is intentionally not reverted.

## Change summary

The implementation adds the C++ `ConditionOutline` algorithm and wrapper, synchronizes three manifests and the node reference, adds direct/graph fixtures and a shared golden, migrates classic-knife authoring files out of `Assets`, and records clean-scene U7 evidence. F1, F3, and F4 are fixed; F2's newly appended preview objects are removed, while the pre-existing dirty crop payload remains intentionally untouched pending scope confirmation.
