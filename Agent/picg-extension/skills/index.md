# PCG Skills 索引

> PCG Graph 编图与完整资产交付 Skills。遵循 MySkills 七段结构；共享脚本与资产规范在 `shared/`（联接时跳过，仅供相对路径引用）。

## Unity / Tuanjie 编图

| Skill | 触发词 | 说明 |
|-------|--------|------|
| [pcg-graph-authoring-unity](./pcg-graph-authoring-unity/SKILL.md) | `/pcg-graph-authoring-unity` / `.pcg` / PCG 编图 / Houdini layout / SceneView 审查 | PCG Graph 编图 + **强制 Tuanjie MCP 干净场景截图对比**（旧名 `pcg-graph-authoring` 仅 redirect） |
| [pcg-graph-authoring-unity-dev](./pcg-graph-authoring-unity-dev/SKILL.md) | `/pcg-graph-authoring-unity-dev` / PCG 管线评估 / 编图能力门控 | 父 skill 功能不变 + **Pipeline Capability Gate**：节点缺失/不足时调研 Houdini 与成熟方案并停止，不继续产 `.pcg` |

## Web 编图

| Skill | 触发词 | 说明 |
|-------|--------|------|
| [pcg-graph-authoring-web](./pcg-graph-authoring-web/SKILL.md) | `/pcg-graph-authoring-web` / PCG web 编图 / 三视图 / three-view / WebGL 审查 | PCG Graph 编图 + **强制 Vite + pcg-server 干净页面截图对比**；正/侧/顶正交图按视图验收 |
| [pcg-graph-authoring-web-dev](./pcg-graph-authoring-web-dev/SKILL.md) | `/pcg-graph-authoring-web-dev` / PCG web 管线评估 / 三视图 | Web 父 skill 功能不变 + Pipeline Capability Gate |

## 兼容重定向

| Skill | 说明 |
|-------|------|
| [pcg-graph-authoring](./pcg-graph-authoring/SKILL.md) | 旧名 → `pcg-graph-authoring-unity` |

## 共享资源（非独立 Skill）

| 路径 | 说明 |
|------|------|
| [shared/pcg-scripts/](./shared/pcg-scripts/) | 通用 Python 编排脚本（plan、validate、layout、comparison sheet、parse_pcgr） |
| [shared/pcg-unity-complete-asset/](./shared/pcg-unity-complete-asset/) | Unity 完整资产工作流与验收规范 |
| [shared/pcg-web-complete-asset/](./shared/pcg-web-complete-asset/) | Web 完整资产工作流与验收规范 |

---

`Agent/picg-extension/skills/` 为 PICG Agent Skill **真源**（PICG 仓内）。各 skill 内 `SHARED_DIR` / `SHARED_SCRIPTS_DIR` 仍按相对路径解析；勿在联接层当第二真源编辑。
