---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/triview
source_path: PCG AI Rule/Graph Authoring/triview.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: web review camera presets, Graph Authoring Plan v2, capture_webview_png per-view receipts
verified_date: 2026-08-16
---

# 三视图重建与节点精编

参考图重建时，正/侧/顶正交图是几何真源。单张透视或 3/4 视图只能当完整性检查，不能单独锁定宽、高、深。

## 输入

- 三视图角色固定为 `front` / `side` / `top`。用户先发一张图时：**停下按顺序要图**（正视图 → 侧视图 → 顶视图），一回合只要一个视角；禁止裁切合成板、禁止从一张图脑补另外两张。
- 用户明确说只用单图时才保持 `mode=single`。
- PCG 物体空间：左手系、`upAxis=+y`。`frontAxis` 默认 `+z`，`sideView` 默认 `right`。写入计划 `coordinateFrame`，后续相机与 `TransformMesh` 都用它。
- 每个视图必须落到本地归档文件；聊天附件和 URL 不是记忆。

## 跨视图约束

在写节点前记录并校验：

| 尺寸 | 驱动视图 | 要求 |
|---|---|---|
| width | front + top | 正数米制、单一 owner、tolerance ∈ (0, 0.1] |
| height | front + side | 同上 |
| depth | side + top | 同上 |

两视图差值超过 tolerance 时写入 `conflictResolutions` 并采用更清晰的那张图，禁止悄悄平均。每个宏部件还要有 manifest 证据的 `componentHypotheses`（`chosenNodeType` + `dimensionDrivers`）。

## 节点编辑精度

1. 先查当前 manifest，禁止发明 type / pin / property。
2. 按真实截面选 primitive：矩形用 `CreateBoxMesh`，等截面用 sweep，回转体用 revolve；禁止用一个盒子同时冒充三视图。
3. 尺寸只来自约束 owner，单位米；禁止对着透视截图像素估尺寸。
4. 一次修正针对一个 mismatch；改完尺寸必须重拍全部必选视图。
5. 身份细节必须 `mapsTo` 真实 node/property。

装配倒角、布局、Subgraph 仍遵守 `pcg/assembly-bevel` 与 `pcg/graph-contract`。

## 验收

- 每个必选视图用确定性正交相机（front/side/top）对比归档参考图。
- 解锁分是 **最差必选视图**，平均分不能掩盖失败视图。
- `three-quarter` 只做完整性：透视塌陷、隐藏穿插、厚度丢失时，即使三张正交图过线也不得 `continue`。
- `validate_pcg.py` 通过不等于三视图还原成功。
