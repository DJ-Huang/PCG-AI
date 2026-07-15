---
description: PCG-AI pcg-core、.pcg 图、Unity PcgPlugin 开发与联调（含 dylib + manifest 双步）
globs:
  - pcg-core/**/*
  - schema/**/*
  - Unity/Assets/PcgPlugin/**/*
  - Unity/Assets/PCGDemo/**/*.pcg
  - examples/**/*.pcg
  - scripts/build-pcg-core.sh
  - scripts/sync-manifest.sh
alwaysApply: false
---

# PCG-AI（Trae 适配）

> 项目级规则：完整正文见 `.codely-cli/rules/pcg-ai.md`（Codely/Cursor/Trae 共用）。本文件为 Trae 薄适配。

**P0 铁律**：节点输出必须 `emit_geometry()`，禁止 `emit_mesh()` 中间输出；禁止 geometry→mesh→geometry round-trip 绕过缺失属性；新增属性必须先扩展 PcgGeometry 字段。详见 `.codely-cli/rules/pcg-ai.md` §「P0 铁律」。

开发 / 改节点 / 编图前：

1. **Read `.codely-cli/rules/pcg-ai.md`**（项目级规则，不再走 `rule_search`）
2. **`vault_search`**：PCG 踩坑与 Phase 记录
3. 新节点或 C++ 行为变更后 **两步缺一不可**（仓库根）：
   - `scripts/build-pcg-core.sh --copy-to-unity` — 原生 `pcg_validate_graph` / 执行
   - `scripts/sync-manifest.sh` — GraphView / Inspector 与 `schema/node-manifest.json` 对齐
   - **禁止**只跑 `cmake --build`：`pcg-core/build/` 与 `Unity/Assets/PcgPlugin/Plugins/macOS/` 是两份独立 dylib 副本，P/Invoke 加载后者，不同步则 Unity 渲染结果不变
4. 编 `.pcg`：Read `.cursor/skills/pcg-graph-authoring/SKILL.md`；跑 `validate_pcg.py`

漏任一步常见症状：Unity **`UnknownNode`**。改 dylib 后需重启 Unity Editor。
