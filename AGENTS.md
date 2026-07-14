## Codely Added Memories
---
- 飞书操作统一使用 `lark-cli` 官方 CLI（`npm install -g @larksuite/cli`），禁止手写 HTTP。业务特化技能（feishu-task/feishu-messages/feishu-bitable-ticket）保留业务规则，技术实现已迁移到 lark-cli。
- Windows PowerShell 传中文参数给 lark-cli 会产生 UTF-8 mojibake 乱码（GBK→UTF-8 双重编码）。涉及中文参数的 lark-cli 命令必须用 Node.js 脚本调用，禁止用 PowerShell。
- **🚪 开发任务强制门禁（P0，不可跳过）**：任何涉及写码/改码的任务（implement/fix/refactor），**必须**在写第一行代码之前完成以下三步，**无论用户 plan 多详细**：
  1. `rule_search` — 查 `{RULES_ROOT}/gates/rule-router-gate.md` + `codely-pre-code-gate.md`，确认路由与编码前置规则
  2. `vault_search` — 按任务关键词检索 Vault 经验（踩坑、约束、平台能力）
  3. 涉及 C#/Shader 改动时，额外查 Vault `Rules/core/hmirp-rendering-agent` §Stable 清洁约束
  - **禁止**以"用户 plan 已足够详细"为由跳过以上步骤
  - **禁止**以"先 explore 再说"为由跳过——explore 可与检索并行，但检索不可省略
  - 违反 = 产出不可信，必须补检索后重新审查所有改动
- **规则/技能加载**：见 `Documentation/references/skill-rule-loading-contract.md`；开发任务路由见 `{RULES_ROOT}/gates/rule-router-gate.md` + `{RULES_ROOT}/gates/codely-pre-code-gate.md`；Codely CLI 用 `rule-router-gate`（AlwaysApply gate，非 Skill）。
- 写码时注意 Stable 清洁约束：见 Vault `Rules/core/hmirp-rendering-agent` §Stable；迁移评估前必须跑红线扫描（REQ-1~4），FAIL 禁止迁入
- 架构/模块/系统概述类查询：用 `SemanticSearch` / `rg` / `Read` 在代码与文档中取证，交叉验证路径与符号定义
- 路由冲突时（`ambiguous=true`），必须用 `ask_user` 让用户确认真实意图，禁止自动选择
- 审查 MR 时不自行 Approve，不随意修改 MR 描述，需用户明确许可
- 检测到「保存/记录」意图时，由 obsidian-write Skill 统一处理写入 Obsidian Vault，不再走 Notion `save_to_notion` 流程。
- 经验/知识点保存 → Obsidian Vault，由 obsidian-write Skill 统一处理（路径映射、模板、双向链接）。不在 CODELY.md 中重复记录 Vault 结构。
- plan 写入 Obsidian（Decisions/ 或 Features/），通过 obsidian-write Skill
- 修改 shader pass 顺序时，必须同步更新所有引用该 shader 的 C# 代码中的 pass index
- 周报起始/结束日期必须是工作日（数据采集范围可含周末）
- **周报生成前必须用 `ask_user` 询问用户保存路径**，禁止自行假设路径。数据收集完成后、开始 AI 分析前，先确认输出目录和文件名
- "spec"/"双文件"/"spec-driven" → 必须使用 spec-driven Skill，禁止引用已删除的 `workflow/spec-driven`
- spec-driven 完成后必须询问用户是否提交 MR 及目标仓库
- 周报/日报/工作总结 → 必须通过 GitLab API 查远端 commits，禁止仅依赖本地 git log
- 创建 MR 前必须先加载 `workflow/gitlab.md` 规则，按 MR 描述模板填写
- Skill 必须遵循 7 段标准格式：Overview → When to Use → Process → Techniques → Rationalizations → Red Flags → Verification Checklist
- 新建 Skill 参考 `sync/extensions/MySkills/skills/TEMPLATE-SKILL.md` 模板 + `sync/extensions/MySkills/skills/STANDARDS.md` 格式规范
- **Rule gate / Skill** 只改 **`sync/`**（gates、workflow）。路由正文只改 Obsidian **`Rules/`** → `vault-rag reindex`。改 gate 后跑 **`setup.bat`**。
- 平台能力：Vault `Experience/PlatformCapabilities/`；审查 finding 见 `hmirp-review-rules` 五点五；记录平台能力 → obsidian-write + reindex
- 涉及团结仓库（TJ URP）时，必须先用 `ask_user` 询问用户 TJ URP 仓库路径，禁止硬编码或假设路径
- FRP 目录结构原则：如果 tj URP 有某功能，FRP 必须保持 tj URP 的目录结构，在此基础上做扩展。禁止将 tj 文件移到其他路径或删掉后重写；tj 缺失的文件（如 MotionVectorsCommon.hlsl、MotionVectorPass.hlsl）必须补回并保持原路径。FRP 扩展通过 partial class（`.FRP.cs`）或新增文件实现，不修改 tj 原文件。
- spec-driven 执行流程中知识库检索为 P0 强制：Phase 0 / Phase 2 每 Step / Phase 4 每 task 前 `vault_search`；结果写入 plan.md「📚 知识库约束」
- 审查结果飞书通知应私发给 MR 作者，不应发到项目群。发消息前先检查 MR author 字段确定接收人。


## 目录与知识库
- 路由正文 → Vault `Rules/`（`rule_search`）；经验 → `vault_search`；回写 → obsidian-write Skill
- 完整 Skill 触发词 → Vault `Rules/meta/skills-trigger-index`（维护用）
- 非开发任务（GitLab / 文档 / 解释）→ `Read` `{RULES_ROOT}/workflow/` 或对应 Skill

## JSON 计划保存
"保存json计划" → `codely-workflow/{主题}_{YYYY-MM-DD}.json`（不用 save_memory）
