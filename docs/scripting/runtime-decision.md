# Embedded JavaScript runtime validation (#21)

Parent: [#20](https://github.com/DJ-Huang/PCG-AI/issues/20). Scope:
[#21](https://github.com/DJ-Huang/PCG-AI/issues/21), not the production execution
or modeling bindings tracked by #24 and #26.

## Decision and acceptance status

**Provisional selection: QuickJS-NG 0.16.2.** GO for an isolated native validation
spike; **NO-GO for claiming production validation or closing #21 yet**. The
native platform matrix and measured overhead below are still outstanding.
A checked-in probe is not evidence that it passed.

The implementation intentionally lives under `pcg-core/tests/scripting/`. It does
not register a node, alter a manifest, install browser state, load arbitrary user
scripts, change the production Cook path, or expose a public scripting API.
The numeric `main(ctx)` return value is only a test fixture, not a replacement
for #20's named typed outputs. Normal Core/server builds do not acquire QuickJS.
No `.github` directory or hosted CI workflow is added; the validation command
is suitable for a local developer machine or a separately authorized CI runner.

## Reproducible dependency

| Item | Selection |
| --- | --- |
| Upstream | `https://github.com/quickjs-ng/quickjs` |
| Release | `v0.16.2` |
| Full Git commit | `1ab8676f4b6d6d669baeb5f21790fb9734636a20` |
| Integration | `cmake/PcgQuickJS.cmake`, CMake FetchContent, full commit pin |
| Dependency verification | Git object checkout, exact HEAD match, no tracked changes |
| Engine sources | `quickjs.c`, `dtoa.c`, `libregexp.c`, `libunicode.c` |
| Excluded | `quickjs-libc.c`, CLI, qjsc, examples, test262 submodule, package installation |
| Language/ABI | C11 engine, C++17 probes; PIC static engine embedded in static/shared adapters |
| MSVC CRT | `/MT`, or `/MTd` in Debug, matching the current Core CMake policy |
| JS parser | Enabled; portable source is evaluated, not persisted engine bytecode |

The upstream tag-to-commit mapping was checked on 2026-09-17. The immutable Git
object ID is the source-content lock; this integration does not download a
release archive and does not claim an unmeasured archive SHA-256. A local
FetchContent source override must also be a clean Git checkout of that commit.
A source ZIP without Git metadata is deliberately rejected rather than silently
bypassing verification. No system QuickJS package is substituted.

The engine-only source list and flags are based on the
[pinned upstream CMake file](https://github.com/quickjs-ng/quickjs/blob/1ab8676f4b6d6d669baeb5f21790fb9734636a20/CMakeLists.txt).
Using an isolated target avoids upstream CLI/libc targets and directory-wide
build options leaking into PCG. `_GNU_SOURCE`, `QUICKJS_NG_BUILD`, supported
`-funsigned-char`, platform definitions, threads, and the platform math library
are applied to that target. The MSVC C11 atomics flag is enabled when supported.
The parent project's `BUILD_SHARED_LIBS` is not rewritten.

### License notices

The pinned upstream [MIT license](https://github.com/quickjs-ng/quickjs/blob/1ab8676f4b6d6d669baeb5f21790fb9734636a20/LICENSE)
is preserved in `third_party/quickjs-ng/LICENSE`; its Git blob ID is
`fae657320cdf957cab10654ac39990fdb20e97ef`. The Unicode License V3 notice from the
[pinned Unicode table](https://github.com/quickjs-ng/quickjs/blob/1ab8676f4b6d6d669baeb5f21790fb9734636a20/libunicode-table.h)
is preserved in `third_party/quickjs-ng/UNICODE-LICENSE`.

The standalone build copies both notices next to its probe executables and
provides an install rule for them. Any later production integration must also
include these notices in server/native binary distributions; the spike does
not modify the existing production packaging. Review licenses again on updates.

## Probe contracts

Each evaluation creates and destroys its runtime and context on the calling
thread. RAII releases JSValues before the context, then the runtime, then the
callback state. No engine pointer, JSValue, or C++ allocation crosses the
probe's DLL ABI. Compiled modules remain owned while namespace/function values
are used, and the consumed `JS_EvalFunction` argument is explicitly duplicated.

The prototype compiles an ES module, verifies the exported `main`, calls it
once with a native-created context object, and returns a copied numeric result.
Exceptions carry a phase, bounded message, and available source stack. Exception
formatting can execute user-defined `toString`/getters, so it keeps the same
interrupt hook; a hostile formatter must not disable the deadline. Each retained
message and stack is limited to 4 KiB, not the size of all temporary allocations.

Defaults are 16 MiB of engine-managed heap, 256 KiB of JS stack, 256 KiB of source,
and a three-second cooperative deadline. Tests deliberately lower these values.
`JS_SetMemoryLimit` is installed before creating the context; runtime bootstrap
allocations that precede the setter are not claimed to be covered by that cap.
An external thread only sets an atomic cancellation token; it never touches a
runtime or context. A separate CTest process timeout is the final test-harness
backstop. `JS_ABORT_ON_LEAKS` is enabled only in this probe, never in the server.

Module normalization/loading denies all external specifiers, including `std`,
`os`, relative paths, Node names, and URLs. There is no host I/O library or module
loader fallback. Dynamic imports/promises/pending jobs are rejected; the probe
does not pump the job queue. Rejected fire-and-forget promises are tracked too.
Blocking `Atomics.wait` is disabled. These controls are **not an OS sandbox**.

### Automated native cases

The 13 cases run through both static and shared adapters: 26 CTest cases.

| Case | Behavior exercised |
| --- | --- |
| `smoke` | Module compile/evaluate, `main(ctx)`, loops, entry/result validation |
| `imports` | Explicit denial of standard-library, relative, Node, and URL imports |
| `async` | Async entry, top-level await, queued jobs, unhandled rejections, dynamic imports |
| `capabilities` | No host I/O/browser/process globals; no blocking Atomics wait |
| `exceptions` | Syntax/runtime/primitive exceptions, phase and source stack, fresh recovery |
| `interrupt` | Infinite loops in entry/module initialization; repeated catch loops |
| `cancellation` | Pre-cancel and cross-thread cancellation after an observed interrupt hook |
| `memory` | Oversized ArrayBuffer/string allocation, tiny context budget, fresh recovery |
| `stack` | Recursive stack exhaustion becomes an exception |
| `teardown` | 200 alternating successful/failed evaluations, exact runtime finalization, fresh globals |
| `concurrency` | Four threads, each running 50 independent runtime lifecycles |
| `source_limit` | Source byte cap and rejection of an unlimited memory configuration |
| `diagnostics` | Bounded retained diagnostics and interruption of a hostile exception formatter |

`--host core` adds the actual `PcgCoreStatic`/`PcgCore` targets and two consumer
smoke tests that call `pcg_get_version()` and execute the embedded script.
`--host server` also builds the actual `pcg-server` configuration, including its
Core and CURL/Assimp requirements. These are build/link coexistence tests, not
production graph execution or HTTP server health/cook tests.

## Current worker and ownership assumptions

The repository was reviewed at `a83468eeb3252af82b1348d9f1ad2b1cee3f4d6d`:

- `pcg-server/src/cook_service.cpp` has `g_cook_mutex` and job/cancellation state;
  existing native Cook synchronization must not be weakened by this spike.
- `pcg-core/CMakeLists.txt` builds shared and static targets with C++17/PIC and
  the MSVC static CRT policy. `pcg-server/CMakeLists.txt` consumes `PcgCoreStatic`.
- Unity is an HTTP server client in the current engineering contract. This
  change does not introduce an in-process Unity JavaScript runtime or plugin.

Independent-runtime concurrency is not evidence that the current Core can Cook
concurrently. Do not share a runtime between workers or move it between threads.
The [upstream C API guidance](https://quickjs-ng.github.io/quickjs/developer-guide/intro/)
and pinned header are the source for runtime/JSValue ownership rules. Production
geometry handles and native allocation accounting remain #24/#26 work.

## Running the validation

From the repository root, with Python 3.10+, CMake 3.20+, Git, and a native
C11/C++17 compiler available:

```sh
python scripts/test-validate-scripting-runtime.py
python scripts/validate-scripting-runtime.py --config Release --samples 200
python scripts/validate-scripting-runtime.py --host core --config Release
python scripts/validate-scripting-runtime.py --host server --config Release
```

`--generator Ninja` is optional. On Windows, run from an appropriate VS 2022+
developer environment; a multi-config generator is supported. Executable paths
are read from CTest's JSON inventory, not guessed from `Release/` folder names.
The upstream platform documentation requires a usable `<stdatomic.h>`; a nominal
VS installation alone does not establish compatibility with its selected SDK.

For an offline build, first prepare an exact, clean dependency checkout on a
machine with network access, then transfer the checkout **including `.git`**:

```sh
git clone https://github.com/quickjs-ng/quickjs.git /path/to/quickjs-ng
git -C /path/to/quickjs-ng checkout --detach 1ab8676f4b6d6d669baeb5f21790fb9734636a20
python scripts/validate-scripting-runtime.py --quickjs-source /path/to/quickjs-ng
```

The integrated Core/server builds still need their existing dependencies cached.
The local-source option does not disable source verification or allow a different
runtime version. Separate build directories are used per host/configuration/
sanitizer. GCC/Clang sanitizer examples, run separately:

```sh
python scripts/validate-scripting-runtime.py --config Debug --sanitizer address
python scripts/validate-scripting-runtime.py --config Debug --sanitizer thread
```

The runner records compiler-reported engine metadata, OS/CPU, configuration,
commands, test count, failures, and benchmarks in
`pcg-core/build/scripting/<host>-<config>-<sanitizer>/runtime-validation.json`.
It refuses an empty/incomplete CTest inventory, failed tests, stale runtime pins,
and malformed benchmark data. Keep generated logs/reports out of Git; attach
real reports to the PR. A failure report must not be interpreted as a benchmark.

## Measurement method and observed evidence

For both linkage modes, the benchmark performs 20 warm-ups followed by the
requested number of fresh-runtime samples and reports mean/p50/p95/min/max in
microseconds using `steady_clock`. Nearest-rank percentiles are used.

1. Runtime + context creation and destruction, without source compilation.
2. A fresh module's complete compile/call/teardown lifecycle.
3. A fresh module containing 10,000 arithmetic-loop iterations, including its
   compile/call/teardown overhead.

These measure repeated engine startup within one process, **not** OS process
startup, geometry performance, isolated warmed function-call time, or full Cook
latency. The probe includes leak checks. Baselines must use Release without
sanitizers on a documented machine; Debug/sanitizer results are correctness
investigations and must not be pooled with performance baselines. Review p95
against representative Core Cook workloads before accepting the runtime; this
spike does not invent a product latency SLA.

### Evidence collected on 2026-09-17 (UTC)

| Check | Observed result |
| --- | --- |
| Offline Python report-validation unit tests | 9 passed |
| Python byte compilation and CLI help | Passed |
| Vendored MIT notice identity | Exact match to pinned upstream Git blob |
| Linux x86-64 native configuration attempt | C/C++ compiler and pthread checks succeeded; dependency acquisition failed |
| Blocking error | `Could not resolve host: github.com` while FetchContent cloned QuickJS-NG |
| Native compile/link/CTest | **Not run to completion** |
| Startup/runtime measurements | **Not measured**; no timing values are claimed |
| Windows/macOS, sanitizers, integrated Core/server runs | **Not run** |

Attempt environment: Linux x86-64, GCC 14.2.0, CMake 3.31.6, Ninja 1.12.1,
Python 3.13.5, Intel Xeon Platinum 8573C. This records an infrastructure blocker,
not evidence that the engine itself failed its native probes.

### Required acceptance matrix before closing #21

| Configuration | Required evidence | Status |
| --- | --- | --- |
| Linux x86-64 GCC, Release/Debug | Static/shared probes and Release measurements | Pending |
| Linux x86-64 Clang, Release/Debug | Static/shared probes; ASan+UBSan and separate TSan | Pending |
| Windows x64 MSVC, Release/Debug | `/MT` + `/MTd`, static/shared/Core consumers | Pending |
| macOS arm64 AppleClang, Release/Debug | Static/shared/Core consumers | Pending |
| macOS x86-64 if shipped | Same probes on that architecture | Pending |
| Actual Core and server build configurations | `--host core` / `--host server` on shipped hosts | Pending |
| Distribution/license review | Both notices accompany distributed spike/native binaries | Pending native package verification |

The [upstream platform matrix](https://quickjs-ng.github.io/quickjs/supported_platforms/)
is useful guidance, not PCG-specific acceptance. No iOS, Android, WASM, 32-bit,
or other native target is newly promised by this spike.

## Go/no-go gates and alternatives

Advance from provisional selection only after the required native probes pass,
real measurements are attached and reviewed, and remaining compiler/platform
limitations are documented. Production use additionally requires #26's supervised
process isolation, native budgets, transitive capability policy, cancellation,
and failure-atomic publication. Do not close #21 solely because this PR exists.

Alternatives considered at the design level, not claimed as benchmarked:

- **Lua:** fallback when the required native toolchains cannot reliably embed
  this pin, repeatable lifetime/concurrency failures cannot be resolved, or
  measured overhead is unacceptable for actual Cook workloads. It would change
  the chosen JavaScript authoring contract and require its own bindings,
  tooling, validation matrix, and safety investigation. It is not an automatic
  cure for a non-interruptible native geometry operation.
- **Original QuickJS:** a possible alternative integration, but this spike
  follows the epic's QuickJS-NG candidate and its reviewed native CMake sources.
  No comparative performance or security superiority is asserted.
- **V8 or another engine:** not evaluated here; introducing another engine's
  build/embedding work is not justified by a demonstrated requirement in #21.
- **Browser-only JavaScript:** rejected because headless/Core/server execution
  must not depend on opening an asset in a browser first.

### Known limitations and update policy

`JS_NewContext` still exposes language intrinsics such as `Date` and `Math.random`;
this prototype does not implement #27's deterministic API. Cooperative interrupts
are not proof of hard compile-time, allocation, native-operation, or teardown
preemption. The memory setter covers engine-managed allocation, not arbitrary
C++ geometry, the entire process, output IPC, or the initial runtime bootstrap.
An untrusted script requires the terminable worker and OS restrictions in #26.

Keep the full commit pin; do not track a moving tag/branch or load untrusted
bytecode. For every upgrade, review release/security notes, source list/build
flags, C API/ownership changes, and license notices; repeat the full native and
sanitizer matrix, attach new Release measurements, and update version/commit,
this decision, and later cache/runtime fingerprints together. Do not silently
apply an unrecorded local dependency patch or switch authoring language.
