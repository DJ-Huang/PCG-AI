# Code Review: `07-08-w1-cursor` vs `main`

## 结论

**needs_changes**

本次三点分支审查确认了 2 条 Major 和 1 条 Minor。两个 Major 都位于本分支新增的跨端材质链路：一条会在 Unity 组件禁用/恢复后留下已销毁的材质引用，另一条会使 Web 图使用的纹理 URL 在 Unity 中静默丢失。建议修复两个 Major 并补齐回归测试后再合并。

## 审查范围

- Source branch: `07-08-w1-cursor`
- Source HEAD: `08c69abe0fb3bd12d928c205f0d4c16c48dc5314`
- Target branch: `main`（`main` 与 `origin/main` 一致）
- Target HEAD: `fa204bf0c0d0a9509ba374892c1b4466f14deed1`
- Merge base: `df422c262c7752a9dfad056554689572fae0f291`
- Diff semantics: `main...HEAD`；`main` 当前 tree 与 merge-base tree 相同
- Change size: 296 files，+70,418 / -3,340
- 未提交且明确排除：`AGENTS.md`、`wooden-cabin.pcg` 的工作区修改；对 `wooden-cabin.pcg` 的审查证据取自 Source HEAD
- 规则上下文：已加载 PCG 工程规则 `pcg/project-engineering`、`pcg/graph-contract`，并检索材质绑定、cook generation、二进制几何、manifest/serializer 等 Vault 经验；本项目不是 HMIRP

## 变更概述

该分支同时扩展了多条关键链路：

- `pcg-core`：新增/扩展 Add、scatter、spline、Material/AssignMaterial 等节点与材质元数据传播。
- `pcg-server`：新增内置 Agent runtime、Provider/凭据管理、会话持久化、SSE turn 生命周期及每页编辑器路由。
- Web Editor：新增 Agent UI、图命令、PBR 材质预览、环境与预览参数、subgraph/serialization 支持。
- Unity：新增由 cook 元数据生成 transient Material/Texture 并绑定到 MeshRenderer/GPU instancing 的路径。
- 内容与工具：新增 wooden-cabin 图、纹理和验证脚本，并更新端到端文档。

## Findings

### F1 [Major] 禁用组件会销毁仍绑定在 MeshRenderer 上的生成材质

- File: `Unity/Assets/PcgPlugin/Runtime/PcgGraphComponent.cs:308`
- Trigger: Play Mode 中，`RunOnStart` 或手动 cook 的 `PcgGraphComponent` 已将 Material 节点生成的材质绑定到 MeshRenderer，之后 GameObject/组件被禁用并再次启用，且没有触发新 cook。
- Impact: Renderer 的 `sharedMaterials` 仍指向已销毁对象；重新启用后出现 Missing/Pink/回退材质。对象池、场景分区激活和常规组件切换均可触发。
- Evidence:
  - `ApplyExecutionResult()` 在 `PcgGraphComponent.cs:808-809` 创建/重建 `PcgGeneratedMaterialSet`。
  - `ApplyMaterialBindings()` 在 `PcgGraphComponent.cs:1793-1802` 将其中材质写入 `MeshRenderer.sharedMaterials`。
  - `OnDisable()` 在 `PcgGraphComponent.cs:308-317` 调用 `ReleaseGeneratedMaterials()`；后者在 `PcgGraphComponent.cs:1885-1889` dispose 并置空材质集合。
  - `PcgGeneratedMaterialSet.Dispose()` 在 `PcgGeneratedMaterialSet.cs:34-43` 销毁全部生成材质和自有纹理。
  - Play Mode 的 `OnEnable()`（`PcgGraphComponent.cs:285-306`）不会重建/重绑材质；`Start()`（`PcgGraphComponent.cs:328-331`）在一次生命周期中也不会因再次启用而重跑。
  - 现有 `PcgGeneratedMaterialTests` 只验证生成、属性映射、dispose 和纯 resolver，没有覆盖组件 disable/enable 生命周期。
- Fix: 最小方案是把生成材质所有权延长到 `OnDestroy()`，不要在 `OnDisable()` dispose；如果必须在 disable 时释放，则应同步清空 renderer/instancer 引用，并在 `OnEnable()` 根据缓存的 cook 元数据重建和重绑。两种方案都需确保 fallback 与 GPU instancing 的所有权一致。
- Test: 新增 Unity PlayMode 生命周期测试。完成一次包含生成材质的 cook，记录 renderer slot，依次 `SetActive(false)` / `SetActive(true)`（另测 `component.enabled`），等待一帧后断言所有 slot 仍为有效 Material 且无需手动 recook。当前实现应在重新启用后得到 destroyed/null 引用。

### F2 [Major] Web 可用的 `/assets/...` 纹理存储值无法跨到 Unity

- File: `wooden-cabin.pcg:1622`（Source HEAD）
- Trigger: 在 Web 中打开本分支新增的 `wooden-cabin.pcg`，预览或点击 **Send to Unity**，然后让 Unity cook/apply 其中 `cabin_stone`、`cabin_wood` 等 Material 节点。
- Impact: Web 中可以显示的 albedo/metallic/roughness/normal/AO 贴图在 Unity 中全部无法解析，生成材质静默退化为纯色与标量参数；这破坏了仓库文档所描述的 Web → JSON → C++ → Unity 闭环及该示例的目标外观。
- Evidence:
  - Source HEAD 的 `wooden-cabin.pcg` 有 24 个 `/assets/textures/wooden-cabin/...` 存储值，例如 `:1622`、`:2799-2806`、`:6610`。
  - 对应 PNG 只新增在仓库根 `assets/` 与 `web/pcg-editor/public/assets/`，Unity 的 `Assets/` 下没有这些文件。
  - Web 的 `createStandardPbrMaterial()` 在 `web/pcg-editor/src/preview/pbrMaterials.ts:149-168` 将这些值直接作为 URL 加载，因此 `/assets/...` 在 dev server 中有效。
  - C++ 的 `build_material_definition()` 在 `pcg-core/src/elements/material_elements.cpp:11-38` 原样传播存储字符串，不做跨端转换。
  - Unity 的 `PcgGeneratedMaterialSet.LoadTexture()` 在 `PcgGeneratedMaterialSet.cs:231-255` 只支持 AssetDatabase/GUID、data URI 和 Resources；`PcgTextureAssetUtil.LoadTextureFromStorage()` 在 `PcgTextureAssetUtil.cs:60-112` 也只能搜索 Unity project assets。仓库根和 Web public 目录不属于 Unity AssetDatabase。
  - Player 路径更窄：非 data URI、非 Resources 的 `/assets/...` 必然返回 null。
  - 现有测试只覆盖标量 PBR 与 data URI，没有 Web URL → Unity transfer 契约测试。
- Fix: 定义单一、可跨 Web/Unity 的纹理 locator 契约。最小可选方案包括：把共享纹理导入 Unity 的确定性 `Assets/...`/Resources 路径并在 export 时重写；或在发送图时内联/复制纹理并生成可移植引用。不要依赖 AssetDatabase 按文件名模糊搜索，否则同名纹理会产生不确定绑定。
- Test: 新增 Web→schema→server cook→Unity apply 集成 fixture。使用本分支 cabin 的一组 PBR map，断言 Unity 生成的 `cabin_wood` Material 上 Base/Metallic/Normal/AO 纹理均非 null 且对应预期资源；同时覆盖 Player 可用的 locator。当前 `/assets/...` fixture 应失败。

### F3 [Minor] 文档声明的确定性 Agent runtime 验证脚本已无法运行

- File: `scripts/validate-agent-runtime.py:222`
- Trigger: 按 `docs/pcg-server.md:142-147` 在干净环境执行 `python3 scripts/validate-agent-runtime.py`。
- Impact: 脚本在第一个 turn 即失败，无法作为 Agent runtime 的回归门禁；开发者会把真实的编辑器会话契约误判为 runtime 故障，新增会话/重启路径也失去自动验证。
- Evidence:
  - 脚本在 `scripts/validate-agent-runtime.py:222-227` 直接 POST `/turns`，既未先发布编辑器上下文，也未传 `editorSessionId`，却断言 HTTP 200。
  - Runtime 在 `pcg-server/src/agent_runtime.cpp:2565-2571` 已强制调用 `GetEditorContext()`；无活跃编辑器时返回 HTTP 409 `editor_offline`。
  - 实际运行稳定复现：断言行 227 收到 409 `editor_offline`。
  - 即使人工在首次 turn 前补发 editor session，脚本在 `scripts/validate-agent-runtime.py:292-323` 重启 server 后仍未重新同步 editor context，后续 turn 会再次失败。
- Fix: 在脚本中先向 editor session API 发布带稳定 `editorSessionId` 的最小图上下文，所有 turn 都携带该 ID；server 重启后重新发布上下文，再验证恢复后的 turn。
- Test: 将该脚本纳入 CI，在隔离的 config/credentials/sessions 目录和干净 server 上直接执行，预期退出码 0；测试必须同时走首次 turn 和 restart 后 turn，不允许依赖外部已打开的 Web 页面。

## 确认的死代码

未确认到可安全删除的死代码。本分支存在多份 cabin 图/布局产物，但它们的交付用途没有足够证据，未将“看似重复”直接判为死代码。

## 预测风险与开放问题

### R1 同一 Agent session 的并发 turn 可能互相覆盖

`HandleAgentTurn()` 在 `pcg-server/src/agent_runtime.cpp:2493` 于 turn 建立时读取整份 session snapshot；运行阶段只持有各自的 `TurnState::mutex`（`:2617`），没有按 `session_id` 串行化。完成时每个 turn 都用自己的 snapshot 覆盖 session，而 `SaveAgentSession()` 还共用 `<session>.json.tmp`（`agent_session_store.cpp:128-145`）。单页 UI 会阻止常规重复提交，但多标签页、两个客户端或 API 调用可绕过。建议增加双客户端确定性并发测试；若复现，按 Major 处理并引入 per-session serialization/optimistic revision。

### R2 Web bundle 缺少明确体积预算

生产 build 通过，但主 bundle 约 1.48 MB（minified）并触发 chunk size warning。当前没有证据表明已经造成可观察回归，因此不列 finding；建议为 Agent/Three.js 等重模块确认 lazy-loading 边界并设置可接受预算。

### R3 cabin 图布局仍有可维护性风险

三份 cabin 图均通过结构校验，但 `web/wooden-cabin.pcg` 报出大量布局 warning。它不直接改变 cook 结果，所以未升级为 correctness finding；若该文件是可编辑交付物，应按项目 top-down layout gate 重新布局后再验收。

## 验证记录

### 通过

- Web production build（有约 1.48 MB bundle warning）
- Web lint
- Agent Vitest：16/16
- Graph command validation
- Web PBR、built-in environment、preview parameter contract validations
- pcg-server rebuild
- Agent bridge validation（针对重建后的 server）
- HTTP/native server verification
- Material cook contract
- 三份 cabin graph 结构验证
- 三份 node manifest SHA-256 一致性检查

### 失败或例外

- `scripts/validate-agent-runtime.py`：HTTP 409 `editor_offline`，对应 F3。
- Core CTest：42/43；`test_lot_subdivision` 因引用旧绝对路径失败。该失败路径已存在于 `main`，不是本分支引入，因此不列 finding。
- Unity PlayMode 未在本轮可用 runner 中完整执行；F1 由完整生命周期/所有权调用链确认，仍需上述回归测试锁定。

## 建议的合并前门禁

1. 修复 F1，执行新增 disable/enable PlayMode 回归以及既有 `PcgGeneratedMaterialTests`。
2. 修复 F2，以 cabin 贴图跑通 Web→schema→cook→Unity Editor/Player 的材质集成测试。
3. 修复 F3，使独立 Agent runtime validator 在首次启动和重启后均通过。
4. 重跑 Web build/lint/Vitest、server build/bridge/HTTP/material contract、core CTest，并单独确认 `test_lot_subdivision` 的基线问题没有被本分支扩大。

## 修复验证（2026-08-13）

- F1 已修复：生成材质不再于 `OnDisable()` 释放，只在 `OnDestroy()`/显式清理时释放；新增组件和 GameObject 启停生命周期回归。
- F2 已修复：Unity 支持 `pcg-resource://...` 及既有 `/assets/...` locator 映射到 Resources；cabin 纹理已作为 Unity Resources 导入，并校正 normal/metallic/roughness/AO importer 色彩空间。
- F3 已修复：runtime validator 在首次 turn 和 server restart 后主动同步同一 `editorSessionId`，所有 turn 均携带该 ID。
- Unity `PcgGeneratedMaterialTests`：14/14 通过。
- Web Agent Vitest：16/16 通过；graph commands、PBR、environment、preview-parameter contract、lint、production build 全部通过（仅保留已知 bundle size warning）。
- Agent runtime validator、material cook contract、HTTP/native parity、Python compile 与 `git diff --check` 通过。

修复后的审查结论：**can_merge（本报告 3 条 finding 均已关闭）**。
