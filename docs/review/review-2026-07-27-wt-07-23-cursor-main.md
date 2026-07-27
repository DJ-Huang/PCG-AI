# Code Review: `wt/07-23-cursor` → `main`

## 审查范围

- Source: `wt/07-23-cursor` / `c5022d8ef649a821f5668ffa734d91ca0ac7c824`
- Target: `main` / `5d953c2d6feebf877c29ab45bc6d1d80283163d2`
- Merge base: `5d953c2d6feebf877c29ab45bc6d1d80283163d2`
- 范围：16 commits，94 files，约 15,431 additions / 2,896 deletions
- 重点：正确性、崩溃边界、跨 C++/C#/Web 契约、发布二进制一致性、无效配置、过度抽象与测试缺口

## 结论

**阻塞合并。**

已确认 1 个 Critical、6 个 Major、2 个 Minor。最严重的问题允许一份合法图在结果统计阶段抛出未捕获的 C++ 异常，直接终止宿主进程；此外，Vector3 兼容逻辑会吞掉 Graph Parameter 覆盖，提交中的原生插件二进制也没有与当前源码同步。

本次变更未引入明显的鉴权绕过、数据外泄或代码注入面，但 C ABI 未捕获异常属于重要的可靠性与宿主安全边界问题。

## Findings

### F1 — Critical：合法的部分 spline group 会越过 C ABI 抛异常并终止宿主进程

**位置**

- `pcg-core/src/graph_executor.cpp:514-535`
- `pcg-core/src/graph_executor.cpp:1158-1160`
- `pcg-core/src/elements/topology_parity_algorithms.cpp:1131-1140`
- `pcg-core/src/pcg_core.cpp:769-778`

**触发条件**

1. 图输出 spline geometry。
2. 某个 primitive attribute 只存在于后面的 spline，而不在第一条 spline 上。`MeasureMesh` 对 range group 的写入可以自然产生这种合法状态。
3. 执行结束后统计 group/attribute 信息。

**影响**

`collect_group_stats()` 先收集所有 spline 的 attribute 名称，随后却固定从 `splines().front().attributes.at(name)` 取样。属性仅存在于后续 spline 时抛出 `std::out_of_range`。异常没有在 `pcg_execute_graph` 的 C ABI 边界被捕获，会调用 `std::terminate`，Unity Editor 或其他宿主进程可直接退出。

**证据**

使用公开 ABI 构造并执行：

`CreateBoxMesh → ConvertLine(connectPath=false) → MeasureMesh(useRangeGroup=true, rangeGroup=inrange, 部分选中) → Output`

新编译 dylib 与仓库提交的 Unity macOS dylib 都以退出码 134 终止，并输出：

```text
libc++abi: terminating due to uncaught exception of type std::out_of_range
unordered_map::at: key not found: inrange
```

**建议修复**

- 统计属性时保存真正包含该属性的代表值，或逐条 spline 查找第一个包含该名称的属性；不要从第一条 spline 无条件 `.at()`。
- 在所有公开 C ABI 顶层增加异常兜底，将异常转换为 `PCG_ERR_EXECUTION` 和错误消息，避免任何 C++ 异常穿越 C ABI。

**必须补充的测试**

- 经公开 ABI 执行上述部分 range group 图，断言返回 `PCG_OK`，并正确输出 primitive `length/inrange` 统计。
- 增加一个故意触发内部异常的测试，断言 C ABI 返回错误而不是终止进程。

### F2 — Major：Vector3 兼容优先级判断反了，`p_lift` 等旧轴绑定被静默忽略

**位置**

- `Unity/Assets/PcgPlugin/Runtime/PcgVector3Property.cs:86-103`
- `pcg-core/src/elements/element_utils.cpp:374-420`
- `Unity/Assets/PcgPlugin/Runtime/PcgGraphSerializer.cs:437-468`
- `examples/lot-city-buildings-procedural.pcg:478`
- `Unity/Assets/PcgPlugin/Examples/Graphs/lot-city-buildings-procedural.pcg:556`

**触发条件**

节点同时存在：

- 新 canonical vector 属性，例如 `"translate": [0, 0, 0]`；
- 旧轴属性或 Graph Parameter 覆盖，例如 `"translateY": 2`；
- canonical 值仍等于 manifest 默认值。

**影响**

C# 和 C++ 两份解析器都因为条件中的

```text
!Approximately(parsed, legacy)
```

在 canonical 默认值与 legacy 值不同时提前返回 canonical，恰好违背代码注释所述的兼容规则。当前示例仍把 `p_lift` 绑定到 `translateY`，因此 Inspector 中修改参数不会移动几何体，也不会报错。

**复现证据**

通过公开 ABI 对比三份图：

- 基准：只有 `translate: [0,0,0]`
- legacy-only：`translateY: 2`
- mixed：`translate: [0,0,0]` 与 `translateY: 2`

legacy-only 会移动几何体；mixed 的输出与基准完全相同，证明覆盖被吞掉。

**建议修复**

- canonical 等于 manifest fallback 且任一 legacy axis 存在时，应优先合并 legacy axis；移除当前反向条件。
- 更稳妥的架构是：反序列化时只做一次迁移并保存 canonical vector，同时迁移 Graph Parameter 的 `targetProperty`；native 只保留窄范围的兼容读取。

**必须补充的测试**

- C# 序列化器与 native ABI 共用 fixture：canonical 默认值 + legacy Y=2 应得到 `[0,2,0]`。
- 对当前示例执行 `p_lift=0` 与 `p_lift=1`，断言几何输出不同。

### F3 — Major：提交的 Unity 原生插件与 HEAD 源码不同步，macOS 和 Windows 行为分裂

**位置**

- `Unity/Assets/PcgPlugin/Runtime/PcgNative.cs:10-18`
- `Unity/Assets/PcgPlugin/Plugins/macOS/libPcgCore.dylib.meta:19-25`
- `Unity/Assets/PcgPlugin/Plugins/x86_64/PcgCore.dll.meta:19-25`
- `pcg-core/src/elements/delete_algorithms.cpp:468-553`
- `Unity/Assets/PcgPlugin/Examples/Graphs/lot-city-buildings-procedural.pcg:455-458`

**影响**

- macOS 插件二进制最后更新于 `b08ee9d`，但 HEAD 之后又修改了 Delete 的 numeric group / pattern / group table 行为；当前 Unity 示例依赖这些后续语义。
- Windows `PcgCore.dll` 沿用更早的版本，二进制字符串中不存在本分支新增节点（包括 Delete）。
- CTest 验证的是本地新编译产物，不是 Unity 实际加载的已提交插件，因此测试通过不能证明交付包可运行。

**证据**

- 当前源码重新构建的 dylib 与提交的 dylib SHA-256 不同。
- 当前源码重新构建的静态库与提交的静态库 SHA-256 不同。
- Windows DLL 在本分支没有更新，且未包含新增节点注册信息。
- Editor 路径明确通过 `PcgNative` 加载 `PcgCore`，两平台 importer 均启用。

**建议修复**

- 从 HEAD 重建并同步 macOS dylib/static library。
- 重建 Windows DLL/import library。
- CI 中校验插件二进制对应当前 commit，并对嵌入 registry/manifest 或一组关键节点做烟测。

**必须补充的测试**

- 在 macOS 和 Windows 的干净 checkout 中，由 Unity 实际加载分发插件，执行包含新 Delete 语义的图，并与源码构建的 native test 结果比对。

### F4 — Major：Pivot Rotate 的逆旋转顺序错误，主变换为 identity 时仍会破坏几何

**位置**

- `pcg-core/src/elements/transform_algorithms.cpp:167-190`

**问题**

代码用相同的 `"xyz"` Euler 顺序和三个负角构造 `pivot_rotate_inverse`。多轴旋转不交换，复合旋转的逆需要逆矩阵或相反的应用顺序，不能只把每个角度取负后维持原顺序。

**影响**

仅设置 `pivotRotate=[30,45,60]`，而 translate、rotate、scale 等主变换保持 identity，本应执行 `P × I × P⁻¹ = I`，实际却改变所有顶点。

**复现证据**

经公开 ABI 执行 `CreateBox → TransformMesh`：

- 基准：无 pivot rotate
- 对照：只设置多轴 pivot rotate，主变换 identity

两份 mesh bytes 不同，首批顶点发生明显位移。

**建议修复**

直接对 pivot rotation matrix 求逆，或按数学上正确的逆序构造逆旋转；不要手工假设 Euler 角取负即为同序逆变换。

**必须补充的测试**

- 参数化多个非零轴的 pivot rotate，主变换 identity 时，输出在 epsilon 内与输入一致。
- 非 identity 主变换与可信矩阵组合实现逐点比对。

### F5 — Major：Web Inspector 不支持新增 `vector3` 契约，TransformMesh 核心属性只能查看、不能编辑

**位置**

- `schema/node-manifest.json:5960-6005`
- `web/pcg-editor/src/nodeManifest.ts:8-20`
- `web/pcg-editor/src/Inspector.tsx:211-305`

**影响**

manifest 为 TransformMesh、MatchSize、Bend 等节点新增了 `vector3` 属性，但 Web 侧：

- `PropertyType` 不包含 `vector3`；
- default value 类型不接受数组；
- Inspector switch 没有 `vector3` 编辑器，落入只读 fallback；
- manifest 通过 `as unknown as NodeManifest` 强转，类型检查无法发现契约漂移。

本分支又移除了旧的逐轴 scalar 属性，因此 translate/rotation/scale/pivot 等核心参数在 Web 编辑器中形成实际功能回退。

**建议修复**

- 增加严格的 `vector3` 类型与三数值编辑控件。
- 删除 `unknown` 双重断言，或在加载时执行共享 schema 校验。
- 将 manifest validator/contract tests 覆盖到 C++、Unity 和 Web 三个消费者。

**必须补充的测试**

- Web 导入或创建 TransformMesh，分别编辑 x/y/z，JSON round-trip 后由 native 执行并验证几何变化。

**验证限制**

本地 Web 依赖未安装，`npm run build` 因 `tsc: command not found` 无法执行；上述结论来自静态控制流和 manifest 契约，不依赖构建失败。

### F6 — Major：ConvertLine 的全部 Inspector 字段不可见，manifest validator 已明确失败

**位置**

- `schema/node-manifest.json:8253-8345`
- `Unity/Assets/PcgPlugin/Editor/PcgNodeInspector.cs:431-470`
- `scripts/validate-manifest.py:98-113`

**问题与影响**

ConvertLine 的 9 个属性全部声明 `section: "main"`，但节点没有定义 `inspectorSections`。Unity Inspector 只会先渲染无 section 的 top-level 属性，再遍历已定义 sections，因此这些字段全部不会显示。

**复现证据**

```text
python3 scripts/validate-manifest.py schema/node-manifest.json
```

退出非零，并报告 ConvertLine 的 9 个属性引用了不存在的 `main` section。三份 manifest 副本哈希一致，问题会同步影响分发副本。

**建议修复**

为节点增加 `main` section，或移除这些属性的 section 标记；把 manifest validator 设为提交门禁。

**必须补充的测试**

- validator 必须返回 0。
- Unity Editor 选中 ConvertLine 后，断言 group、connect path、max points 等字段可见且可编辑。

### F7 — Major：TransformMesh 暴露了没有任何运行时效果的公共选项

**位置**

- `schema/node-manifest.json:6074-6093`
- `pcg-core/src/elements/transform_algorithms.cpp:406-442`

**问题**

manifest 暴露 `Attributes` 与 `Recompute Affected Normals`。执行代码会把它们解析进 options，但仓库中没有任何其他读取：

- `options.attributes` 不限制被变换的 vector attributes；
- `options.recompute_affected_normals` 不影响 normal 重计算。

**影响**

UI 向用户承诺了不存在的行为。尤其在局部 group 变换中，默认开启的 `Recompute Affected Normals` 会让用户误以为边界法线已修复，实际可能留下错误阴影。额外 options/manifest/UI 面也增加维护成本，却没有功能收益。

**建议修复**

实现完整语义；如果当前迭代不准备实现，应移除公共字段与 options，避免提前扩张 API。

**必须补充的测试**

- 局部 group 变换中，开关 affected normals 应产生预期差异。
- attribute filter 应保证未选中的 vector attribute 不被修改。

### F8 — Minor：Vertex group 的 corner ID 被当成 point ID，Scene View 高亮会缺失或落在错误点

**位置**

- `pcg-core/src/graph_executor.cpp:55-104`
- `pcg-core/src/elements/geometry_algorithms.cpp:926-935`
- `Unity/Assets/PcgPlugin/Editor/PcgCreateSplineSceneHandles.cs:2702-2716`

**问题**

group stats 对 Point 和 Vertex domain 使用同一逻辑：直接将 group ID 作为 `points` 数组索引。但项目的 Vertex ID 是全局 face corner ID，需要先经 corner → point 映射。

**影响**

例如 box 有 24 个 corners、8 个 points。corner ID 8–23 会被丢弃，0–7 也不保证对应正确 point；Unity Scene View 使用这些 `pointPositions` 绘制 Vertex group，高亮会不完整或错误。

**建议修复**

按 face/corner table 将 global corner ID 映射到 point index，或输出独立的 corner positions 字段。

**测试建议**

对带重复 corners 的 box 创建 Vertex group，断言返回位置数量及坐标与实际选中 corners 一致。

### F9 — Minor：Preview setters 不再清理旧 mesh，RuntimeRunner 在结果类型切换后残留旧几何

**位置**

- `Unity/Assets/PcgPlugin/Runtime/PcgPreview.cs:43-57`
- `Unity/Assets/PcgPlugin/Runtime/PcgRuntimeRunner.cs:52-82`

**问题与影响**

`SetPoints` / `SetSplines` 删除了原有的 `SetMesh(null)`。`PcgGraphComponent` 在部分路径上会显式清理，但 `PcgRuntimeRunner` 直接按结果类型调用 setter，没有先清理。

同一 Runner 先运行 Mesh 图，再改为 Splines 或 Points 图时，旧 `MeshFilter.sharedMesh` 仍会显示，与新结果叠加。

**建议修复**

区分“替换主结果”的 setter 与“叠加 preview”的 setter；RuntimeRunner 使用 replacement API，或在结果 switch 前统一 `ClearAll()`。

**测试建议**

同一 Runner 连续应用 Mesh → Splines、Mesh → Points，断言 renderer 禁用且 `sharedMesh == null`。

## 确认的死代码 / 无效配置

- `TransformMeshOptions.attributes` 与 `recompute_affected_normals` 只写不读，已作为 F7 报告。
- `ConvertLineOptions.keep_group_order` 与 `remove_unused_points` 在 `pcg-core/src/elements/facade_foundation_algorithms.cpp:528-529` 以显式 no-op 结束，但 manifest 已向用户暴露。至少 `keepGroupOrder` 是无效配置；如果当前 spline 表示中 `removeUnusedPoints` 没有可观察语义，也不应公开。
- 没有发现值得为了“形式统一”而删除的大批 helper/class；主要浪费来自未实现就公开的配置面，而不是类数量本身。

## 架构与抽象评价

### 1. 兼容迁移分散在两套运行时，已经产生同源 bug

`PcgVector3Property` 与 native `read_vector_param` 复制了同一套默认值/legacy precedence 策略，并复制了同一个布尔错误。建议将迁移决策集中在图反序列化阶段，保存 canonical 数据；native 只保留必要的输入容错。这样比继续抽取更多跨语言“概念相似”的 wrapper 更可靠。

### 2. Manifest 是跨三端 API，却缺少真正的契约门禁

C++ registry、Unity Inspector、Web Inspector 都消费同一 manifest，但 Web 用 `unknown` 强转绕过类型系统，Unity section 问题也只有未接入门禁的 Python validator 能发现。这里值得增加共享 schema/codegen/contract tests；不建议为每个平台再包一层不受验证的 adapter。

### 3. Affine/group helper 存在重复，适合做小范围合并

`assembly_algorithms.cpp` 与 `transform_algorithms.cpp` 重复了 group domain 解析、point 收集、affine 应用、矩阵辅助逻辑。它们涉及完全相同的几何语义，继续分叉容易在 corner/point、normal、pivot 规则上漂移。可以合并为一组窄而纯的 geometry utilities，但没有必要引入 interface/factory 或深层继承。

### 4. Delete 实现体量大，但不能仅凭行数判定过度封装

Delete 已按 selection predicate 与 geometry rebuild 阶段拆分。当前更重要的是补足跨 domain/反选/pattern/group table 的行为测试，再依据重复和变化轴决定是否继续拆分；不建议为了缩短单文件而进行无证据的抽象。

## 预测风险与开放问题

- C ABI 缺少统一异常边界。F1 只是已复现的一条路径，其他 `.at()`、JSON type conversion 或 allocation 异常也可能终止宿主。
- Delete 的 domain × pattern × invert × numeric group 组合空间较大，现有测试不足以证明所有组合在 point/vertex/primitive/detail 上保持引用完整性。
- group stats 现在可为每个节点输出完整 face polygons 与 point positions；大型图的 result JSON 可能明显膨胀，需要定义采样/上限和性能预算。
- 两份变更的 `.pcg` 经图验证器检查时还出现 SpatialGeometry pin 兼容性告警。由于 validator 可能尚未理解新 pin kind，本次不直接判为产品 bug，但工具与新 schema 已发生契约漂移，应在合并前澄清。
- `git diff --check main...HEAD` 报告 6 个新增 `.meta` 文件有 trailing whitespace，建议清理并加入基础格式门禁。
- 未在真实 Unity/Tuanjie Editor 中执行 Scene handles、Undo、Inspector GUI 集成测试；Mono msbuild 只能证明编译，不覆盖编辑器交互行为。

## 测试与验证

| 检查 | 结果 |
|---|---|
| `cmake --build pcg-core/build -j 4` | 通过 |
| `ctest --test-dir pcg-core/build --output-on-failure -j 4` | 40/40 通过 |
| Mono msbuild: `PcgPlugin.Runtime` | 通过 |
| Mono msbuild: `PcgPlugin.Editor` | 通过，有警告 |
| Mono msbuild: `PcgPlugin.Editor.Tests` | 通过 |
| Manifest validator | **失败：ConvertLine 9 个无效 section 引用** |
| F1 公开 ABI 崩溃复现 | **复现，退出码 134** |
| F2 vector legacy override 复现 | **复现，mixed 输出等于基准** |
| F4 pivot identity 复现 | **复现，输出 mesh 被改变** |
| Web build | 未执行成功：本地缺少 `tsc`/依赖 |
| `git diff --check` | 失败：6 个新增 `.meta` trailing whitespace |

已有 native/managed 测试全部通过仍未覆盖 F1、F2、F4，说明当前回归集对公开 ABI、数据迁移和矩阵不变量的覆盖不足。
