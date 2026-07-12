# Code Review [S2026071201]

> Mode: Local | Project: generic (C++ / C# mixed)
> Round: 2 | Conclusion: can_merge
> Last Review SHA: 2a80496aa2a85ec23ba226dd98e7fb83f7d26269
> Branch: dev/0711-01-codely → main

---

## 总结

### 功能概述

本次变更涉及 PCG-AI 项目的 **bevel 倒角网格生成** 和 **Unity 编辑器 Spline 场景手柄** 两个功能领域。核心动机是修复 bevel terminal cap（终端盖面）的绕序（winding）错误和面重建缺陷，同时为 Unity 编辑器增加 PCG Mode 工具栏和 Spline 控制点切线手柄编辑能力。关键技术决策上，C++ 侧参考 Blender `bmesh_bevel.cc` 的 `leftv/rightv` 语义重写了 edge strip 连接逻辑（与 Vault Pitfall `pit-bevel-edge-strip-connection` 的正确做法一致），新增 BFS 绕序传播校正；C# 侧将 Spline 曲线插值从 Catmull-Rom 改为 CubicHermite 以支持显式切线编辑。

### 变更要点

- **Bevel 核心 C++（bevel_blender.cpp, bevel_blender.hpp）**：从 `build_boundary` 分离出 `build_boundary_terminal_edge` 专门处理 selcount==1 终端边场景；重写 `build_edge_polygons` 使用 `e->leftv` + `e2->leftv` 连接 edge 两端 profile（修复原代码误连同一 BevVert 左右侧的 bug）；新增 `NewVert::valid` 标志防止无效 VMesh 点参与三角化；新增 `fix_winding()` BFS 传播翻转 + 有符号体积校正保证整体外朝向；`bevel_build_poly` 插入 ebev profile 中间点；`set_profile_params` 为 collapsed terminal cap 合成 profile span
- **Unity 编辑器 C#（PcgCreateSplineSceneHandles.cs）**：新增 PCG Mode 进入/退出流程（工具栏、选择锁定、Hide/Isolate 模式）；新增切线手柄编辑（FreeMoveHandle 拖拽、镜像 in/out tangent）；选择逻辑重构为 select-then-drag UX（先选中再拖拽）；程序化生成 16x16 工具栏图标
- **Spline 运行时（PcgSplineControlPoints.cs）**：新增 tangent 解析/序列化/计算 API；`GetTangents` 优先返回显式切线，否则计算 Catmull-Rom 切线
- **GraphView 架构（PcgGraphView.cs, PcgGraphEditorWindow.cs, PcgNodeInspector.cs）**：新增 `SceneEditContext` / `SceneEditLevel` / `SceneEditDomain` 结构驱动 SceneView 状态
- **测试（test_bridge_bevel.cpp）**：新增 `count_boundary_loops` 边界环计数工具；新增 `build_parametric_sedan` 参数化轿车构建；新增 18 配置（cap×segments×path_length）回归测试
- **数据/配置（lowpoly-sedan.pcg, node-manifest.json, .gitignore）**：CreateSpline 节点新增 `tangents` 字段；demo 文件 JSON 格式重排版；`.gitignore` 补充 `build-debug/` 和 `*.DS_Store`
- **工程卫生**：`pcg-core/build-debug/` 全部构建产物从 git 移除（Round 2 修复）

### 审查评价

核心 bevel 修复逻辑正确，`build_edge_polygons` 重写严格遵循 Blender 语义，`fix_winding()` BFS + 有符号体积校验算法覆盖完整。测试覆盖充分——18 配置参数化回归覆盖了 cap/segments/path_length 组合空间。C# 侧 `RestoreHiddenRenderers` 原始状态保存和切线一致性守卫在 Round 2 已修复。主要残余风险为 F7（诊断函数内联生产代码），属非阻塞建议，可在后续迭代中提取。

统计：🔴 Critical: 0 | 🟠 Major: 0 | 🟡 Minor: 0 | 🔵 Suggestion: 1
结论：can_merge — 全部 Major/Minor 已修复，仅剩 1 个非阻塞建议。

审查覆盖：
- [x] Pass 1: 设计审查
- [x] Pass 2: 实现审查
- [x] Pass 3: 一致性审查
- [x] Pass 4: 安全验证
- [x] Round 2 增量 diff 审查完成

---

## Round 2 Finding Status

| # | 级别 | Round 1 状态 | Round 2 状态 | 修复方式 |
|---|------|-------------|-------------|---------|
| F1 | 🟠 | open | **fixed** | `HashSet<Renderer>` → `Dictionary<Renderer, bool>`，保存原始 enabled 状态 |
| F2 | 🟠 | open | **fixed** | `SampleCatullRom` → `SampleHermiteSpline`，调用方同步更新 |
| F3 | 🟡 | open | **fixed** | 新增 `pointCountAfterDeletion` 参数 + `tangents.Count != pointCountAfterDeletion + sortedIndices.Count` 守卫 |
| F4 | 🟡 | open | **fixed** | `excludeGroups` 改回 `"cap_start,cap_end"`，与测试一致 |
| F5 | 🟡 | open | **fixed** | 添加文件末尾换行符 |
| F6 | 🟡 | open | **fixed** | `pcg-core/build-debug/` 加入 `.gitignore`，所有构建产物从 git 移除 |
| F7 | 🔵 | open | open（非阻塞） | 诊断函数内联在生产源文件，建议后续提取 |

## Findings

### F1 [🟠 Major] RestoreHiddenRenderers 未保存原始 enabled 状态
- **Pass**: 2 - 实现审查
- **File**: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:ApplyOthersDisplayMode / RestoreHiddenRenderers
- **Category**: 资源管理
- **Status**: fixed ✅
- **Verified**: ✅ (Round 2)
- **Detail**: `ApplyOthersDisplayMode` 遍历所有 Renderer（包括 `FindObjectsInactive.Include` 找到的已禁用对象），设置 `renderer.enabled = false` 并加入 `s_HiddenRenderers`。`RestoreHiddenRenderers` 统一设置 `enabled = true`。如果一个 Renderer 在进入 PCG Mode 前就是 `enabled = false`，退出后会被错误地启用。
- **Suggestion**: 在 `ApplyOthersDisplayMode` 中保存原始 enabled 状态（如 `Dictionary<Renderer, bool>` 或 `List<(Renderer, bool)>`），`RestoreHiddenRenderers` 恢复到原始值而非固定 `true`。
- **Fix**: `s_HiddenRenderers` 改为 `Dictionary<Renderer, bool>`，存储 `renderer.enabled` 原始值，`RestoreHiddenRenderers` 恢复 `originalEnabled`。

### F2 [🟠 Major] SampleCatmulRom 函数名与实际算法不一致
- **Pass**: 2 - 实现审查
- **File**: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:SampleCatmulRom
- **Category**: 命名规范
- **Status**: fixed ✅
- **Verified**: ✅ (Round 2)
- **Detail**: 函数仍命名为 `SampleCatmulRom`，但内部已改为调用 `CubicHermite`。旧的 `CatmulRom` 和 `WrapIndex` 函数已删除。函数签名新增 `tangents` 参数。命名误导后续维护者。
- **Suggestion**: 重命名为 `SampleHermiteSpline` 或 `SampleSpline`。
- **Fix**: 重命名为 `SampleHermiteSpline`，调用方同步更新。

### F3 [🟡 Minor] DeleteTangentsAtIndices 缺少 tangent/point 数量一致性检查
- **Pass**: 2 - 实现审查
- **File**: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:DeleteTangentsAtIndices
- **Category**: 防御性编程
- **Status**: fixed ✅
- **Verified**: ✅ (Round 2)
- **Detail**: `InsertTangentAtIndex` 检查 `tangents.Count != points.Count - 1` 作为守卫，但 `DeleteTangentsAtIndices` 没有类似检查。如果 tangent 列表与 point 列表不一致（例如之前的插入操作未更新 tangent），`RemoveAt` 可能删除错误的 tangent 或静默跳过。虽然 `idx >= 0 && idx < tangents.Count` 防止越界，但可能产生语义错误的 tangent 数据。
- **Suggestion**: 在删除前添加 `if (tangents.Count != points.Count + sortedIndices.Count) return;` 守卫（删除前 points 已减少，tangents 尚未减少）。
- **Fix**: 新增 `pointCountAfterDeletion` 参数，检查 `tangents.Count != pointCountAfterDeletion + sortedIndices.Count` 时 return。

### F4 [🟡 Minor] demo 文件 excludeGroups 与测试不一致
- **Pass**: 3 - 一致性审查
- **File**: Unity/Assets/PCGDemo/lowpoly-sedan.pcg:body_bevel
- **Category**: 数据一致性
- **Status**: fixed ✅
- **Verified**: ✅ (Round 2)
- **Detail**: `lowpoly-sedan.pcg` 中 `body_bevel.excludeGroups` 从 `"cap_start,cap_end"` 改为 `"profile_corner"`，但 `test_bridge_bevel.cpp` 的 `build_parametric_sedan` 仍使用 `"cap_start,cap_end"`。如果这是有意测试不同配置，可以接受；如果遗漏，需统一。
- **Suggestion**: 确认两种 `excludeGroups` 配置是否都需要覆盖。如果 demo 文件的变更是有意的，考虑在测试中也添加 `profile_corner` 配置的覆盖。
- **Fix**: `excludeGroups` 改回 `"cap_start,cap_end"`，与测试一致。

### F5 [🟡 Minor] lowpoly-sedan.pcg 缺少文件末尾换行
- **Pass**: 3 - 一致性审查
- **File**: Unity/Assets/PCGDemo/lowpoly-sedan.pcg
- **Category**: 格式规范
- **Status**: fixed ✅
- **Verified**: ✅ (Round 2)
- **Detail**: Diff 末尾显示 `\ No newline at end of file`。
- **Suggestion**: 添加文件末尾换行符。
- **Fix**: 添加文件末尾换行符。

### F6 [🟡 Minor] pcg-core/build-debug/ 构建产物被版本控制追踪
- **Pass**: 4 - 安全验证
- **File**: pcg-core/build-debug/
- **Category**: 仓库卫生
- **Status**: fixed ✅
- **Verified**: ✅ (Round 2)
- **Detail**: `pcg-core/build-debug/` 下的 `.o` 文件、测试二进制、库文件被 git 追踪并在本次 diff 中修改。这些是 CMake 构建产物，不应纳入版本控制。此为预存问题（非本分支引入），但本分支更新了这些文件。
- **Suggestion**: 将 `pcg-core/build-debug/` 添加到 `.gitignore` 并 `git rm --cached` 移除追踪。
- **Fix**: `pcg-core/build-debug/` 加入 `.gitignore`，所有构建产物从 git 移除。

### F7 [🔵 Suggestion] 诊断函数在生产代码中占用较大体积
- **Pass**: 1 - 设计审查
- **File**: pcg-core/src/elements/bevel_blender.cpp:bevel_diag_enabled / analyze_bevel_output / log_bevel_stage / diag_terminal_verts_after_vmesh
- **Category**: 代码组织
- **Status**: open（非阻塞）
- **Verified**: ✅
- **Detail**: 约 200 行诊断代码内联在 `bevel_blender.cpp` 中，虽然通过 `PCG_BEVEL_DIAG` 环境变量门控，运行时零开销，但在生产源文件中占用显著体积，增加阅读复杂度。
- **Suggestion**: 考虑将诊断函数提取到独立的 `bevel_diag.hpp` / `bevel_diag.cpp`，或使用 `#ifdef PCG_BEVEL_DIAG` 编译时排除。不阻塞合并。

## Verified Checklist

- [x] Pass 1: 设计审查
- [x] Pass 2: 实现审查
- [x] Pass 3: 一致性审查
- [x] Pass 4: 安全验证
- [x] Round 2 增量 diff 审查完成
- [x] F1-F6 全部 fixed，无新增问题
