# GitLab 工作流

## 仓库映射

> **单一数据源**：`.codely-link/config/config.json → projects`
>
> 该 JSON 包含所有项目的完整映射（GitLab 数字 ID、路径、本地路径、飞书等），代码通过 `projects.ts` 模块加载。**如需查找项目 ID，请直接查看该文件。**
>
> **优先使用数字 ID**，URL-encoded path 在项目迁移后可能失效。

## 基础规则

- ✅ 本地操作优先
- ❌ 不 Clone、不直接提交 main/master、不删远程分支、不 Force push

---

## Commit Message 规范

**格式**: `<type>: <subject>`（英文）

| type | 说明 | 示例 |
|------|------|------|
| `feat` | 新功能 | `feat: add shadow cascade system` |
| `fix` | Bug 修复 | `fix: resolve light bleeding issue` |
| `docs` | 文档更新 | `docs: update API documentation` |
| `style` | 代码格式 | `style: format code with Prettier` |
| `refactor` | 重构 | `refactor: simplify render pipeline` |
| `perf` | 性能优化 | `perf: optimize shader compilation` |
| `test` | 测试 | `test: add unit tests for lighting` |
| `chore` | 构建/辅助工具 | `chore: update dependencies` |
| `revert` | 回退 | `revert: revert previous commit` |

**Subject 要求**:
- 必须使用英文
- 简洁描述（50 字符以内）
- 首字母小写，句末不加句号
- 使用祈使句语气

---

## Merge Request 规范

**标题格式**: `[类型]: 中文描述`

| 类型 | 说明 | 示例 |
|------|------|------|
| `功能` | 新功能开发 | `功能：添加体积光系统` |
| `修复` | Bug 修复 | `修复：解决阴影渗漏问题` |
| `文档` | 文档更新 | `文档：更新 API 文档` |
| `优化` | 性能或代码优化 | `优化：重构光照系统架构` |
| `重构` | 代码重构 | `重构：简化渲染管线` |
| `测试` | 测试相关 | `测试：添加光照系统单元测试` |
| `构建` | 构建或工具变动 | `构建：更新依赖版本` |

**默认配置**:
- Target: `master` 或 `main`
- Squash: `true`
- Remove source branch: `true`

**MR Description 模板**:

创建 MR 时必须包含以下内容，确保审查者能够快速理解变更意图和影响范围：

```markdown
## ⚡ 快速摘要

- **变更类型**: 功能新增 / Bug 修复 / 性能优化 / 重构
- **影响范围**: 小 / 中 / 大
- **风险等级**: 低 / 中 / 高

---

## 📋 变更概述

简要说明本次变更的目的和背景。

### 架构/功能变化（如适用）

**之前**：
```
旧的处理流程
```

**之后**：
```
新的处理流程
```

---

## 🔄 主要变更

### 1. 架构/功能变更

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `xxx.js` | 新增/修改/删除 | 具体说明 |

### 2. 其他变更

| 文件 | 变更内容 |
|------|---------|
| `xxx.js` | 具体说明 |

---

## 🎯 设计理念

1. **要点 1**：说明
2. **要点 2**：说明

---

## ✅ 测试验证

- [ ] 测试项 1
- [ ] 测试项 2

---

## 📝 使用方式（如适用）

```bash
# 命令示例
```

**必须包含**：
1. **快速摘要** - 变更类型、影响范围、风险等级
2. **变更概述** - 目的和背景
3. **主要变更** - 文件级别的变更表格
4. **设计理念** - 为什么这样设计
5. **测试验证** - 检查清单
6. **使用方式** - 如何使用（如适用）
```

**注意事项**:
- MR 优先使用中文描述
- 专业术语保留英文（如：Shader, Render Pipeline, Bloom, SSR 等）
- Title 简洁明了，不超过 80 字符
- 使用 Emoji 图标增强可读性（📋 🔄 🎯 ✅ 📝）

---

## 禁止操作

| 操作 | 约束 |
|------|------|
| ❌ Clone 仓库 | 使用本地已有代码 |
| ❌ 直接提交到 main/master | 必须通过 MR |
| ❌ 删除远程分支 | 除非明确要求 |
| ❌ Force push | 危险操作 |
| ❌ 随意 push 代码 | **必须用户明确要求** |
| ❌ 随意添加 remote | **必须用户明确要求** |

**Push/Remote 原则**: 仅在用户明确指令时执行（如"推送这些修改"、"添加 remote"），禁止未经确认自动执行。

---

## SSH 配置

**AgentSpace/mcp-server 项目**使用 Ed25519 密钥：
- 私钥：`C:\Users\dongjun.huang\.ssh\id_ed25519_codely`
- 命令：`ssh -i C:/Users/dongjun.huang/.ssh/id_ed25519_codely -o IdentitiesOnly=yes`

**其他项目**使用默认 SSH 密钥（`id_rsa`）。