# sync/ 编辑与多平台适配

> **适用范围**：修改 `sync/` 下任意内容（rules、skills、adapters、config、scripts）时必读。
> 契约与架构见 `Documentation/references/skill-rule-loading-contract.md`、`Documentation/references/multi-platform-sharing.md`。

## 原则

1. **`sync/` 是唯一真源** — 禁止在 `.cursor/rules/`、`.trae/rules/`、目标仓 `.codely-cli/rules/` 等**生成物**上改共享正文。
2. **共享行为一处改、多处同步** — 改完后对受影响目标工程跑 `setup.bat`；Skills 变更需重启 IDE。
3. **薄适配与厚正文分离** — 跨工具相同的流程/约束写在 `sync/rules/` 或 `sync/extensions/MySkills/skills/`；仅某工具特有的映射写在对应适配层。

## 平台与输出（改前对照）

| 平台 | 共享正文 | 薄适配 / 发现层 | 同步方式 |
|------|----------|-----------------|----------|
| Cursor | `sync/rules/`、`sync/extensions/MySkills/skills/` | `sync/rules/gates/` → `.cursor/rules/gates/*.md` | `setup.bat` 复制 rules；skills junction 到 user |
| Trae | 同上 | gate 经 `setup.bat` 转为 `.trae/rules/` | 同上 |
| Codely CLI | 同上 | `rule-router-gate.md` 作手册 | skills junction；rules 复制到目标 `.codely-cli/rules/` |

## 按修改类型的检查清单

### 改 Rule（`sync/rules/`）

- [ ] 行为是否**所有平台一致**？是 → 只改正文；否 → 正文保持共享，差异放进 `gates/` 或 `adapters/`。
- [ ] **路由正文**（Obsidian `Rules/`）改后 → `vault-rag reindex`；更新 Vault `Rules/meta/rules-vault-index`（若增删 rule_id）。
- [ ] **Gate**（`gates/`）改后 → `setup.bat` 同步到目标工程。
- [ ] 新增/删除 rule_id → 更新 gate §2、`rule-router-gate.md`、Vault `Rules/meta/rules-vault-index`；Skill 触发词变更时更新 `Rules/meta/skills-trigger-index`。
- [ ] **禁止**在 `sync/rules/` 恢复 `core/`、`agents/`、`reviews/` 目录。
- [ ] 改门禁（`gates/rule-router-gate.md`、`codely-pre-code-gate.md`、`file-write-integrity.md`、`codely-shared.md`）→ 对照现有 gate 的 frontmatter（`description`、`alwaysApply`）与占位符 `.codely-cli/rules`、`~/.codely-cli/extensions/MySkills/skills`（由 `scripts/rule-format.mjs` 在同步时替换）。
- [ ] 文中工具名使用 Codely 原生名时 → 在 `gates/codely-shared.md` 补 Cursor/Trae 映射或降级说明（参考现有表格写法）。

### 改 Skill（`sync/extensions/MySkills/skills/`）

- [ ] 符合 `STANDARDS.md` 七段格式；`description` 含触发词。
- [ ] 引用规则用相对路径或 `.codely-cli/rules`，**不复制**规则正文到 Skill。
- [ ] 出现 `activate_skill`、`ask_user`、`codely_agent_*` 等 → 确认 `codely-shared` 有等价路径。
- [ ] 新增 Skill → 更新 `extensions/MySkills/skills/index.md`。

### 改适配层（`sync/adapters/`）

- [ ] **保持薄**：只写加载契约、工具映射、平台路径；workflow 正文链到 `sync/rules/` / Skill。
- [ ] Cursor MCP 示例：只改 `sync/adapters/cursor/mcp.json.example`，勿提交含密钥的 `mcp.json`。

### 改配置 / 脚本（`sync/config/`、`sync/scripts/`）

- [ ] `config.json` 不入库；只改 `config.example.json`。
- [ ] 脚本路径在 Skill 中写目标仓运行时路径时，确认 `setup.bat` / 联接文档仍成立。

## 参考现有做法（改前先读）

| 场景 | 参考文件 |
|------|----------|
| 跨工具加载顺序 | `skill-rule-loading-contract.md` |
| Cursor 工具映射 | `sync/rules/gates/codely-shared.md` |
| 开发任务路由 | `sync/rules/gates/rule-router-gate.md` |
| Skill 格式 | `sync/extensions/MySkills/skills/STANDARDS.md` |
| 同步命令 | `LINK-GLOBAL.md`、`sync/README.md` |

## 改后必做

1. 对**每个使用中的目标工程**执行 `setup.bat`（至少同步 rules；skills 变更时含 skills 选项）。
2. 若影响全局记忆或加载契约 → 更新根目录 `CODELY.md` 短指针。
3. 若改架构说明 → 同步 `Documentation/references/cursor-sharing.md` 或 `multi-platform-sharing.md`。

## 禁止

- 在 `.cursor/rules/`、项目 `.trae/rules/`、user profile 联接目录当第二真源编辑。
- 把整份 Skill/Rule **复制**到 `adapters/`。
- 只改 Cursor 而不更新 `sync/`，导致 Codely CLI / Trae 分叉。
