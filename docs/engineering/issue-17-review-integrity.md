# Issue #17: Boolean execution and reference-review integrity

This change fixes execution/acceptance infrastructure. It does **not** claim to
reconstruct the original scifi-crate or demonstrate visual reference fidelity.
The 92,702-byte investigation fixture and original reference were not available
in this checkout. Keep #17 open for its construction and visual-validation work.

## Runtime contract

`BooleanMesh` emits one node diagnostic per invocation: `success`, `noop`,
`empty`, `degraded`, or `failure`. It records node identity, reason and effective
Boolean settings. `noop` is conservative (identical surface or strictly separated
solid subtraction); equal vertex counts or equal volume are not proof of a no-op.
It is not a general redundant-cutter detector.

A successful empty intersection/subtraction remains empty, even with
`onFailure=passthroughA`. Typed empty operands compose through subsequent ordinary
union/intersection/subtraction. Missing inputs and cancellation are not preview
fallbacks. Self/shatter semantics continue through the kernel.

Explicit `passthroughA` remains usable for exploratory previews but is reported
as degraded and not execution-acceptable. A degraded node and all later nodes
in that cook are not cached. Successful cached Booleans retain their diagnostic;
ForEach keeps every invocation, not only the last result for a node ID.

Core result JSON adds:

```json
{
  "diagnostics_version": 1,
  "cook_outcome": "success",
  "fallback_used": false,
  "execution_acceptable": true,
  "node_diagnostics": [],
  "evaluation_seed": 42,
  "evaluation_graph": {"nodes": [], "edges": []}
}
```

`evaluation_graph` records the actual native graph node data and connections,
not a file path or exposed-default guess. It increases JSON payload size. The
existing mesh C ABI and server PCGR envelope transport this JSON unchanged.
Hard failures keep their native error return and a node-qualified error message;
the internal executor also retains failure diagnostics. Legacy C ABI error and
binary-point-only paths do not yet expose the full structured failure receipt.
Consumers that discard the JSON cannot certify acceptance and fail closed.

## Parameters and Review page

Preview overrides update node data **and** defaults in the effective graph,
without mutating the authored graph. Values are type/finite checked; duplicate
IDs, ambiguous bindings, and orphan targets fail explicitly. Vector values are
copied. Numeric slider ranges are not promoted to structural coupling guarantees.

The Review page displays default/baked conflicts and blocks final review/export
until the source is explicitly synchronized (for example with Save defaults).
Parameter changes invalidate capture evidence immediately, before debounce;
obsolete load/cook responses cannot restore an earlier evaluation. Save defaults
checks for an already-changed source before writing and recooks after success.
That check is **not atomic compare-and-swap**: simultaneous writers between the
read and write still require a server-side revision/ETag contract.

`window.__pcgReview.getEvaluationEvidence()` returns the captured source, effective
graph, native cook JSON, seed and resolution status. It rejects pending, degraded,
legacy/unverified, parameter-conflicting, or reduced-resolution evaluations.
Diagnostic screenshots and interactive preview remain available; they are not
acceptance receipts. Full GLB export checks execution quality, not visual fidelity.

## Capture and acceptance

Rebuild/restart `pcg-server` first. A running old binary cannot produce version-1
diagnostics. The normal Web editor and Playwright Chromium are prerequisites.

From the repository root:

```sh
SCRIPTS=.agents/skills/shared/pcg-scripts
python "$SCRIPTS/capture_review_receipt.py" \
  'http://localhost:5173/review?graph=examples/my-asset.pcg' \
  --graph examples/my-asset.pcg --out screenshots/my-asset-r1.json
```

The command uses full-quality, deterministic front/side/top/three-quarter views,
stops animation, resets explode, captures the WebGL PNG through the API, and
writes SHA-256 bindings. It never accepts an error-page screenshot as fallback.
Use a new output prefix per revision. Source changes during capture fail; partial
PNGs may remain for debugging but no successful receipt is written.

Supply its JSON to `append_review.py --evaluation-receipt`. Existing reference,
render, comparison and per-view evidence arguments remain required. Visual
`continue` additionally requires named feature reviews, for example:

```json
[
  {"featureId":"silhouette", "required":true, "status":"matched", "evidence":"Compared the required orthographic outlines."},
  {"featureId":"lid-seam", "required":true, "status":"mismatch", "evidence":"The seam is still a full-body gap; repair before continue."}
]
```

This example **must fail** because a required feature remains unresolved. Status
can be `matched`, `mismatch`, or `not-applicable`; optional features may use the
latter two with an explanation. At least one required feature must be reviewed.
Review camera receipts must match captured cameras; required views cannot reuse
one PNG. The script rechecks graph/PNG hashes, actual effective graph and seed,
all Boolean diagnostics (including earlier loop fallback), full resolution and
local reference/comparison files. Scores must be finite. Reference/comparison
hashes and the receipt hash are stored with the review entry.

These are consistency checks, **not** authenticated provenance or an independent
pixel scorer. Agent scores and feature judgments remain assertions, explicitly
labelled as such. External texture/mesh file contents and native binary versions
are not content-hashed here. Flattened/subgraph evaluation must match the provided
effective graph; unsupported expansion mismatches are rejected, not guessed.
Historical reviews are not rewritten. Non-visual passes and revise/replan actions
do not require a new visual receipt.

## Regression checks and deployment

```sh
node web/pcg-editor/scripts/validate-review-evidence.mjs
python -m unittest discover \
  -s .agents/skills/shared/pcg-scripts/tests -p test_review_receipt.py -v
# Use the project's existing native build configuration:
cmake --build <core-build-dir> --target test_boolean_graph
ctest --test-dir <core-build-dir> -R 'test_boolean_graph|test_boolean_bevel_chain' --output-on-failure
```

The native test extends the existing CMake target with strict/fallback budget
cases, valid empty/no-op, empty chaining, cancellation, repeated cache cooks,
ForEach invocation preservation and mesh C ABI diagnostic transport.

Before merge/deployment, run the full affected build/contracts, restart the
server, perform a real multi-view capture, and reload the exported asset. Native
compilation and live Web/Unity rendering were not available in the chat repair
environment; passing dependency-light tests is not a substitute.

## Remaining #17 work

Repair the original shell/lid/panel relationships, redundant contained cutters,
34 mm slice, handle/floor/guard attachments and prototype orientation using the
original fixture. Couple body controls to dependent components, or remove unsafe
claims of whole-asset resizing. Reproduce seam/topology quality with measurable
geometry checks and the original reference. Do not infer any of those outcomes
from successful cooking, this PR, or a scalar self-score.
