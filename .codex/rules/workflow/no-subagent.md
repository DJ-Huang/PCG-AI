---
alwaysApply: false
description: 禁止使用 Air 模型 Subagent
---
# 禁止使用 Air 模型 Subagent

**触发条件**：所有任务

**加载方式**：始终生效（workflow 规则）

---

## 核心规则

- **禁止**使用运行在 air 模型上的 subagent——典型为 builtin `explore`（专有 air 模型，如 hy3 / glm4.7，非主 agent 模型）。
- 继承主 agent 模型的 subagent（builtin `general-purpose`、`plan`，以及 custom TOML 定义且未指定独立模型的 subagent）**可以**使用。
- 禁止使用的 subagent 对应的工作（代码搜索、文件查找）**必须**直接使用内置工具完成（`read_file`、`search_file_content`、`glob`、`run_shell_command` 等）。

## 理由

- Air 模型 subagent（如 `explore`）使用与主 agent 不同的轻量模型，推理能力与上下文理解可能不足，产出不可控。
- 继承主 agent 模型的 subagent 保持与主 agent 一致的推理质量，可用于复杂多步任务委托。
- 对 air 模型 subagent 的工作，直接使用内置工具可以保持透明度和效率。
