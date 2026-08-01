# Code Review: `07-08-w1-cursor` → `main`

## Findings

### F1 [Critical] v2 Subgraph 序列化会无限递归并触发 Editor 栈溢出

- File: `Unity/Assets/PcgPlugin/Runtime/PcgSubgraphAssetSerializer.cs:13-23`，`Unity/Assets/PcgPlugin/Runtime/PcgSubgraphAssetContentHash.cs:9-18`
- Trigger: 保存带参数的 v2 `.pcgsubgraph`；或先加载任意 v1 资产，再通过编辑器保存。`TryFromJson()` 会先把 v1 root 升级到 v2，所有编辑器保存入口最终都会调用 `PcgSubgraphAssetSerializer.ToJson()`。
- Impact: `ToJson()` 为 v2 文档调用 `Compute()`，`Compute()` 又调用同一个公开 `ToJson()`，形成无终止条件的 `ToJson → Compute → ToJson`。最终是 `StackOverflowException`/进程级栈溢出，Unity/Tuanjie Editor 可直接退出，资产也无法保存。
- Evidence:
  - `PcgSubgraphAssetSerializer.cs:22-23` 对所有 v2 文档调用 `PcgSubgraphAssetContentHash.Compute(doc)`。
  - `PcgSubgraphAssetContentHash.cs:14-16` clone 后清空 hash，再调用公开 `PcgSubgraphAssetSerializer.ToJson(clone, false)`；clone 仍是 v2，所以立即重新进入 `Compute()`。
  - `PcgSubgraphAssetMigration.cs:31-35` 会把 v1 root 无条件升级到 v2，因此问题不只影响显式新建的 v2 文档。
  - `PcgGraphView.cs:846,2863,2898,3144,3288`、`PcgGraphEditorWindow.cs:740,810` 等实际保存路径均可达。
  - 已有 `PcgSubgraphLinkedAssetTests.cs:336-361` 正好调用 v2 `ToJson()`，但本次只能编译测试程序集，未运行 Unity EditMode 测试；执行该测试会进入上述递归。
- Fix: 增加不重算 hash 的私有 canonical 序列化入口。`Compute()` 应对 clone 清空 hash 后调用该入口一次；公开 `ToJson()` 先计算 canonical bytes 的 hash，再把结果写入最终 JSON，禁止 `Compute()` 回调公开 `ToJson()`。
- Test: Unity EditMode 中分别用“v2 + parameter”和“v1 fixture → TryFromJson → save”调用 `ToJson()`；断言调用正常返回、hash 为稳定的 64 位小写十六进制、重复保存 hash 不变、修改内容后 hash 改变。该测试在修复前会以栈溢出失败。

### F2 [Major] 临时诊断开关让 Stamp、Mask、Match Size SceneView 功能恒定不可达

- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:59-92`，`PcgHeightFieldMaskOverlaySceneHandles.cs:50-56`，`PcgStampOverlaySceneHandles.cs:34-40,64-73`，`PcgMatchSizeSceneHandles.cs:33-34`
- Trigger: 进入 PCG Mode，尝试使用 Stamp transform handle、HeightField mask tint 或 Match Size bounds。
- Impact: 三类原有 SceneView 编辑能力被关闭。Stamp 的拖拽与 on-release cook 不会发生，HeightField mask overlay 永远不绘制，Match Size 在 PCG Mode 中直接退出。
- Evidence:
  - `IsExternalSceneHandleIsolation` 被定义为 `s_PcgModeActive`，没有额外 stage 条件。
  - HeightField 与 Stamp 的 guard 是 `!IsPcgModeActive || IsExternalSceneHandleIsolation`。令 `A = s_PcgModeActive` 后表达式为 `!A || A`，在所有状态下恒为 true，因此两个 callback 永远提前返回。
  - Match Size 在 `IsExternalSceneHandleIsolation` 为 true 时返回，因此恰好在 PCG Mode 激活时禁用。
  - 代码注释明确称其为 “Temporary diagnostic stage”；枚举中没有恢复 external handles 的 production/full-pipeline stage。
  - 新增测试 `PcgSceneModeToolStateTests` 只验证退出时清理 Stamp drag state，没有覆盖 active-mode 可达性。
- Fix: 删除合并前的临时恒定隔离，或增加明确的 production/full-pipeline stage，并让 `IsExternalSceneHandleIsolation` 只在选定的诊断 stage 返回 true；`PcgCoreSceneGui` 正常模式下应返回 false。
- Test: EditMode 状态测试设置 inactive/active production mode，断言 external isolation 分别为 false/false；再以 SceneView 集成测试选择 Stamp、HeightField mask、Match Size 节点，断言对应 callback 注册 handle/overlay 且拖拽结束触发一次 cook。修复后还应在干净 `PcgReview_*` 场景做 Tuanjie SceneView 人工验收。

### F3 [Major] 同一 Subgraph 的多个实例会共享并覆盖参数，最后一个实例获胜

- File: `Unity/Assets/PcgPlugin/Runtime/PcgSubgraphParameterResolver.cs:14-25,43-64,68-104`，`Unity/Assets/PcgPlugin/Runtime/PcgGraphFlattener.cs:188-225`，`PcgExecutionDocumentBuilder.cs:44-48,73-75`
- Trigger: 根图或嵌套 scope 中放置两个引用同一 `subgraphId` 的 Subgraph 实例，并为同一参数设置不同 override。
- Impact: 两个实例展开后的内部节点得到相同参数值，通常是遍历顺序中最后一个实例的值；交换实例顺序会改变输出。实例化几何的尺寸、位置或其他被提升参数会静默错误。
- Evidence:
  - Resolver 用 `subgraphId` 建立一个共享 definition map。
  - `ApplyScope()` 遍历实例后，`ApplyDefinitionParameters()` 直接修改共享 `definition.nodes`，并未创建实例级副本。
  - Builder 在 flatten 前调用 resolver；Flattener 随后对每个实例都从同一个已经被改写的 `definition.nodes` clone，因此无法恢复各实例原本的 override。
  - 现有 `SubgraphParameterResolver_AppliesInstanceOverrideToInternalNode` 只有一个实例，并直接断言共享 definition 被修改，未覆盖实例隔离。
- Fix: 不要在 flatten 前改写 definition map。把 override 传入实例展开过程，对每个实例 clone definition/nodes 后再应用参数；嵌套实例也必须拥有独立作用域。
- Test: 一个 definition 的参数绑定内部 `CreateBoxMesh.width`，根图放置 `instA(width=1)` 与 `instB(width=5)`；执行 builder/flatten 后断言 `instA/box.width == 1`、`instB/box.width == 5`。反转根节点顺序后结果映射必须完全相同。该测试在修复前会稳定暴露 last-instance-wins。

### F4 [Major] 权威 `.pcgsubgraph` schema 同时拒绝 v2 输出和仓库内大多数现有资产

- File: `schema/subgraph-schema.json:5-12,15-44`，`PcgSubgraphAssetMigration.cs:7-35`，`PcgSubgraphAssetSerializer.cs:19-23,31-42`
- Trigger: 用权威 schema 校验 serializer 生成的 v2 资产，或校验仓库当前提交的 `.pcgsubgraph` 示例。
- Impact: schema、serializer、migration 和示例资产没有共同可接受的契约。外部工具或 CI 按 `schema/subgraph-schema.json` 校验时，会拒绝编辑器生成的参数化资产，也会拒绝仓库内 10 个示例中的至少 9 个，破坏跨消费者互操作和 source-of-truth 约束。
- Evidence:
  - Schema 仍声明 “No graph parameters”，`version.const` 固定为 `1.0`，顶层 `additionalProperties: false`，且 properties 中没有 `contentHash`/`parameters`。
  - Migration 的 current version 是 `2.0`，并给 v1 加入 `parameters` 与 `contentHash`；Serializer 对 v2 写出这两个字段。
  - 本分支又把 `inputs.minItems` 和单个 `Output` node 设为必需；对仓库 10 个 `.pcgsubgraph` 做结构扫描，7 个资产 `inputs=[]` 且仍使用 legacy `SubgraphOutput`，另外 2 个虽有 input 但仍没有 `Output` node。只有 `Windows_floor.pcgsubgraph` 满足这两项新增约束。
  - `docs/Tutorials/11-subgraphs.md:108-114` 明确把该 schema 列为 JSON 契约。
- Fix: 为 1.0 legacy 与 2.0 current 建立显式 `oneOf`/版本化 schema；2.0 分支必须声明 `contentHash`、`parameters` 及其定义。随后选择保留 legacy schema 兼容，或一次性迁移全部示例和文档；不能让运行时隐式修复、磁盘契约却拒绝同一文件。
- Test: CI 用 JSON Schema validator 校验仓库内所有 `.pcgsubgraph`；另由 serializer 生成 v1 空模板、v1 迁移结果、v2 参数化资产三个 golden fixture，并全部按对应版本 schema 校验。修复前 v2 fixture 和至少 9 个现有资产会失败。

### F5 [Minor] HTTP 迁移后的运行时文档仍大段描述已删除的 P/Invoke/静态链接路径

- File: `docs/Tutorials/07-unity-runtime.md:5-6,24-71,113-175,267-281`，`README.md:261-275`
- Trigger: 开发者按当前教程理解 Unity cook 或配置 Player/IL2CPP。
- Impact: 会被引导去查找已删除的 `DllImport`、`PcgCore.dll/.dylib` 与 `PcgCore.lib → GameAssembly.dll` 路径，和实际 localhost HTTP 架构冲突；排障与发布判断容易走错方向。
- Evidence: 教程开头声明 Unity 不再 DllImport，但心智模型、3.1、调用表、代码片段和复习答案仍写 P/Invoke；README 先说 Player 不静态链接 `PcgCore`，随后又说静态符号通过 `PcgCore.lib` 链入 `GameAssembly.dll`。
- Fix: 按 `PcgCookClient → pcg-server → binary response parser` 重写教程相关章节，删除 README 的旧静态链接句，并明确 Player 是否支持外置 server。
- Test: 文档 smoke review 至少校验架构关键词与现有源码符号一致；可增加链接/源码符号引用检查，避免再次引用已删除的 native binding。

## 开放问题与预测风险

1. **Player/IL2CPP 支持是否被产品层明确取消？** `main` 支持静态链接，当前分支删除全部 Unity native plugins，并把 build processor 改为 no-op；但 `PcgRuntimeRunner.cs:8-43` 仍自称 “IL2CPP Player path” 且会在 Player 中请求固定 localhost。若必须保留离线/随包运行时 cook，这应升级为 Major finding；若确实取消，需要删除或重新定义 runtime API 与验收里程碑。
2. **取消语义目前是全局 best-effort。** Client 生成 `job_id`，server 在 `cook_service.cpp:239-249,511-514` 解析后完全不使用；`POST /v1/cancel` 无请求身份并取消当前全局 native job。文档明确称 global cancel，所以本次不列 finding；若 server 会被多客户端并发使用，应改为 job-scoped cancel，否则一个客户端可能取消另一个客户端的 cook。
3. **content hash 在 import 时被直接信任。** `PcgSubgraphAssetImporter.cs:51-58` 读取内嵌 hash 后不重算；即使修复 F1，手工编辑、外部工具或冲突合并未同步更新 hash 时，version pin 仍可能漏报。建议在 importer 中重算并校验。
4. 本次没有执行 Unity/Tuanjie EditMode test runner 或 SceneView 交互测试。F1 的静态调用环足以阻塞，且直接运行相关测试可能终止 Editor；F2 修复后仍需要真实 SceneView 验证。

## 确认的死代码与无效状态

- `Unity/Assets/PcgPlugin/Runtime/PcgSubgraphAssetTypes.cs:111` 的私有 `TryInferPortsFromInterfaceEdges()` 没有调用方。
- `PcgCreateSplineSceneHandles.cs:38` 的 `s_TangentDragActive` 只有写入没有读取；`:191` 的 `s_LastParsedJson` 同样只有赋值。Mono 编译同时报告了由常量 `DiagnosticStage` 造成的多处不可达分支。
- `pcg-core/src/elements/delete_algorithms.cpp:168,181,186` 的 `json_number`、`indexed_curve_u`、`spline_curve_u` 没有调用方，C++ 构建产生 unused-function warning。
- `pcg-server` 的外部 `job_id` 目前只解析、不参与排队、执行或取消，是无效协议字段；是否删除或真正接入取决于上面的并发契约决策。

## 变更概述

- Source: `07-08-w1-cursor` / `bfe7335e3d5ad71fa633fda6faab63703d3dd794`
- Target: `main` / `44a84b4ba40ef3362868ee38b15bcefe15df0631`
- Merge base: `c53eb45390f2bd22882ead6cfc4c870d200abdd5`
- Diff: `main...HEAD`，31 个 source commits，276 files，59,329 insertions / 4,080 deletions
- `main` 比 source 多 1 个 merge commit，但其 tree 与 merge base 相同；`git merge-tree --write-tree --quiet main HEAD` 返回 0，无文本冲突。
- 主要变更：Unity/native cook 迁移到 localhost `pcg-server`；linked/inline Subgraph 参数、接口、导航和 flatten 扩展；大量 pcg-core 几何/属性/样条算法；SceneView/Inspector 工具；示例 graph、材质、纹理和 review scenes。
- 三份 node manifest SHA-256 完全一致：`schema/`、Unity Editor Graph、Unity Resources 均为 `9c5c265e...eebe82f4`。

## 验证结果与测试计划

### 已执行

- `./scripts/build-pcg-core.sh --run-tests`：构建成功，CTest 41/41 通过。
- `./scripts/build-pcg-server.sh`：构建成功。
- 在端口 28791 启动 server，执行 `PCG_SERVER_PORT=28791 COMPARE_NATIVE=1 ./scripts/verify-pcg-server.sh`：health、cook 均成功，HTTP payload hash 与 native payload 完全一致。
- Mono `msbuild` 编译 `PcgPlugin.Runtime.csproj`、`PcgPlugin.Editor.csproj`、`PcgPlugin.Editor.Tests.csproj`：全部成功；Editor 项目有上述不可达/未使用字段 warning。
- `npm run build` 与 `npm run lint`：均成功；Vite 仅报告主 bundle 530.87 kB 的 chunk-size warning。
- `jq empty` 校验 `schema/`、`examples/`、`Unity/Assets/PcgPlugin/` 下 JSON：通过。
- Unity `.meta` GUID 重复扫描：未发现重复 GUID。
- `git diff --check main...HEAD`：报告 908 行 trailing whitespace，主要来自 Unity 生成的 `.meta/.mat/.scene`，另有少量 Markdown；属于提交卫生问题，不单列功能 finding。

### 修复后必须补跑

1. 单独运行 v1/v2 subgraph serializer EditMode 回归测试，确认不再崩溃且 hash 稳定。
2. 新增并运行双实例 parameter override 的 builder/flatten 测试，包含反转实例顺序与嵌套实例。
3. 对全部 `.pcgsubgraph` 和 serializer golden 输出执行版本化 schema validation。
4. 运行完整 Unity/Tuanjie EditMode tests，并在干净 review scene 验收 Stamp、Mask、Match Size 与 spline transform gizmo 的输入所有权。
5. 在 Windows 环境验证 `pcg-server`、Unity Editor HTTP cook；若 Player 仍在范围内，再做 IL2CPP Player 端到端验证。

## 结论

**blocked / 阻塞合并。**

已确认 1 个 Critical、3 个 Major、1 个 Minor。F1 可导致 Unity/Tuanjie Editor 栈溢出；F2、F3 分别让已有 SceneView 能力不可达和多实例参数结果错误；F4 说明权威资产契约尚未闭合。应按 F1 → F2/F3 → F4 → F5 的顺序修复并 re-review。
