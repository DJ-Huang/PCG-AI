# PICG Agent Extensions

PCG Graph 编图与完整资产交付 Skills（Unity/Tuanjie 与 Web 变体），含共享 Python 脚本与资产验收规范。

## 目录

| 路径 | 内容 |
|------|------|
| `gemini-extension.json` | Codely CLI 扩展清单 |
| `skills/` | PCG Skill 正文与 `shared/` 共享脚本/规范 |

## Skills 索引

[skills/index.md](./skills/index.md)

## 同步到 AI 工具

`Agent/` 目录下执行：

```bash
# macOS / Linux
cd Agent && ./setup.sh

# Windows
Agent\setup.bat
```

取消联接：

```bash
cd Agent && ./setup.sh unlink
Agent\setup.bat unlink
```

| 工具 | 联接目标 |
|------|----------|
| Cursor | `~/.cursor/skills/<skill-name>/` |
| Trae | `~/.trae/skills/`、`~/.trae-cn/skills/` |
| Codely CLI | `~/.codely-cli/extensions/picg-extension/` → `Agent/picg-extension/` |
| Kimi Code | `~/.kimi-code/skills/<skill-name>/` |
| OpenCode | `~/.config/opencode/skills/<skill-name>/` |
| Codex | `~/.codex/skills/<skill-name>/` |

`shared/` 不单独联接，各 skill 通过相对路径引用。

非交互一次性同步全部工具：

```bash
node scripts/setup-agent-skills.mjs --tools all --once
```

## 真源

`Agent/picg-extension/skills/` 为 PICG Agent Skill **真源**。只在此处编辑，改后重跑 `Agent/setup.bat` / `Agent/setup.sh`。
