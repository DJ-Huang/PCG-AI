---
id: pit-pcg-houdini-ui-parity-reference-contract
name: PCG Houdini UI 对齐必须以同一状态参考图为参数契约
severity: medium
rootCauseType: 参考状态错配与通用 Inspector 过度暴露
techStack: [PCG-AI, Unity UI Toolkit, Houdini]
affectedPlatforms: [PCG-AI Unity/Tuanjie Graph Inspector]
relatedConcepts: []
relatedNotes:
  - "[[WorkLog/执行记录/PCG-AI/log-inspector-houdini-layout]]"
  - "[[pit-multi-path-node-explicit-input-mode]]"
tags:
  - type/pitfall
  - area/pcg
  - area/unity-editor
  - area/houdini-ui
verified_status: limited
verified_by: PCG-AI Carve Inspector 同状态参考图对比、Unity Editor 编译、manifest/native 测试与用户视觉验收
verified_date: 2026-07-30
common_assumption: 只要参数名称和控件大致相同，或把后端支持的全部参数都显示出来，就算完成 Houdini UI 对齐。
---

## 问题

PCG Inspector 已把控件压成单行，但与 Houdini 参考面板相比仍显得松散、参数更多、分组不同。常见误判包括：

- 拿 Houdini 的 `Cut` 页签与 PCG 的 `Extract` 页签比较，把 Extract 从属参数当成多余参数；
- manifest 中存在的兼容字段全部自动显示；
- 通用 Inspector 的 `+ / bind` 操作泄漏到需要严格复刻的节点面板；
- toggle 与数值框、slider 分别渲染，窄面板下发生换行。

已在 PCG-AI Carve Inspector 复现并修复；结论仅对当前 PCG-AI Unity/Tuanjie Graph Inspector 验证。

## 根因

置信度：高。

Houdini 参数面板是**状态化 UI 契约**：可见参数由节点版本、页签、radio 和 toggle 共同决定。把 manifest 的“后端能力全集”直接当作“当前 UI 参数集”，或在不同状态间逐项比较，都会制造额外参数和错误分组。通用 renderer 若不理解 companion、条件显隐、禁用态与模式容器，也无法只靠紧凑 CSS 达到完全对齐。

## 修复方案

1. 先记录参考图的版本与完整状态，再建立参数矩阵：

   | 类别 | 处理 |
   |---|---|
   | 常驻参数 | 始终显示，顺序与参考图一致 |
   | 模式条件参数 | 只在相同 tab/radio 状态显示 |
   | 从属参数 | 主 toggle 关闭时按参考语义隐藏或禁用 |
   | 内部兼容参数 | 保留序列化/native 能力，不在 Inspector 暴露 |
   | PCG 专属操作 | 参考图不存在时隐藏 |

2. 复合参数用不可换行的单行容器；明确字段顺序、最小宽度与 flex 行为。
3. 通用 renderer 完整处理 `companionField`、`visibleWhen`、状态重建和 enabled state。
4. 当参考 UI 含嵌套页签、边框分组或特殊复合行时，使用节点专用 renderer；不要为了“通用”牺牲参数契约。
5. 隐藏 UI 字段时保留旧图反序列化和 native 执行能力，避免视觉修复破坏功能兼容。

## 验证方法

1. **复现**：在 Houdini 与 PCG 中选中同一语义节点，固定相同 tab/radio/toggle 状态，逐项记录可见参数、顺序、分组、enabled state 和一行内控件。
2. **改动**：按参数矩阵调整 manifest renderer 或节点专用 renderer；内部兼容参数只隐藏 UI，不删除序列化/native 字段。
3. **通过标准**：
   - 每个模式下可见参数集合、顺序、分组与参考图一致；
   - 复合参数在目标 Inspector 宽度不换行；
   - 无参考图不存在的 `+ / bind` 或内部字段；
   - manifest 副本同步且 validator 通过，Editor 编译通过，节点功能测试通过；
   - 最终由同状态截图或用户人眼完成视觉验收。

## 原始记录

- [[WorkLog/执行记录/PCG-AI/log-inspector-houdini-layout]] — 2026-07-30 Carve Inspector Houdini 精确对齐与用户验收。
