---
id: pit-multi-path-node-explicit-input-mode
name: 多输入路径节点须用显式 inputMode，禁止「有数据就切换」
severity: medium
rootCauseType: 路由与 UI 契约误用数据存在性
techStack: [PCG-AI, Unity GraphView, node-manifest]
affectedPlatforms: [PCG-AI Unity/Tuanjie Graph Editor / Inspector]
relatedConcepts: []
relatedNotes:
  - "[[pit-pcg-houdini-ui-parity-reference-contract]]"
  - "[[WorkLog/执行记录/PCG-AI/log-reference-projection-pipeline]]"
tags:
  - type/pitfall
  - area/pcg
  - area/unity-editor
  - area/graph-authoring
verified_status: limited
verified_by: PCG-AI OutlineSolid inputMode（outline / columnsJson / spineEdge）+ visibleWhen；用户否定数据存在性自动路由（2026-08-01）
verified_date: 2026-08-01
common_assumption: 多个可选输入并存时，谁有数据就走谁，或同时展示全部 pin/参数，对用户最方便。
---

## 问题

节点同时支持多种互斥输入路径（例如闭合轮廓挤出、JSON 站表、双样条图编）时，若用「某个 pin 有连线 / 某个 JSON 字段非空」自动选路径：

- Inspector 会同时堆出各模式字段，UI 嘈杂、难对照 Houdini 式「当前状态只露当前参数」；
- cook 可能静默走错分支（例如残留 JSON 盖过图编，或反过来）；
- 缺输入时回退到另一模式，错误被掩盖，难排查。

已在 PCG-AI `OutlineSolid` 上复现：初版 `spine+edge → columnsJson → outline` 自动优先级被用户否决，改为显式 `inputMode`。

## 根因

置信度：高。

**数据存在性 ≠ 用户意图。** 图上可同时残留多路数据（旧 JSON、未删边、默认空数组）。把存在性当路由器，等于隐式状态机；与「模式条件参数只在相同 radio/tab 显示」的 UI 契约冲突（见 [[pit-pcg-houdini-ui-parity-reference-contract]]）。

## 修复方案

1. 增加显式枚举属性（如 `inputMode`：`outline` | `columnsJson` | `spineEdge`），作为**唯一**路径选择器。
2. 执行层：只读当前 mode 对应输入；缺输入则**报错**，禁止回退到其它模式。
3. UI：属性与 pin 用 `visibleWhen` / `oneOf` 按 mode 显隐；GraphView 同步隐藏无关端口。
4. 旧图迁移：写入显式 mode，勿依赖「有 JSON 就当 columns」的隐式兼容（若必须兼容，仅限一次性迁移脚本，不进运行时默认）。

## 验证方法

1. **复现**：同一节点填满两路数据（如 `columnsJson` 非空且 `spine`/`edge` 已连），在「自动路由」下确认路径与预期不符或 UI 同时露出无关字段。
2. **改动**：加 `inputMode`；执行只认 enum；Inspector/端口条件显隐。
3. **通过标准**：
   - 切换 mode 后仅见该模式 pin 与参数；
   - 选中 mode 但对应输入缺失 → 可观测错误，不静默换路；
   - 两路数据并存时，结果只随 `inputMode` 变化；
   - 相关 `.pcg` 带显式 mode；manifest 同步与 ctest 通过。

## 原始记录

- [[WorkLog/执行记录/PCG-AI/log-reference-projection-pipeline]] — 2026-08-01 OutlineSolid 双模式 → `inputMode` 显式切换。
