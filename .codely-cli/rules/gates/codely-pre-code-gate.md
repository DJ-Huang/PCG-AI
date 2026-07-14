---
description: 每次修改产品代码前 pre-code 回执（配合 rule-router-gate）
---

# Codely pre-code gate



依赖 **`gates/rule-router-gate`**。本文件覆盖 **每一次** 修改产品路径，而非仅首次。



## 产品路径

**Unity 渲染**：`Packages/`、`Assets/`、`*.shader`、`*.hlsl`、`*.compute`、RenderFeature / ScriptableRenderPass、HMIRP/URP 渲染 C#

**通用源码**：`*.cs`、`*.cpp`、`*.h`、`*.hpp`、`*.cc`、`*.c`、`*.py`、`*.ts`、`*.tsx`、`*.js`、`*.jsx`、`*.go`、`*.rs`、`*.java`、`*.kt`、`*.swift`、`*.m`、`*.mm`、`*.lua`、`*.rb`、`*.php` — 所有产品代码文件，不限项目



## 每次改产品路径前



1. 按 **`gates/rule-router-gate`** 完成域名判断、`rule_search`、`vault_search`（不得复用本会话旧结果）。

2. 在**本次** `Write` / `StrReplace` 产品路径之前，各起一行回执：  

   **`Rules（pre-code）`**：`rule_search` 命中 `rule_id` 与要点，或 `no hit`  

   **`Vault（pre-code）`**：经验命中要点，或 `no relevant hit`



## 跳过



纯 Q&A、仅文档、仅 Git、用户明确禁止改仓库。

