---
alwaysApply: true
description: 每次修改产品代码前 pre-code 回执（配合 rule-router-gate）
---

# Codely pre-code gate



依赖 **`gates/rule-router-gate`**。本文件覆盖 **每一次** 修改产品路径，但不要求对相同范围重复调用 RAG。



## 产品路径

**Unity 渲染**：`Packages/`、`Assets/`、`*.shader`、`*.hlsl`、`*.compute`、RenderFeature / ScriptableRenderPass、HMIRP/URP 渲染 C#

**通用源码**：`*.cs`、`*.cpp`、`*.h`、`*.hpp`、`*.cc`、`*.c`、`*.py`、`*.ts`、`*.tsx`、`*.js`、`*.jsx`、`*.go`、`*.rs`、`*.java`、`*.kt`、`*.swift`、`*.m`、`*.mm`、`*.lua`、`*.rb`、`*.php` — 所有产品代码文件，不限项目



## 每次改产品路径前



1. 按 **`gates/rule-router-gate`** 检查本会话检索账本，判定为 `fresh`、`reused` 或 `delta`。只有 `fresh` / `delta` 调用 RAG；`reused` 必须确认域名、意图、技术关键词和影响模块仍被账本覆盖。

2. 在同一用户需求的**连续写码批次首次写入前**各起一行回执；后续单个 `Write` / `StrReplace` 若检索状态与约束未变化，不重复回执。范围变化时先补查并重新回执：

   **`Rules（pre-code, fresh|reused|delta）`**：`rule_search` 命中 `rule_id` 与要点，或 `no hit`

   **`Vault（pre-code, fresh|reused|delta）`**：经验笔记 id 与新增/复用要点，或 `no relevant hit`

3. `reused` 回执只列 id 和一行约束摘要，不重复粘贴旧 excerpt；`delta` 回执只列新增、删除或冲突内容。

**连续写码批次**：同一用户需求、同一域名/意图、按同一实现计划连续修改一组相关文件。用户切换需求，或出现新模块、API、平台、Shader Pass、GPU 能力假设时，批次结束并重新判定。



## 跳过



纯 Q&A、仅文档、仅 Git、用户明确禁止改仓库。
