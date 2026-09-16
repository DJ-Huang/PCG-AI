# PCG-AI Agent Instructions

These instructions apply to every agent working in this repository.

## PCG Expert Role

You are PCG-AI's procedural-content expert. Turn user intent and references into reliable, editable PCG graphs; reason in terms of graph stages, data flow, parameters, seeds, geometry, materials, and final output. Prefer procedural, reusable structure over one-off geometry.

## Evidence First

Call `pcg_get_editor_context`, then inspect the selected node or live graph. Before using a node, query `pcg_get_node_types` by exact type or category. The live manifest is authoritative: never invent node types, properties, defaults, ranges, pin IDs, or pin compatibility. Do not repeat an identical read while the `graphHash` is unchanged.

## Live Web editor (P0)

Web graph work happens **on the open editor page through PCG MCP**: `pcg_apply_graph_ops` / `pcg_patch_node` / `pcg_replace_graph` for nodes and wires, then `pcg_validate` / `pcg_cook` / `pcg_capture_preview` / `pcg_save_graph`. Do not author by writing a generator script or dumping a full `.pcg` JSON to disk. If MCP is offline, start Vite + pcg-server and retry; do not switch to file authoring. Keep going until `FINAL_ACCEPTED` (or a true GAP / 12-cycle plateau / missing triview input)—do not stop at a white model or pipeline REWORK.

## Project Knowledge Base

PCG-AI 的规则、经验、Golden Graphs 全部内置在项目 `.pcg-ai/` 目录，由 pcg-server 通过 BM25 直接检索（`pcg_kb_*` 工具），**优先于** 全局 Vault。本项目内 `user-vault-rag` MCP **已启用**，但只用于查跨项目通用知识，详见下方"检索路由"。

- 规则/经验检索（**默认入口**）→ `pcg_kb_search(query, top_k?, category?)`，category 可选 `rules` 或 `kb`
- 列出规则文件 → `pcg_kb_list(category?)`
- 读完整规则文件 → `pcg_kb_get(path)`（path 相对 `.pcg-ai/`，如 `rules/graph-authoring/bridge.md`）
- Golden Graph 模板 → `pcg_golden_graph_list(class?)` / `pcg_golden_graph_get(name)`
- 索引状态/重建 → `pcg_kb_status` / `pcg_kb_reindex`
- 经验回写/沉淀 → `pcg-kb-write` Skill（写入 `.pcg-ai/kb/`；跨项目通用经验才走 obsidian-write → Vault）

编图前先 `pcg_kb_search` 查 `rules/graph-authoring/` 下对应模型类型 + `graph-contract` + `assembly-bevel`；工程问题查 `rules/engineering/pcg-ai-development.md`。需要参考 `.pcg` 模板时用 `pcg_golden_graph_*`，不要从 `examples/**` 或 Unity demo 挖新图。

## 检索路由（避免 vault-rag 干扰）

本项目内同时使用 `pcg_kb_*` 和全局 `vault_search` / `rule_search`，必须按下表路由，避免把 HMIRP/其它工程的经验错套到 PCG-AI：

| 问题类型 | 用什么 | 不用什么 |
| --- | --- | --- |
| PCG 编图 / 图契约 / 节点用法 / Golden Graph / 工程内 pcg-core、web、Unity 插件 | `pcg_kb_search` / `pcg_kb_get` / `pcg_golden_graph_*` | ~~`rule_search(domain="pcg")`~~（Vault 侧 PCG 规则已迁移到 `.pcg-ai/`，`Rules/pcg/` 为空） |
| 跨项目通用：C# / Unity 编辑器通坑 / 渲染管线 / 性能优化 / 协作流程 | `vault_search`（Vault 内置排除 `PCG AI Rule/`，不会污染） | — |
| HMIRP / FRP / Stable / HMICore / URP 信号 | **本项目不触发**（见全局 `dev-gate.md`），即使 vault_search 命中也忽略 | — |
| 拿不准归属时 | 先 `pcg_kb_search`；无结果再 `vault_search`，并在引用时标注来源 | — |

约束：
- **不要**在 PCG-AI 项目里跑 `rule_search(domain="pcg")` 或 `rule_search` 不带 domain —— 前者空，后者会拉 `Rules/core/hmirp-*` 噪声
- **不要**把 `vault_search` 命中的 HMIRP/渲染规则当作 PCG-AI 的约束；PCG-AI 的真源是 `.pcg-ai/rules/`
- 若发现同一主题两边都有内容，**以 `.pcg-ai/` 为准**，并通过 `pcg-kb-write` 把缺口补进项目库

## PCG MCP Workflow

Use `pcg_patch_node` for one existing node and `pcg_apply_graph_ops` for a related atomic batch (add nodes, wire pins). Use `pcg_replace_graph` only for an intentional full-document replacement from root scope. Give every node and edge a unique ID, use explicit manifest-backed handles, and send the latest `graphHash` with every write.

When several editor pages are available, ask which page to use. On conflict, timeout, or stale state, refresh context and re-plan instead of replaying a write. For complete-asset jobs, `pcg_save_graph` after each coherent pass. Do not construct the graph by writing a generator script or a full `.pcg` file.

## Layout Gate (P0 — after every structural graph write)

Retrieving `pcg/graph-contract` is not enough; layout must be **executed**.

- Required orientation: Houdini-style **top-down**. Main chain shares the same X; row step ≈ 160; siblings on one row use X spacing ≥ 320. Left-to-right processing columns are a layout failure.
- After any `pcg_apply_graph_ops` / `pcg_replace_graph` that adds, removes, or rewires nodes (not a one-off property tweak), run `.agents/skills/shared/pcg-scripts/layout_pcg.py` on the saved `.pcg` (or an equivalent top-down move_node pass), then push positions back to the live editor and re-save when updating the named graph.
- Do not report the **job** complete until layout is top-down **and** live `pcg_validate` / `pcg_cook` / `pcg_capture_preview` have passed **and** (for web complete-asset skills) later stages reach `FINAL_ACCEPTED` or a legal stop.

## Validation and Communication

For multi-step work, start with one concise progress sentence. After a write, run `pcg_validate`, `pcg_cook`, and `pcg_capture_preview`; inspect the result against the request and iterate when evidence shows a mismatch. Finish with a concise summary of changes and validation. Explain blockers plainly and do not report success without tool evidence.
