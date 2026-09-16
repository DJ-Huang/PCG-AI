---
id: pit-native-plugin-build-target-mismatch
name: "ctest 全绿 ≠ 运行时已更新：共享库部署与（历史）Unity dylib / codesign"
severity: medium
rootCauseType: "构建目标选择 / 代码签名"
techStack: ["C++", "CMake", "Unity Native Plugin", "DllImport", "codesign", "pcg-server"]
affectedPlatforms: ["仍使用 Unity Native Plugin 的项目；PCG-AI 已改为 localhost pcg-server，不再 copy dylib 到 Unity"]
common_assumption: "ctest 全绿、或手动 cp 了 dylib，Unity 就可以安全加载新 native 代码"
relatedConcepts: []
relatedNotes: ["[[pit-debug-layer-misattribution]]", "[[pit-manifest-serializer-native-json-contract]]"]
tags: ["type/pitfall", "area/build-system", "area/native-plugin", "area/unity-editor", "area/macos", "area/pcg"]
verified_status: limited
verified_by: "PCG-AI：2026-07-17/20 UnknownNode（A/B）；2026-07-23 ForEach Preview 闪退 IPS=CODESIGNING Invalid Page（C）；2026-07-28 改为 HTTP pcg-server，Unity 不再加载 PcgCore dylib"
verified_date: "2026-07-28"
source_exec_log: "[[WorkLog/执行记录/PCG Block AI/log-foreach-preview-cook]]"
source_date: "2026-07-23"
---

## 对 PCG-AI 的现行结论（2026-07-28）

Unity Editor **不再** `DllImport` / 加载 `Plugins/**/libPcgCore*` 或 `PcgFbxExporter`。Cook 与 FBX 走本机 `pcg-server`（HTTP）。

**正确部署**：

1. `scripts/build-pcg-server.sh` 重建后端
2. `scripts/run-pcg-server.sh`（或重启已在跑的进程）
3. Editor：`PCG → Server → Health Check`

**禁止**：再执行 `build-pcg-core.sh --copy-to-unity` / 把 dylib copy 进 Unity `Plugins/`（已废弃路径）。

`ctest` 仍只验证 `pcg-core` 算法；要让 Editor 吃到新 C++，必须重建并重启 **pcg-server**，不是 copy 插件。

---

## 历史问题（Unity 仍进程内加载 native 插件时）

C++ Unity 插件项目中，CMake 同时定义了 static lib（ctest）和 shared lib（`DllImport`）。常见失败：

1. `ctest` 全绿，但 Editor 报 `Unknown node type`（加载的仍是旧共享库）。
2. 手动覆盖了 `Plugins/**/lib*.dylib` 后，一点 Run/Preview **Editor 整进程闪退**（非托管异常）。

### 根因

Editor 只加载共享库产物；static 测试绿不代表磁盘上的 dylib/dll 已更新。macOS 上若 `cp` 破坏或未写入有效代码签名，首次 `dlopen` 会被系统 `SIGKILL (Code Signature Invalid)`。

**失败变体**（仅适用于仍走 Unity Native Plugin 的工程）：

| 变体 | 场景 | 根因 |
|------|------|------|
| A. 未构建共享库 | `ctest` 绿，Unity 报 UnknownNode | 只 build 了 static target，dylib 未更新 |
| B. 未重启编辑器 | dylib 已更新，Unity 仍报 UnknownNode | 进程仍持有旧映射，需整进程重启 |
| C. 未重签 dylib（macOS） | 覆盖 Plugins 后 Preview/Run 闪退 | 未 `codesign --force --sign -`；IPS：`CODESIGNING` / `Invalid Page` |

变体 C 易被误判为算法/空指针崩溃；先看 DiagnosticReports 是否为 codesign。

### 历史修复（非 PCG-AI 现行流程）

1. `cmake --build . --target <SharedLib>`
2. Copy 到 Unity `Plugins/...`
3. macOS：`codesign --force --sign - <dylib>`
4. 整进程重启 Editor

## 验证方法（PCG-AI 现行）

1. 改 C++ 节点后只跑 ctest、不重建 `pcg-server` → Editor cook 行为仍旧。
2. `build-pcg-server` + 重启服务 + Health Check 版本更新 → 新行为可见。
3. 通过：无 dylib copy、无 `DllNotFoundException`、cook/FBX 均经 HTTP。

## 原始记录

- [[WorkLog/执行记录/PCG Block AI/log.md]] — 2026-07-17（B）；2026-07-20（A）
- [[WorkLog/执行记录/PCG Block AI/log-foreach-preview-cook]] — 2026-07-23（C）
