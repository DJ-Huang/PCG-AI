# Code Review Report [S2026071201]

> **Mode**: Local
> **Project**: generic (C++ / C# mixed)
> **Round**: 1
> **Conclusion**: needs_changes
> **Last Review SHA**: 2c8997f181d3fb49f9ff4caf5d639a43fdeeefe5
> **Branch**: dev/0711-01-codely → main

## Summary

🔴 Critical: 0 | 🟠 Major: 2 | 🟡 Minor: 4 | 🔵 Suggestion: 1

7 commits 修复 bevel terminal cap 的绕序和面重建问题，重写 `build_edge_polygons` 使用正确的 `leftv/rightv` 语义（与 Vault Pitfall `pit-bevel-edge-strip-connection` 一致），新增 `fix_winding()` BFS 翻转校正。C# 侧新增 PCG Mode 工具栏和 Spline 切线手柄编辑。核心逻辑正确，但 `RestoreHiddenRenderers` 会错误启用原本禁用的渲染器，`SampleCatullRom` 命名与实际算法不符。

## Findings

### F1 [🟠 Major] RestoreHiddenRenderers 未保存原始 enabled 状态
- **Pass**: 2 - 实现审查
- **File**: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:ApplyOthersDisplayMode / RestoreHiddenRenderers
- **Category**: 资源管理
- **Status**: open
- **Verified**: ✅
- **Detail**: `ApplyOthersDisplayMode` 遍历所有 Renderer（包括 `FindObjectsInactive.Include` 找到的已禁用对象），设置 `renderer.enabled = false` 并加入 `s_HiddenRenderers`。`RestoreHiddenRenderers` 统一设置 `enabled = true`。如果一个 Renderer 在进入 PCG Mode 前就是 `enabled = false`，退出后会被错误地启用。
- **Suggestion**: 在 `ApplyOthersDisplayMode` 中保存原始 enabled 状态（如 `Dictionary<Renderer, bool>` 或 `List<(Renderer, bool)>`），`RestoreHiddenRenderers` 恢复到原始值而非固定 `true`。

### F2 [🟠 Major] SampleCatullRom 函数名与实际算法不一致
- **Pass**: 2 - 实现审查
- **File**: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:SampleCatullRom
- **Category**: 命名规范
- **Status**: open
- **Verified**: ✅
- **Detail**: 函数仍命名为 `SampleCatullRom`，但内部已改为调用 `CubicHermite`。旧的 `CatmullRom` 和 `WrapIndex` 函数已删除。函数签名新增 `tangents` 参数。命名误导后续维护者。
- **Suggestion**: 重命名为 `SampleHermiteSpline` 或 `SampleSpline`。

### F3 [🟡 Minor] DeleteTangentsAtIndices 缺少 tangent/point 数量一致性检查
- **Pass**: 2 - 实现审查
- **File**: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:DeleteTangentsAtIndices
- **Category**: 防御性编程
- **Status**: open
- **Verified**: ✅
- **Detail**: `InsertTangentAtIndex` 检查 `tangents.Count != points.Count - 1` 作为守卫，但 `DeleteTangentsAtIndices` 没有类似检查。如果 tangent 列表与 point 列表不一致（例如之前的插入操作未更新 tangent），`RemoveAt` 可能删除错误的 tangent 或静默跳过。虽然 `idx >= 0 && idx < tangents.Count` 防止越界，但可能产生语义错误的 tangent 数据。
- **Suggestion**: 在删除前添加 `if (tangents.Count != points.Count + sortedIndices.Count) return;` 守卫（删除前 points 已减少，tangents 尚未减少）。

### F4 [🟡 Minor] demo 文件 excludeGroups 与测试不一致
- **Pass**: 3 - 一致性审查
- **File**: Unity/Assets/PCGDemo/lowpoly-sedan.pcg:body_bevel
- **Category**: 数据一致性
- **Status**: open
- **Verified**: ✅
- **Detail**: `lowpoly-sedan.pcg` 中 `body_bevel.excludeGroups` 从 `"cap_start,cap_end"` 改为 `"profile_corner"`，但 `test_bridge_bevel.cpp` 的 `build_parametric_sedan` 仍使用 `"cap_start,cap_end"`。如果这是有意测试不同配置，可以接受；如果遗漏，需统一。
- **Suggestion**: 确认两种 `excludeGroups` 配置是否都需要覆盖。如果 demo 文件的变更是有意的，考虑在测试中也添加 `profile_corner` 配置的覆盖。

### F5 [🟡 Minor] lowpoly-sedan.pcg 缺少文件末尾换行
- **Pass**: 3 - 一致性审查
- **File**: Unity/Assets/PCGDemo/lowpoly-sedan.pcg
- **Category**: 格式规范
- **Status**: open
- **Verified**: ✅
- **Detail**: Diff 末尾显示 `\ No newline at end of file`。
- **Suggestion**: 添加文件末尾换行符。

### F6 [🟡 Minor] pcg-core/build-debug/ 构建产物被版本控制追踪
- **Pass**: 4 - 安全验证
- **File**: pcg-core/build-debug/
- **Category**: 仓库卫生
- **Status**: open
- **Verified**: ✅
- **Detail**: `pcg-core/build-debug/` 下的 `.o` 文件、测试二进制、库文件被 git 追踪并在本次 diff 中修改。这些是 CMake 构建产物，不应纳入版本控制。此为预存问题（非本分支引入），但本分支更新了这些文件。
- **Suggestion**: 将 `pcg-core/build-debug/` 添加到 `.gitignore` 并 `git rm --cached` 移除追踪。

### F7 [🔵 Suggestion] 诊断函数在生产代码中占用较大体积
- **Pass**: 1 - 设计审查
- **File**: pcg-core/src/elements/bevel_blender.cpp:bevel_diag_enabled / analyze_bevel_output / log_bevel_stage / diag_terminal_verts_after_vmesh
- **Category**: 代码组织
- **Status**: open
- **Verified**: ✅
- **Detail**: 约 200 行诊断代码内联在 `bevel_blender.cpp` 中，虽然通过 `PCG_BEVEL_DIAG` 环境变量门控，运行时零开销，但在生产源文件中占用显著体积，增加阅读复杂度。
- **Suggestion**: 考虑将诊断函数提取到独立的 `bevel_diag.hpp` / `bevel_diag.cpp`，或使用 `#ifdef PCG_BEVEL_DIAG` 编译时排除。不阻塞合并。

## Verified Checklist

- [x] Pass 1: 设计审查
- [x] Pass 2: 实现审查
- [x] Pass 3: 一致性审查
- [x] Pass 4: 安全验证
