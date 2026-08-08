# Code Review: `07-08-w1-cursor` vs `main`

## Scope

- Review mode: branch, `main...07-08-w1-cursor`
- Base (`main`): `ad3e6b97b0bc`
- Merge base: `6d41613445f3`
- Head: `d0926ab024f8`
- Size: 571 files, 64,141 insertions, 3,274 deletions
- Stack: C++ core/server, Unity C#, React/TypeScript/Vite, schemas, tests and Unity assets
- Rules loaded: `PCG AI Rule/Engineering/pcg-ai-development.md`; Vault PCG/Unity pitfalls were consulted. This is not an HMIRP project, so HMIRP Stable constraints do not apply.

## Change overview

The branch adds Web graph preview and an Agent panel, broadens native cook-result formats and preview support, adds Unity preview rendering and asynchronous Meshy/Tripo generation, extends import-like native nodes and dependency hashing, adds spline editing, and reorganizes the Unity demo assets. The critical shared contract is now Web/Unity client → `pcg-server` → process-global native cook execution.

## Findings

### F1 [Major] Fire-and-forget global cancellation can cancel the replacement cook or another client

- File: `web/pcg-editor/src/App.tsx:622`
- Trigger: a preview recook starts while a previous cook is active, the preview is closed, or two Web/Unity clients share one `pcg-server`.
- Impact: the replacement preview can fail nondeterministically as canceled; one editor can also cancel an unrelated Unity or browser cook. This is a reachable functional regression in normal debounced preview use.
- Evidence: `requestPreviewCook` aborts the browser request and starts `cancelCook()` without awaiting it (`App.tsx:622-624`), then immediately starts the new cook (`App.tsx:644`). `cancelCook()` sends no job identifier (`previewCook.ts:254-256`). The server ignores the cancel request body and always calls `pcg_request_cancel()` (`pcg-server/src/cook_service.cpp:504-507`), although it parses a `job_id` for cook requests (`cook_service.cpp:517-524`). Native cancellation targets whichever process-global `g_running_job` is active, or leaves a global pending flag (`pcg-core/src/pcg_core.cpp:1100-1106`); each serialized cook subsequently clears that global cancellation state (`cook_service.cpp:616-619`). Therefore network ordering determines which cook is canceled, and cancellation is not scoped to the initiating client/job.
- Fix: include the previous `job_id` in the cancel request, track the active request job in `pcg-server`, and only forward cancellation when IDs match. Await the scoped cancellation response before submitting the replacement cook so the cancel cannot overtake it. Keep browser `AbortController` cancellation for response disposal, but do not treat it as native job identity.
- Test: add a server integration test with a blocking/slow graph. Start job A, issue cancel(A), immediately submit job B, and assert A is canceled while B succeeds. Add a two-client variant where cancel(A) is sent while B is active and assert B is not canceled. The current implementation fails at least one ordering because `/api/cook-cancel` has no identity.

### F2 [Minor] Malformed heightfield payloads are reported as missing payloads

- File: `web/pcg-editor/src/previewCook.ts:208`
- Trigger: the server returns a non-empty PCGH blob with an unsupported version, truncated chunk, invalid dimensions, or other parse failure.
- Impact: the UI discards the parser exception and later tells the user the heightfield binary is *missing* and to restart the server (`previewCook.ts:231-237`). This hides a real binary-contract incompatibility/corruption and sends debugging in the wrong direction.
- Evidence: all `parseHeightFieldBinary` exceptions are caught and replaced with `null` (`previewCook.ts:208-213`), making malformed and absent blobs indistinguishable in the subsequent error branch.
- Fix: retain the parse error and return a distinct message such as `Invalid heightfield payload: ...`; reserve the current missing-binary message for `cook.heightfield.length === 0`.
- Test: unit-test a cook result whose JSON identifies a heightfield and whose PCGH data has an unsupported version/truncated layer; assert the preview reports an invalid payload rather than a missing payload.

## Confirmed dead code

No newly introduced, definitely unreachable code was confirmed. The branch contains feature scaffolding (notably the mock Agent service), but its routes and callers are reachable and therefore are not classified as dead code.

## Duplication, architecture and abstraction review

- The extracted native import-like execution path is reused by ImportMesh, Meshy and Tripo nodes and has a real shared contract; it does not appear to be gratuitous abstraction.
- Three node-manifest copies remain synchronized in this diff, consistent with the repository rule, but this remains a manual drift risk.
- The Agent action dispatcher and async Unity action controller each have distinct ownership/lifecycle responsibilities; no single-use wrapper was found that warrants a blocking finding.
- Cook cancellation is the principal architectural violation: a client/job-scoped UI operation crosses into process-global mutable state without preserving identity.

## Predicted risks / open questions

- Hundreds of Unity assets and `.meta` files were moved or regenerated. A representative old nonstandard GUID was absent from current serialized references, so GUID breakage was not promoted to a finding; a clean Unity import/open of all moved demo scenes is still required.
- Meshy/Tripo external API flows were reviewed statically only. Authentication-region fallback, long polling, cancellation latency during `Thread.Sleep`, downloads, and cache reuse need sandbox-account integration coverage.
- The Web production bundle is large (about 1.14 MB minified, 300.57 kB gzip) and Vite reports a >500 kB chunk warning. This is a performance budget question, not a correctness finding without an agreed load-time target.
- The new Agent client/server flow has no direct automated tests; attachment payloads are currently metadata-only and the server is explicitly a mock.

## Verification performed

- `npm run build` in `web/pcg-editor`: passed (`tsc -b && vite build`, 200 modules).
- `python3 scripts/validate-web-roundtrip.py`: passed all three fixtures.
- `python3 scripts/validate-cook-result-parse.py`: passed box and points fixtures.
- Rebuilt `test_assembly_nodes` and ran `ctest -R '^test_assembly_nodes$'`: passed.
- Rebuilt `pcg-server` and its core dependency: passed.
- Unity compilation, clean asset import, scene opening and live Meshy/Tripo calls were not run in this environment.

## Recommended test plan

1. Add the scoped cancellation race and cross-client integration tests described in F1.
2. Add malformed/unsupported PCGH parser tests described in F2.
3. Run Unity batch-mode compilation and edit-mode tests, then clean-import and open each moved demo scene to detect missing GUID references.
4. Run Meshy/Tripo happy path, API failure, cancel-during-poll and interrupted-download tests against sandbox accounts.
5. Add Agent client tests for success, 401/500, network fallback, abort, malformed actions and single-undo batching.

## Conclusion

**needs_changes**. F1 is a reachable cross-request correctness bug in the new live-preview path and should be fixed before merging. F2 is non-blocking but should be corrected while the binary-preview contract is being introduced.

## Re-review 2026-08-08

### Incremental scope

- Review base: previous review at HEAD `d0926ab024f8`
- Increment: current staged diff, 10 files, 460 insertions, 76 deletions
- Intent: scope Web and Unity cancellation by client `job_id`, add cancellation validation, preserve heightfield parse failures, and add a parser contract check

### Previous finding status

- **F1 [Major] remains open.** Clients now send a stable job ID and await cancellation, but the server-side lock makes matching cancellation unable to reach a running cook; see F3.
- **F2 [Minor] resolved.** `buildPreviewDataFromCook` preserves PCGH parsing errors and distinguishes malformed from missing heightfield payloads. The new contract check covers both partitions.

### F3 [Major] Cancel handler waits behind the cook it is supposed to interrupt

- File: `pcg-server/src/cook_service.cpp:531`
- Trigger: Web or Unity requests cancellation while a native cook is executing.
- Impact: cancellation cannot interrupt the active cook. Web preview replacement/close waits until the old cook finishes; Unity's synchronous cancel call times out after two seconds and its later wait can block up to 30 seconds. The original preview latency and cancellation problem therefore remains despite the job-ID protocol.
- Evidence: `HandleCook` acquires `g_cook_mutex` at line 651 and holds it across `pcg_execute_graph_v10` through line 725. `HandleCancel` must acquire that same mutex at line 531 before it can compare `g_active_client_job_id` or call `pcg_request_cancel` at line 533. The cancel thread can only enter after the cook clears the active ID and releases the mutex, so it returns `canceled:false`. Web now awaits this response before starting its replacement (`App.tsx:624-630`), turning the race into a wait for natural completion. Unity waits synchronously for the same blocked endpoint with a two-second timeout (`PcgCookClient.cs:192-202`).
- Fix: protect active job identity with a separate short-lived state mutex (or an atomic/immutable job token) that is not held during native execution. Publish the active ID immediately before execution, compare/cancel under the state lock, and clear it with identity checking after execution. Keep `g_cook_mutex` only for serializing native execution/cache access. Also define how cancellation of a parsed-but-queued job is handled so a superseded queued request does not execute later.
- Test: use a deterministic blocking/slow node or test barrier. Wait until job A is confirmed active, send cancel(A), and assert the cancel response returns promptly with `canceled:true` while A finishes with the native canceled result; then assert replacement B succeeds. The staged `validate-cook-cancel.py` does not establish this: its default graph completes in under the 50 ms delay, and the matching test neither asserts `canceled:true` nor asserts that A returned a canceled result, so it passed against the broken locking implementation.

### Incremental five-pass result

- Correctness/contract: job IDs propagate consistently through Web and the reviewed Unity async cook path; the server locking defect above blocks the intended behavior.
- Duplication/reuse: the optional job ID is threaded through existing cook layers without introducing a competing cancellation path.
- Architecture: separating native serialization from active-job state is required; one mutex cannot both surround a long cook and service its interrupt path.
- Abstraction cost: `buildPreviewDataFromCook` is justified by live parsing plus the new contract test.
- Bug prediction/tests: stale/wrong-job partitions are represented, but active matching cancellation needs deterministic synchronization and outcome assertions.

### Re-review verification

- `cmake --build pcg-server/build -j4`: passed.
- `npm run build` in `web/pcg-editor`: passed (200 modules; existing >500 kB bundle warning).
- `npx tsx scripts/preview-cook-parse-check.ts`: passed.
- `PCG_SERVER_PORT=17991 python3 scripts/validate-cook-cancel.py`: passed, but is a false-negative for F3 for the reasons above.
- `git diff --cached --check`: passed.
- Unity compilation was not run.

### Re-review conclusion

**needs_changes**. F2 is fixed, but F1 remains blocked by the new F3 server-locking defect. Do not merge until an active matching cancel can run concurrently with the cook and a deterministic regression test proves the interruption and replacement outcomes.
