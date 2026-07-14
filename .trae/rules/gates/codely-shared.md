---
alwaysApply: true
---
# Codely Shared Assets



## 路径（本仓库）



| 内容 | 路径 |

|------|------|

| Rules gate（AlwaysApply） | `.trae/rules/gates/`（`setup.bat` 同步） |

| Rules 路由正文 | Obsidian `VAULT_ROOT/Rules/`（`rule_search`，唯一真源） |

| Skills | `~/.trae/skills/`（user 目录全局联接） |

| 记忆 | `AGENTS.md`（真源 `CODELY.md`） |



改 gate / workflow → `{AISYNC_ROOT}/sync/rules/` 后 `setup.bat`。改路由正文 → Obsidian `Rules/` 后 `vault-rag reindex`。维护清单见 `workflow/sync-editing.md`。



## 开发任务



1. **`gates/rule-router-gate`**：域名 → `rule_search` → `vault_search` → `Router（dev）` 回执

2. **每次**改产品路径：`gates/codely-pre-code-gate`（`Rules（pre-code）` + `Vault（pre-code）`）

3. **每次** `Write` / 整文件替换：`gates/file-write-integrity`（写后 `Read` 校验换行）

4. 子 Agent 不继承 alwaysApply；主 Agent 在 prompt 注入约束摘要



## 非开发任务



纯 Q&A、文档、Git、同步等 → `Read` `.trae/rules/workflow/` 或对应 Skill，无需完整路由。



## Loading Contract



- **开发任务** → `rule-router-gate` + **`rule_search`** + `vault_search`；每次改产品代码 → `codely-pre-code-gate`

- **非开发任务** → 直接 `Read` `.trae/rules/workflow/` 等



## Cursor 工具映射



| Codely | Cursor |

|--------|--------|

| `read_file` | `Read` |

| `glob` | `Glob` |

| `grep` | `Grep` |

| `web_search` / `web_fetch` | `WebSearch` / `WebFetch` |

| `ask_user` | `AskQuestion` |

| `activate_skill('name')` | `Read` `~/.trae/skills/name/SKILL.md` |

| Vault 规则 | `user-vault-rag.rule_search`（仅 `Rules/`） |

| Vault 经验 | `user-vault-rag.vault_search` |

| Vault 全文 | `user-vault-rag.vault_get_chunk` |

| Unity | `user-unityMCP` |



## Trae 工具映射



| Codely | Trae |

|--------|--------|

| `activate_skill('name')` | `Read` `~/.trae/skills/name/SKILL.md` |

| `ask_user` | 对话追问并给出选项 |

| 开发路由 | `rule-router-gate`：`rule_search` + `vault_search` |

| Vault 规则 | `user-vault-rag.rule_search` |

| Vault 经验 | `user-vault-rag.vault_search` |



## Codely CLI 工具映射



| 场景 | 做法 |

|------|------|

| 开发路由 | `rule-router` Skill 或 `.trae/rules/gates/rule-router-gate.md` |

| Skills | `~/.trae/skills/<name>/SKILL.md` |

| Rules gate | `.trae/rules/gates/` |

| Rules 正文 | Obsidian `Rules/` via `rule_search` |



MCP（Cursor）：`%USERPROFILE%\.cursor\mcp.json`

