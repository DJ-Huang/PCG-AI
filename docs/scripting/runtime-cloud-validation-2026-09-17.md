# Runtime cloud validation — 2026-09-17

## Revisions and scope

- Issue: #21; parent: #20; implementation PR: #31.
- Tested PCG-AI source: `39be66e7f95595c22652dbd1ac908fbf86386576`.
- QuickJS-NG: `0.16.2`, commit `1ab8676f4b6d6d669baeb5f21790fb9734636a20`.
- Execution used an isolated temporary GitHub Actions branch, not the network-restricted chat container. The workflow was never added to `main` or the implementation PR and was removed from the temporary branch in `4b6a43904054dc9ff671b6e429013d60be49caf5`.
- This record covers standalone/static/shared runtime tests, sanitizers, and Linux Core/server build/link integration. Production graph Cook is outside its scope.

Cloud runs: [GCC Release/Debug](https://github.com/DJ-Huang/PCG-AI/actions/runs/35189437786) and [extended native and Linux integration matrix](https://github.com/DJ-Huang/PCG-AI/actions/runs/35189586789).

## Observed results

All **14 configurations passed** configuration, native compilation/linking, CTest, and both static/shared benchmark validations: **372 passing CTest executions**. This consists of 10 configurations with the same 26-case runtime inventory, plus four Linux integration configurations with 28 cases each. It is not 372 unique behaviors. Each invocation also passed the Python runner unit tests.

| Configuration | Compiler | CTest | Report |
| --- | --- | ---: | --- |
| Linux GCC Release | GNU 13.3.0 | 26/26 | `passed` |
| Linux GCC Debug | GNU 13.3.0 | 26/26 | `passed` |
| Linux Clang Release | Clang 18.1.3 | 26/26 | `passed` |
| Linux Clang Debug | Clang 18.1.3 | 26/26 | `passed` |
| Linux Clang ASan + UBSan | Clang 18.1.3 | 26/26 | `passed` |
| Linux Clang TSan | Clang 18.1.3 | 26/26 | `passed` |
| Windows x64 MSVC Release | MSVC 19.44.35228.0 | 26/26 | `passed` |
| Windows x64 MSVC Debug | MSVC 19.44.35228.0 | 26/26 | `passed` |
| macOS arm64 Release | AppleClang 17.0.0.17000013 | 26/26 | `passed` |
| macOS arm64 Debug | AppleClang 17.0.0.17000013 | 26/26 | `passed` |
| Linux Core Release | GNU 13.3.0 | 28/28 | `passed` |
| Linux Core Debug | GNU 13.3.0 | 28/28 | `passed` |
| Linux server Release | GNU 13.3.0 | 28/28 | `passed` |
| Linux server Debug | GNU 13.3.0 | 28/28 | `passed` |

The downloaded reports were checked against the exact PCG and runtime commits, every passing CTest log entry, sample counts, and both linkage modes. No implementation source changes or skipped cases were needed. All 14 artifact packages contained the expected license notices.

The Core configurations built the actual `PcgCore` and `PcgCoreStatic` targets and ran their two consumer smoke tests. Both server configurations also completed the actual `pcg-server` executable link. These tests do not start an HTTP server, execute a graph, or exercise a production Custom Function node.

## Release measurements

Values are microseconds. Each metric uses 20 warm-up iterations and 200 measured fresh-runtime samples. Debug and sanitizer measurements are excluded from this baseline table. The table uses standalone runs to avoid mixing multiple host-build samples.

| Configuration | Linkage | Create/destroy mean | Module lifecycle mean | Module lifecycle p95 | 10,000-loop lifecycle mean |
| --- | --- | ---: | ---: | ---: | ---: |
| Linux GCC Release | static | 50.610 | 53.828 | 58.327 | 195.960 |
| Linux GCC Release | shared | 53.191 | 56.096 | 61.351 | 174.564 |
| Linux Clang Release | static | 101.240 | 111.305 | 139.961 | 349.397 |
| Linux Clang Release | shared | 99.764 | 109.693 | 127.557 | 576.414 |
| Windows MSVC Release | static | 175.440 | 167.470 | 191.000 | 845.402 |
| Windows MSVC Release | shared | 174.923 | 165.654 | 194.900 | 863.600 |
| macOS arm64 Release | static | 88.801 | 85.527 | 93.250 | 421.324 |
| macOS arm64 Release | shared | 135.015 | 97.497 | 167.458 | 441.758 |

Creation/destruction includes runtime and context. Module lifecycle includes source compilation, the `main(ctx)` call, and teardown. The loop metric includes these same costs plus 10,000 arithmetic iterations. These are not OS worker-process startup, geometry execution, or end-to-end Cook measurements.

The runners used different processors; these numbers are not a controlled compiler/platform comparison. The Linux GCC Release runner reported AMD EPYC 9V45, the Clang Release runner AMD EPYC 7763, Windows reported an AMD64 processor-family identifier, and macOS reported arm64 without an exact SoC model. Raw reports retain the observed OS, CPU, compiler, and complete distributions. No product latency SLA or universal reproducibility claim is established.

## License and evidence verification

Every downloaded artifact contained both `licenses/quickjs-ng/LICENSE` and `UNICODE-LICENSE`. The MIT notice matches pinned upstream Git blob `fae657320cdf957cab10654ac39990fdb20e97ef`. All copied Unicode notices match Git blob `56da58912807030b451a4700687d5bd02c72d6ed` in this spike.

Artifacts contain runner JSON, CMake/build/CTest/benchmark logs, and copied notices. They were uploaded with seven-day retention; preserve a downloaded copy before expiration. This document retains a reviewed summary rather than committing generated build output.

| Artifact | SHA-256 |
| --- | --- |
| `issue-21-linux-gcc-Release.zip` | `fb573338ada78941b3100bf5d2777fcadcf29dd5a4d2d88f192086dae383a740` |
| `issue-21-linux-gcc-Debug.zip` | `c13fbdbda775df13b7d6cd9e3d6ae7b8695e5531967540eae98be9dbe5599484` |
| `issue-21-linux-clang-Release.zip` | `4e1baf30321e0d756c41bb0f7a636a44f4be1142fd238d73626cfcd64ee8384c` |
| `issue-21-linux-clang-Debug.zip` | `4b04158a46fb0633ebfa0713892eecf0cebf5ad115f649f125ee358ae8034002` |
| `issue-21-linux-clang-ASan-UBSan.zip` | `14406a676bc294b3ec9f8106709afac98e6532da4321da22113871d0734ae2a3` |
| `issue-21-linux-clang-TSan.zip` | `1984dda8c6ea748e110f5bf0c7d87b51679c4e6624fc9a23e799be07d5cbc43e` |
| `issue-21-windows-msvc-Release.zip` | `ad343c038ee730eee51b0b9e5a8c2eb019bfda6cd9365d493d30d8b16de94563` |
| `issue-21-windows-msvc-Debug.zip` | `2fddedc68d07b5dd0f4fab27e04d18966934c3aebe660a031a3b7c9dc795f60a` |
| `issue-21-macos-arm64-Release.zip` | `de763ffe034c86a406799ac166e51a037ef8ffdef61a9f858233bf6cafba1e33` |
| `issue-21-macos-arm64-Debug.zip` | `64ab80fa7f5b88337679706873c5dc5cd901268ec5d8c1d7a02506848d7ea3c4` |
| `issue-21-linux-core-Release.zip` | `c1a6acd349c86b0d53daad46b2dc23b6833fbda168219c37e8fa5d9c526f9f8c` |
| `issue-21-linux-core-Debug.zip` | `b670a9888eb6cd3668e5ed5c34045751ba423f74ac47f7fbd6f40c719444fdb9` |
| `issue-21-linux-server-Release.zip` | `46a943fcf794051593116e49175b7f198b7f0a6f3fa746b71383c8b97cc1b384` |
| `issue-21-linux-server-Debug.zip` | `982bd608b1c46f498868f60d64c1985f9bd01465c6c3f47b1df33befd8e0d0a0` |

## Remaining acceptance boundaries

The original chat-container DNS failure is no longer a blocker for the native and Linux integration coverage recorded here. Passing independent JS runtimes does not establish that process-global Core Cook state supports concurrent cooking.

Keep #21 open until remaining shipped-platform requirements are reviewed. Windows/macOS Core/server integration and macOS x86-64, if shipped, are not established by these results. Production packaging, supervised process isolation/native budgets (#26), deterministic execution/cache identity (#27), modeling bindings, editor behavior, and end-to-end graph Cook remain outside this evidence. The initial decision document's unrun-native entries describe the earlier chat-container attempt; this dated cloud record supersedes those entries only for the configurations explicitly shown as passing above.
