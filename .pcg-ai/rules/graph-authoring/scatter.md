---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/scatter
source_path: PCG AI Rule/Graph Authoring/scatter.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: PCG-AI scatter examples and current manifest; oriented-instance facing note 2026-07-21
verified_date: 2026-07-21
---

# Scatter 建模策略

按数据类型选择链路，禁止只看节点名字连线：

1. 用 `SpawnPoints`、`CreatePointGrid`、`SampleMeshSurface`/`SampleSurface` 或 terrain scatter 生成点。
2. 用 `DensityFilter`、attribute 节点或项目需要的 mask 筛选/扰动点。
3. 需要贴地时用与当前数据类型兼容的 terrain/surface 投影节点。
4. 将真实 prototype 通过 `StaticMeshSpawner`、`PlaceInScene` 或 `CopyMeshToPoints` 放置；以 manifest pin type 决定具体节点。

随机 seed、密度/spacing、scale range 和 prototype style 是高价值 Graph Parameter 候选。实例数量较大时优先复用 prototype，避免在根图展开重复几何链。

## 有向 prototype 的实例朝向（P0 — 与主通道对齐）

当 prototype 有约定正面（建筑门、车头、招牌等），在 `CopyMeshToPoints` / `StaticMeshSpawner` **之前**写入朝向点属性（优先 `rotationY` / `orient`；完整 frame 时 `N`=上、`tangent`=前）。目标默认为最近主通道（道路/路径），不要用大范围随机旋转冒充朝向。建筑 lot-city 细则见 `pcg/building`「主立面朝向主通道」；总述见 `pcg/graph-contract`。

注意：`CopyMeshToPoints` 的 `N` 映射到局部 **Y**，水平朝向应写进 `tangent`（`tx/ty/tz`）或欧拉/`orient`，不要把“朝路向量”单独塞进 `N`。

## Prototype 原点对齐（P0 — 地面贴地规则）

`CreateBoxMesh` / `CreateCylinderMesh` 等生成节点以**几何中心**为原点。当 `CopyMeshToPoints` 将 prototype 放置到 y≈0 的地面点时，prototype 底部一半会沉入地下，表现为"建筑在地面中间"。

### 规则

1. **必须**在 prototype 生成链与 `CopyMeshToPoints` 之间插入 `TransformMesh`，设置 `translateY = height / 2`，使 prototype 底面位于原点 y=0。
2. `height` 取 prototype 实际高度值（`CreateBoxMesh.height`、`CreateCylinderMesh.height` 等），不要用 AABB 估算。
3. 若 prototype 经过 `AttributeRandomize`（随机缩放），仍用原始 height/2 — `CopyMeshToPoints` 的 per-point scale 会在放置时等比缩放整体（含 translateY），缩放后底面仍贴地。
4. 若 prototype 有锥度/变截面且底部不在 y=0（如 `SweepAlongSpline` 以底端为起点），则无需额外偏移。

### 正确拓扑

```text
CreateBoxMesh (height=H) → [BevelMesh] → TransformMesh (translateY=H/2) → CopyMeshToPoints → …
```

### 反模式

| ❌ 错误 | 原因 |
|---------|------|
| `CreateBoxMesh → CopyMeshToPoints`（无 TransformMesh） | 建筑底部 H/2 沉入地下 |
| `TransformMesh(translateY=H)` | 过度偏移，建筑悬浮于地面之上 |
| 在 `CopyMeshToPoints` 之后才加 `TransformMesh` | 全局平移整个实例群，非 per-instance 贴地 |

## SampleMeshSurface：faceGroup + edgeMargin（对齐 Houdini Scatter）

| 参数 | Houdini 对应 | 语义 |
|------|--------------|------|
| `faceGroup` | Scatter SOP **Group** | 只在指定 primitive/face group 上生成点；空 = 全部面 |
| `excludeGroups` | Group 表达式减集（`a - b`） | 从采样池排除 face group |
| `edgeMargin` | Labs **Distance From Border** + Density 硬阈值 | 拒绝距**采样面组边界** < margin 的点 |

### 正确拓扑（lot / 建筑落点）

```text
PolyExtrude(topGroup=extrude_top)
  → [Bevel / Boolean …]          # Boolean 会丢掉用户 face group
  → FaceGroupByNormal(outputGroup=extrude_top, +Y)   # 重建顶面组（必要）
  → SampleMeshSurface(faceGroup=extrude_top, edgeMargin>0)
  → CopyMeshToPoints
```

### 边界语义（重要）

`edgeMargin` 的边界是**采样面组内只出现一次的边**（等价于 Houdini：先 Blast 保留 Group，再对剩余几何算 unshared edge 距离）。  
对封闭挤出体，若对全 mesh 的 unshared edge 测距，顶面与侧面共享边不会被当成边界，margin 无效。

### 反模式

| ❌ 错误 | 原因 |
|---------|------|
| Boolean 后直接 `faceGroup=extrude_top` 且无重建 group | Boolean 通常只写 A/B boolean group，原 `extrude_top` 丢失 → 采样池为空或回退异常 |
| 只设 `edgeMargin`、不设 `faceGroup` | 侧壁/切口面仍被采样，建筑可贴墙或贴路沿 |
| `edgeMargin` 过大（接近 lot 半宽） | 可接受区域过小，点数可能凑不满 `count` |

