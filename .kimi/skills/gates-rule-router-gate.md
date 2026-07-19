---
name: gates-rule-router-gate
description: 开发任务强制规则路由（域名 + rule_search + vault_search）
---
# Rule router gate

**手册**：`~/.kimi/skills/rule-router/SKILL.md`

## 何时执行

用户任务含 **写/改/实现/调试/review/审查/重构/优化** 等开发意图，或显式 `/rule-router`。

**跳过**：纯 Q&A、仅文档、仅 Git、纯 `vault_search` / `rule_search`、纯 GitLab（改读 `workflow/gitlab.md`）、HMIRP 手册（`create-hmirp-doc` Skill）、用户明确禁止改仓库。

## 1. 域名（四选一）

| 域名 | 强信号关键词 |
|------|----------------|
| **Unity 渲染** | hmirp, unity, shader, hlsl, 渲染管线, render feature, pass, 材质, 光照, 后处理, stable, Runtime.Stable, FRP, sync-tj, 合并 URP |
| **ComfyUI** | comfyui, custom node, ta-toolkit, node_base, tensor |
| **PCG** | pcg, .pcg, PcgGeometry, BevelMesh, SweepAlongSpline, pcg-graph, pcg-core, MergeMesh, RevolveMesh, 编图 |
| **通用** | （未命中上述域名强信号的开发任务，自动归入） |

弱信号 → 让用户选域（Cursor 用 `AskQuestion`，Trae/Codely 在对话中追问）。未命中任何强信号 → 自动 **通用**，不跳过后续步骤。

> PCG 与 Unity 同时出现时（如「Unity 里跑 .pcg」）：优先 **PCG**（编图/节点/几何连线）；仅改 Unity Editor UI 且无 graph 语义时用 Unity 渲染。

## 2. rule_search（写产品代码前必须确认）

路由规则**唯一真源**：Obsidian `Rules/`（`VAULT_ROOT/Rules`）。

**禁止**用 `vault_search` 代替规则检索；**禁止** `Read` `.kimi/skills/core/`、`agents/`、`reviews/`（本地不应存在这些目录）。

首次修改产品路径（见 `gates/codely-pre-code-gate`）**之前**：

1. `user-vault-rag.rule_search(query=任务技术关键词 + 意图, domain=unity|comfyui|pcg, top_k=5)`（**通用**域名传 `domain=null`）
2. 命中后按需 `vault_get_chunk(chunk_id=…)` 读完整规则正文；同一 `rule_id` 的多个 chunk 只保留与任务最相关的要点
3. 建立本会话**检索账本**：`domain + intent + 技术关键词 + 影响模块/路径`，并记录命中 `rule_id`、关键约束和已读 `chunk_id`
4. **通用**域名 `rule_search` 可能 `no hit` — 此时回执写 `no hit (general domain)`，**但 vault_search 仍必须执行**

后续写码前必须检查检索账本，不得跳过检查；按范围选择状态：

| 状态 | 条件 | 行为 |
|------|------|------|
| `fresh` | 本会话尚无账本；域名或意图变化；已知 Rules/Vault 重建索引或规则更新；旧结果不足或互相冲突 | 完整执行 `rule_search` + `vault_search`，覆盖账本 |
| `reused` | 同一会话、同一域名与意图，当前技术关键词和影响模块均被账本覆盖，且未发现索引/规则变化 | 不调用 RAG；直接复用已记录约束 |
| `delta` | 域名与意图不变，但新增技术关键词、模块、API、平台或风险假设 | 只用新增关键词补查 `rule_search` + `vault_search`，按 `rule_id` / `file_path` / `chunk_id` 去重后合并账本 |

新会话不得假设持有旧会话账本。此前的 `no hit` 在检索键完全相同时可 `reused`；新增关键词时必须 `delta`。涉及新平台能力、Shader Pass、GPU 资源/API 假设时至少执行 `delta`。

| 域名 | 意图 | 应命中 rule_id（检索词示例） |
|------|------|------------------------------|
| Unity | 始终 | `core/role`, `core/anti-ai-trace` |
| Unity | 写码 | `core/hmirp-rendering-agent` |
| Unity | 架构/文档/模块说明 | `agents/ta-render-expert` |
| Unity | review | `reviews/hmirp-review-rules`, `agents/shader-expert` |
| Unity | 调试 | `agents/test-engineer` |
| ComfyUI | 始终 | `core/comfyui`, `core/anti-ai-trace` |
| ComfyUI | review | `reviews/comfyui-review-rules`, `agents/comfyui-expert` |
| PCG | 始终 / 编图 | `pcg/index`, `pcg/assembly-bevel` |
| PCG | 按模型类型 | `pcg/vehicle`, `pcg/bridge`, `pcg/spiral-staircase`, `pcg/scatter` |
| 通用 | — | （无域专属规则，`no hit` 正常） |

**AlwaysApply gates**（`rule-router-gate`、`codely-pre-code-gate` 等）仍在 `.kimi/skills/gates/`，由 `setup.bat` 同步，不迁入 Vault。

**完整 rule_id 清单**（维护者）：Obsidian `Rules/meta/rules-vault-index`（`rag_index: false`，直接 Read 或维护时查阅）。

## 3. vault_search（经验知识，与规则分开 — 所有域名强制）

`user-vault-rag.vault_search(query=任务技术关键词 + 任务类型, top_k=5)` — Pitfall / Tip / Concept / BugFix。规则正文必须用 §2 `rule_search`（仅 `Rules/`）。

首次检索与 §2 同批执行；后续与 §2 共用同一检索账本和 `fresh` / `reused` / `delta` 判定。返回结果按 `file_path` 去重，同一笔记默认只保留一个最相关 excerpt；需要细节时再用 `vault_get_chunk`。

### 为什么通用域名也必须搜

Vault 中有**跨域通用**的经验知识，不绑定特定技术栈：

- **Concept**（如 `cpt-agent-complex-bug-playbook`）— 复杂 bug 修复方法论，适用任何项目
- **Pitfall**（如 `pit-debug-layer-misattribution`）— 层归因错误，通用调试陷阱
- **Tip**（如 `tip-diagnose-before-fix`）— 先诊断再修复，通用工程纪律

**搜索策略**：query 应包含任务类型关键词（debug / refactor / implement / review）+ 技术关键词，不要只搜技术名。

**禁止**以「这是通用项目，Vault 里不会有相关经验」为由跳过 vault_search。

## 4. 回执（改代码/出审查结论前一行）

`Router（dev, <fresh|reused|delta>）：<域名> | key: <意图+关键词+模块摘要> | rules: <rule_id 列表> | vault: <笔记 id/摘要或 no hit>`

`reused` 只引用既有 `rule_id` / 笔记 id 和约束摘要，不重复粘贴 excerpt。`delta` 只报告新增、删除或发生冲突的命中。

## 5. 委派子 Agent

子 Agent 不继承 alwaysApply。主 Agent 先完成 §1–§4，再在 prompt 注入硬约束摘要（含 rule_search 命中要点）。
