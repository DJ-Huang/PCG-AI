---
name: pcg-kb-write
description: 把 PCG-AI 工程经验安全写入项目内 .pcg-ai/kb/ 知识库（BM25 检索）。三种模式：Quick Capture（碎片进 kb/inbox/）、Distill（泛化后写入 kb/pitfalls|concepts|tips|research/）、Distill Inbox（整理 kb/inbox/）。触发词：记到 PCG 知识库、写入 pcg kb、沉淀 PCG 踩坑、整理 pcg inbox、/pcg-kb-write。
---

# pcg-kb-write

把内容安全写入 PCG-AI 工程内置知识库 `.pcg-ai/kb/`，分为三种模式：

- **Quick Capture**：保存碎片或用户原话到 `kb/inbox/`，不做泛化。
- **Distill**：先判断能否跨功能复用，再检索、路由、确认、合并、校验。
- **Distill Inbox**：逐条整理 `kb/inbox/inb-*.md`，成功蒸馏后才删除原条目。

知识库根路径为工程根下 `.pcg-ai/`；禁止硬编码绝对路径，所有写入必须落在该目录内。任何写入前先用 `pcg_kb_status` 确认索引根与状态。检索工具：`pcg_kb_search` / `pcg_kb_get` / `pcg_kb_list`；写入后必须 `pcg_kb_reindex`。本工程知识库**不走** Obsidian Vault / vault-rag（那是全局经验库，由 obsidian-write Skill 负责）；若内容值得跨项目复用，提示用户另走 obsidian-write。

## When to Use

- 用户说"记到 PCG 知识库""沉淀 PCG 踩坑""写进 pcg kb""整理 pcg inbox"或 `/pcg-kb-write`。
- 用户要求新建或更新本工程的 Pitfall、Concept、Tip、Research 笔记。
- 用户只想快速备忘、随手记或保留原话。

不要把当前功能的实现步骤、改动文件、临时 workaround、时间线或未验证结论写成 `pit-*`、`cpt-*`、`tip-*`；这类内容放 `kb/inbox/` 或工程 `mem-log/` / `Plan/`。

## Process

### 1. 选择模式

- "随手记 / 记一下 / 备忘 / 快速记录" → **Quick Capture**。
- "写入知识库 / 新建笔记 / 总结通用经验" → **Distill**。
- "蒸馏 / 整理 inbox" → **Distill Inbox**。
- 无法判断时默认 Quick Capture；用户已明确类型、目录或路径时不重复询问。

### 2. Quick Capture

`pcg_kb_status` 确认后直接写入，不要求确认或双向链接。`id` 必须与文件名一致，正文保留用户原话：

```markdown
---
id: "inb-YYYYMMDD-HHmmss"
createdTime: "YYYY-MM-DDTHH:mm:ss"
tags: ["type/inbox", "area/pcg"]
---

{用户原话}
```

路径：`.pcg-ai/kb/inbox/inb-{YYYYMMDD-HHmmss}.md`。

### 3. Distill

1. **定位**：`pcg_kb_status`；按关键词、平台、类型 `pcg_kb_search(query, category="kb")`；`pcg_kb_list("kb")` 浏览已有条目。找到同主题笔记时优先合并，可用 `pcg_kb_get(path)` 读全文。
2. **路由**：先判是否跨功能，再判笔记类型。类型不明时用 `AskQuestion`；用户已指定则按用户指定。

   - `kb/pitfalls/` `pit-*`：可复现的坑，含根因与验证方法。
   - `kb/concepts/` `cpt-*`：机制、架构、模式类知识。
   - `kb/tips/` `tip-*`：简短可操作建议。
   - `kb/research/`：调研报告与决策依据，保留日期与范围。
   - `kb/inbox/` `inb-*`：碎片，不作为主库经验。
3. **Schema**：无独立模板目录，frontmatter 参照 `kb/` 下已有同类型笔记（如 `pit-pcg-assign-material-binding`、`cpt-pcg-cook-policy`）。Pitfall 至少含 `id`、`name`、`severity`、`rootCauseType`、`techStack`、`tags`、`verified_status`、`verified_by`、`verified_date`，正文必须有 `## 验证方法`；Concept/Tip 至少含 `id`、`name`、`tags`、`verified_status`。
4. **确认与预览**：用户未指定时确认类型、路径和 `id`；展示目标路径、frontmatter 摘要，以及一句"可跨功能复用的机制"。若无法泛化，停止 Distill，改为 inbox 或 mem-log。
5. **写入**：`id = {前缀}-{标题 kebab-case}`；`/ : # ? *` 替换为 `-`，冲突时追加 `-2`、`-3`。已有笔记只能合并/追加，不能静默整篇覆盖；同时更新 `verified_date` 或编辑日期。
6. **验证与索引**：落盘后 `Read` 抽查 frontmatter 和正文，报告相对路径、`id`、`verified_status` 及新建/合并结果；随后必须 `pcg_kb_reindex`，并用一次 `pcg_kb_search` 确认新条目可被检索。

### 4. Distill Inbox

扫描 `kb/inbox/inb-*.md`，为每条给出摘要和建议类型，由用户选择创建、合并、跳过或删除；随后按 Distill 流程处理。只有目标笔记成功写入并校验后，才删除 inbox 源文件；批量整理可合并为一次确认。

### 5. 可信度

`verified_status` 表示可靠性，不表示严重程度；`severity` 仅表示 Pitfall 严重程度。没有可复现证据和验证方法时用 `unverified`；有项目/平台依据但范围有限时用 `limited`；不得凭推测写 `verified_true`，不得写无依据的"全平台"。高严重度 Pitfall 必须给出可执行步骤和通过标准。

## Techniques

### 泛化门禁

写入 `pitfalls/`、`concepts/`、`tips/` 前，问：换功能、换模型类型后这条是否仍成立？正文应描述机制、边界、前提、验证和可迁移做法，而不是当前图的接线或调试流水。

### 路由与合并

先 `pcg_kb_search` 查同主题，再决定新建或合并；标题和 `id` 用机制名，不用当前功能名、工单号、分支名或日期堆砌。

### 与全局 Vault 的分工

- PCG-AI 工程机制、pcg-server/native/Web 编辑器踩坑 → 本库（`pcg_kb_*`）。
- 跨项目通用经验（Unity 编辑器通坑、协作流程、全局平台能力）→ obsidian-write → Vault。
- 同一条经验两边都有价值时，主写本库，提示用户可另存 Vault；不要静默双写。

### 证据与降级

不确定是否通用或是否已验证时，宁可保留在 inbox/mem-log 或标 `unverified`，不要为了"总结"硬凑主库笔记。平台、版本、设备和性能数字必须保留适用条件与证据范围。

## Common Rationalizations

| 说法 | 正确处理 |
|---|---|
| "既然已经总结了，就顺手建一个 Pitfall。" | 先过泛化门禁；功能专属内容降级到 inbox 或 mem-log。 |
| "这条看起来没问题，先标 `verified_true`。" | 根据可复现证据和验证方法设状态，不凭直觉提高可信度。 |
| "新建比查重省事。" | 先 `pcg_kb_search`；已有主题优先合并，避免知识分叉。 |

## Red Flags

- ❌ 写入路径不在工程 `.pcg-ai/` 下，或硬编码绝对路径。
- ❌ 把 PCG-AI 工程经验写进 Obsidian Vault（应写本库），或未走 obsidian-write 就写全局库。
- ❌ `pit-*`、`cpt-*`、`tip-*` 的标题或正文仍依赖当前功能、某张具体图或单次调试流水。
- ❌ 未 `pcg_kb_search` 查重就新建，或未经确认静默覆盖已有笔记；未完成蒸馏就删除 inbox 源文件。
- ❌ 用"全平台""永远"等绝对结论替代平台/版本/设备条件，或没有证据却标 `verified_true`。
- ❌ 写入后未 `pcg_kb_reindex`，导致 `pcg_kb_search` 检索不到新条目。

## Verification Checklist

### 写入前

- [ ] 已 `pcg_kb_status` 确认索引根为工程 `.pcg-ai/`。
- [ ] Distill 已完成泛化判断，并已 `pcg_kb_search` 查重。
- [ ] frontmatter 参照已有同类型笔记，无臆造字段。

### Quick Capture

- [ ] 路径为 `kb/inbox/inb-{timestamp}.md`，且 `id` 与文件名一致。
- [ ] frontmatter 只有 `id`、`createdTime`、`tags`；正文是用户原话。

### Distill

- [ ] 类型、路径、`id` 无冲突，且用户已看到预览或已明确指定。
- [ ] 主库内容已去除当前功能壳；Pitfall 有验证方法，可信度与证据一致。
- [ ] 已合并而非静默覆盖，写后已 `Read` 抽查。
- [ ] 已 `pcg_kb_reindex` 并用 `pcg_kb_search` 验证可检索。

### Distill Inbox

- [ ] 已完成目标笔记写入和校验后才删除 inbox 源文件。
- [ ] 合并时保留原有有效段落，并报告每条的处理结果。
