---
rag_index: false
tags: [domain/pcg, project/pcg-ai, area/golden-graphs]
---

# PCG Golden Graphs（编图参考库）

**不进 vault-rag 索引**（已写入库根 `.ragignore`：`PCG AI Rule/Golden Graphs/**`）。  
Agent 用 **文件系统** Glob / Read 本目录，不要用 `rule_search` / `vault_search` 找这里的图。

路径（相对 Vault 根）：`PCG AI Rule/Golden Graphs/`  
绝对路径示例：`$VAULT_ROOT/PCG AI Rule/Golden Graphs/`

## 给 Agent 的读取方式

新建图写 `.pcg` 前（规则加载之后）：

1. `Glob`：`VAULT_ROOT/PCG AI Rule/Golden Graphs/<class>/*.{pcg,md}`（class = vehicle|bridge|building|prop|scatter|other，或先列子目录）。
2. 读匹配的 **`.md` 卡片**（若有）与同名 **`.pcg`**。
3. 仍以 manifest + `pcg/graph-contract` / `pcg/assembly-bevel` / 类型规则为准；与规则冲突时 **改图服从规则**。

**禁止**：把 PCG-AI 工程仓库里的 `examples/*.pcg`、Unity demo `.pcg` 当作同类参考。

## 目录结构

| 子目录 | 放什么 |
|--------|--------|
| `vehicle/` | 车、轮、车身等 |
| `bridge/` | 桥、栏杆、墩 |
| `building/` | 建筑、立面、lot/city 模块 |
| `prop/` | 罐体、道具、硬表面单体 |
| `scatter/` | 散布 / 实例化范例 |
| `other/` | 未归类但仍达标的图 |

每个范例建议一对文件（同 basename）：

- `<slug>.md` — 可选摘要卡片（本目录不进 RAG，仅给人/Agent 直读）
- `<slug>.pcg` — 完整 JSON（主参考）

## 入库质量门槛（须全部满足）

- [ ] 当前 manifest 可 cook；`validate_pcg.py` 无 error
- [ ] 多部件：bevel 在 Merge 前按件完成（`pcg/assembly-bevel`）
- [ ] 真实世界尺度（米）或卡片中明确标注 stylized
- [ ] 每节点有唯一 `__nodeTitle`；自上而下 + `COL_STEP_X ≥ 320`
- [ ] 无已知反模式

未达标放 `Inbox/`，不要放进本目录。

## 新增范例

1. 复制 `_template.md` 为 `<slug>.md`，放到对应子目录。
2. 放入同名 `<slug>.pcg`。
3. **无需** `vault-rag reindex`（本目录被 ignore）。
