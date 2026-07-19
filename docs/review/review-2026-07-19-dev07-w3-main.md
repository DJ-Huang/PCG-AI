# Code Review: `dev/07-w3` → `main`

- **审查日期：** 2026-07-19
- **源分支 / 提交：** `dev/07-w3` / `da0a97e3a6b5820bf1fb08669d3fdfd894976701`
- **目标分支 / 提交：** `main` / `e96200ed84515a2d99fd83eea18d30255d8c260f`
- **Merge base：** `e96200ed84515a2d99fd83eea18d30255d8c260f`
- **范围：** `main...HEAD`，53 commits，550 files，+83,764 / -4,656
- **审查输入：** 只读取已提交 Git 对象；未跟踪的 Cursor 工作区文件不在范围内
- **结论：** **Needs changes**（4 Major，0 Critical）

## 变更概述

本分支在 PCG Core、Unity Runtime/Editor、图结构、HeightField、几何属性、节点库和 FBX exporter 上进行了大规模扩展。核心数据流如下：

1. `.pcg` JSON 经 Unity execution policy 预处理后进入 Native ABI。
2. Core 解析并展开 subgraph，执行节点，输出 Mesh / Geometry / Points / HeightField sidecar。
3. Unity 将图参数覆盖应用到文档，并把宿主 Terrain 数据通过 v9 ABI 上传。
4. Editor FBX exporter 读取 geometry binary，通过 Assimp 输出 FBX。

高风险边界集中在“校验 → 执行”“根图参数 → 子图定义”“托管数组 → 裸指针”“corner attribute → per-vertex exporter”四处，分别对应下列 findings。

## Findings

### F1 [Major] 执行 API 不再执行图结构校验，非法图会被静默执行

- **文件：** `pcg-core/src/pcg_core.cpp:603-633`，`pcg-core/src/graph_parser.cpp:308-405`，`Unity/Assets/PcgPlugin/Runtime/PcgGraphLoader.cs:102-111`
- **触发条件：** 外部、旧版本或手工生成的 JSON 含非法 pin handle、pin 类型不匹配、重复 edge，或多个 edge 连接到非 variadic input，然后直接调用任一 execute API。
- **影响：** `pcg_validate_graph` 会拒绝的图，execute 路径却可能成功并生成错误结果。调用方无法再依赖“执行前必定通过结构校验”的契约。
- **证据：**
  - `execute_graph_cached` 只在 603-610 行调用 `parse_graph`，随后在 627-633 行直接执行。
  - 结构校验仍独立存在于 `pcg_validate_graph`，其中 381-402 行校验 pin handle、pin 类型、重复 edge 和非 variadic input 基数。
  - Unity 侧删除了独立 validation，并在 108-109 行注释“ExecuteGraph parses and validates”，但 Native execute 实际只 parse。
  - 针对已提交构建的 ABI smoke：同一个图把两个 `SpawnPoints.out` 接到一个非 variadic `PlaceInScene.in`；`pcg_validate_graph` 返回 invalid，`pcg_execute_graph` 却返回 `PCG_OK`，只产出其中一路数据。
- **修复方向：** parse 后对同一 `Graph` 调用 `validate_graph_structure`，放入 execute API 共用路径；这样恢复安全性且不需要再次解析 JSON。
- **测试计划：** 对 legacy、cached 以及当前最高版本 execute API 参数化测试非法 handle、类型冲突、duplicate edge、non-variadic 多输入，断言全部与 `pcg_validate_graph` 返回一致的错误码。

### F2 [Major] “Create Subgraph from Selection” 会让已暴露参数静默失效

- **文件：** `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphView.cs:432-555`，`Unity/Assets/PcgPlugin/Runtime/PcgGraphComponent.cs:541-581`
- **触发条件：** 根图参数绑定到某个已选节点的属性，然后对该节点执行 “Create Subgraph from Selection”。
- **影响：** 参数记录仍指向原节点 ID，但节点已从 `document.nodes` 移入 `document.subgraphs[*].nodes`。Component override 找不到目标节点，暴露参数继续显示却不再影响 cook，且没有错误提示。
- **证据：**
  - 456-458 行把选中节点放入 subgraph definition；531-552 行从父 scope 移除节点并保留原 `m_RootDocument.parameters`。
  - `ApplyOverridesToDocument` 在 564 行调用 `FindNode`；该函数 575-579 行只遍历 `doc.nodes`，不搜索 subgraph definition。
- **修复方向：** 创建子图时显式迁移/拒绝相关 parameter binding，并把目标身份扩展为带 scope/instance chain 的引用；不应简单按 node ID 遍历所有 definition，否则一个 definition 的多个实例会被同时改写。
- **测试计划：** 创建绑定 Box size 的 exposed parameter，选中 Box 建子图，修改 component override 并 cook；断言目标 instance 的内部节点数据和输出尺寸改变，其他 instance 不受影响。

### F3 [Major] HeightField v9 上传未携带数组长度，Native 可越界读取托管数组

- **文件：** `Unity/Assets/PcgPlugin/Runtime/PcgNative.cs:510-542`，`Unity/Assets/PcgPlugin/Runtime/PcgHostTerrain.cs:132-145`，`pcg-core/include/pcg_api.h:341-359`，`pcg-core/src/pcg_core.cpp:524-554`
- **触发条件：** 调用公开 `PcgGraphLoader.Execute(..., heightfields)`，传入 `ResolutionX * ResolutionZ` 大于 `Heights.Length` 或 `Mask.Length` 的 `PcgHeightFieldUpload`；对象字段公开，也可在标准 producer 创建后被修改。
- **影响：** C++ 依据 resolution 无条件 `memcpy` `sample_count` 个 float，会读出 pinned array 边界，可能造成进程崩溃或数据泄漏/污染。
- **证据：**
  - Managed wrapper 仅检查 `Heights` 非空，随后直接 pin；没有检查数组长度与分辨率乘积相等。
  - `PcgHeightFieldSlot` ABI 只有两个裸指针，没有 `height_count` / `mask_count`。
  - Native 在 550-553 行按 `resolution_x * resolution_z` 计算目标长度并复制，无法知道源缓冲区真实大小。
- **修复方向：** ABI 增加 height/mask 元素数并在 Native 校验；同时 Managed wrapper 用 checked multiplication 校验精确长度。若短期不能升级 ABI，至少在唯一公开托管入口拒绝不匹配数据，但这不能保护直接 C ABI 调用者。
- **测试计划：** Managed 层覆盖过短、过长、乘法溢出和 mask 长度不匹配；升级 ABI 后增加 Native mismatch test，断言返回 invalid argument 且不执行 memcpy。

### F4 [Major] FBX exporter 把 corner UV 压成 point UV，UV seam 会被覆盖

- **文件：** `pcg-fbx-exporter/src/geometry_binary_reader.cpp:215-235`，`pcg-fbx-exporter/src/pcg_fbx_exporter.cpp:98-134`，对照 `pcg-core/src/data/pcg_geometry.cpp:652-673`
- **触发条件：** geometry 在同一个 point 的不同 face corner 上有不同 UV，例如圆柱接缝、UV island 或 HeightField corner UV，然后执行 FBX 导出。
- **影响：** 每个 point 最终只能保留一个 UV，接缝两侧至少一侧被覆盖，导出的 FBX 纹理坐标产生拉伸/错位。
- **证据：**
  - reader 在 225-234 行分配 `point_count` 个 UV，并按 corner 反复写入 `geometry.uvs[index]`；注释称 “first writer wins”，实际代码是后写覆盖前写。
  - exporter 在 101-123 行严格创建 `geometry.points.size()` 个 Assimp vertex，每个 point 只写一个 UV；face index 又直接复用原 point index。
  - Core 的 render conversion 已采用正确语义：corner UV 优先，并按 render key 拆分 vertex（652-665 行）。
  - 现有 exporter test 只有单 quad 的 point-domain UV，没有共享 point 的 seam case。
- **修复方向：** 构建 Assimp mesh 时按 `(source point, corner UV, normal/material partition)` 拆分或去重 render vertex，并重映射 face indices；corner UV 应优先于 point UV。
- **测试计划：** 两个三角形共享一个 point，但对应 corner UV 不同；FBX readback 后断言该点被拆成两个 vertex，且两组 UV 都保留。

## 五轮审查摘要

- **正确性：** 确认 F1-F4；均有明确触发链路和可观察影响。
- **重复代码：** 未确认本次新增重复导致独立功能缺陷；多份 manifest/rule 镜像属于同步风险，但当前提交未发现可证明的不一致。
- **架构与依赖：** F1 和 F3 位于跨语言信任边界；F2 缺少 scope-aware 参数身份模型；F4 在 exporter 层丢失 Core 已表达的数据域语义。
- **抽象成本：** 未发现应单独列项的过度抽象；F2 不宜用“递归搜 ID”这种低成本补丁掩盖 definition/instance 区分。
- **Bug 预测：** 非法外部图、子图化已有参数、畸形 HeightField host upload、带 UV seam 的几何分别是四个最小复现场景。

## Confirmed Dead Code

未确认 `main...HEAD` 中存在可安全删除且没有反射、序列化、Native 或 Editor 注册用途的新增死代码。

## 基线问题（不计入本分支 findings）

`PcgGraphRunner.cs` 与 `PcgRuntimeRunner.cs` 引用了 `PcgParameterApplicator`，而对应 `.cs/.meta` 被 `.gitignore` 排除且未提交。不过相同引用和忽略规则已存在于 `main`，因此它是目标分支已有问题，不是 `dev/07-w3` 引入的回归。本审查不据此阻塞当前分支结论。

## 验证结果

- `cmake --build pcg-core/build -j 2`：通过。
- `ctest --test-dir pcg-core/build --output-on-failure`：35/35 通过。
- FBX exporter release / readback build：通过。
- FBX exporter release / readback tests：各 1/1 通过。
- Unity/Tuanjie Editor 测试未纳入本轮验证；按用户要求只做已提交代码层审查。
- 当前未跟踪的 `aot-procedural-house` 目录及 `.meta` 属于并行 Cursor 工作，不在审查输入内，也未被修改。

## Verdict

**Needs changes.** 四个 Major 都处于核心数据链路或内存安全边界；建议修复并补齐对应回归测试后再合入 `main`。本轮未发现 Critical，也未发现可确认的新增死代码。
