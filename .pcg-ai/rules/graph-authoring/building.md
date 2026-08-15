---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/building
source_path: PCG AI Rule/Graph Authoring/building.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: lot-city-buildings-instanced.pcg facade + footprint spacing fix 2026-07-21; road-facing orientation gap observed 2026-07-21
verified_date: 2026-07-21
---

# 建筑建模策略

策略候选，不替代 Shape Analysis 与当前 manifest。适用于低层/多层盒式建筑、lot-city 原型、带门窗立面的建筑装配。

| 部件 | 首选策略 |
|---|---|
| 主体 | 矩形体用 `CreateBoxMesh`；变截面/收分用 `SweepAlongSpline` |
| 屋顶 | 平顶可用薄 Box；坡顶用截面 + sweep / loft |
| 门 | 独立 Box（或带拱的 sweep），贴在立面中心或明确 bay；真实门宽约 0.8–1.2 m、高约 2.0–2.2 m |
| 窗 | 窗框 + 玻璃分件，再 `CopyMesh` / points 阵列；禁止与门共用同一 bay |
| 装饰 | 转角石、檐口、阳台等按部件 bevel 后 merge（见 `pcg/assembly-bevel`） |

主体与门窗分件完成 material/bevel 后再 `MergeMesh`。落地实例化遵循 `pcg/scatter` 的 prototype 贴地规则。

## 主立面朝向主通道（P0 — lot / street / access）

有门、入口或明确“正面”的建筑实例，**不得**全部以世界坐标 identity 旋转落地。默认应将 **主立面（有门的一面）朝向最近的主通道**（道路中心线、路缘、广场边、人行主路径等）。这是 `pcg/graph-contract`「有向实例朝向主通道」在建筑上的落地细则。

### 原型约定（先写清再旋转）

| 约定 | 要求 |
|---|---|
| 主立面轴 | 在 prototype / Subgraph 内固定一门所在局部轴，并在图注释或 `__nodeTitle` 链上可识别 |
| lot-city 现状 | 门贴在局部 **+Z**（`Door Place.translateZ ≈ +depth/2`）→ 世界朝向应让局部 +Z 指向道路 |
| 贴地 | 仍遵守 `pcg/scatter`：`TransformMesh(translateY=H/2)` 在 copy/spawn **之前** |

### 点属性怎么写（对齐 `CopyMeshToPoints`）

`CopyMeshToPoints` 帧映射：`local.x→binormal`，`local.y→N`，`local.z→tangent`。建筑要直立且门朝路时：

| 推荐写法 | 含义 |
|---|---|
| `rotationY`（度）或 `orient` 四元数 | 最直观：绕世界 Y 把局部 +Z 旋到水平朝向道路 |
| `N=[0,1,0]` + `tx/ty/tz` = 水平朝路单位向量 | 完整 frame：`N`=上，`tangent`=前（门向） |
| ❌ 只写水平 `N` 指向道路 | `N` 映射到局部 **Y**，会把楼“放倒”，不是绕 Y 转向 |

`ExtractCentroid` 目前只写 `P` + `primnum`，**不**带朝向；避让道路之后、spawn 之前必须显式写入朝向属性。

### 朝向目标怎么选

```text
对每个建筑落点 P：
  1. 在水平 XZ 上找最近主通道参考（优先道路中心线 polyline；其次道路 footprint 边界）
  2. facing = normalize( closest_point_on_access(P) - P )，y=0
  3. 若 |facing|≈0（点几乎在道路上）→ 该点应已被道路排除；否则用次近通道或 lot 外向边法线
  4. 转角地块：朝向更宽/更高等级的那条路；两条同级时朝向距质心更近的一段
```

推荐拓扑（概念；节点以当前 manifest 为准）：

```text
… → 避让道路 → ExtractCentroid → [PointRelax]
  → 写入朝向（rotationY / orient / N↑+tangent→路）
  → [可选 AttributeRandomize：仅小幅 rotateY 抖动，不要覆盖主朝向]
  → StaticMeshSpawner / CopyMeshToPoints
```

若尚无“最近点朝向道路”专用节点：用 `AttributeWrangle` 或上游已烘焙的通道方向属性写入；**禁止**用“全部 `rotationY=0`”交差。

### 反模式

| ❌ 错误 | 原因 |
|---------|------|
| lot 质心直接 spawn，无朝向属性 | 所有门朝同一世界轴，背对/侧对道路（lot-city 历史观感 bug） |
| `AttributeRandomize.rotateY` 大范围随机当“朝向” | 随机 ≠ 朝街；只允许在已朝街基础上做小抖动 |
| 把水平朝路向量写入单独的 `N` | 弄错 frame 轴，建筑倾倒或门朝侧向 |
| 只避让道路、不朝向道路 | 足迹正确但立面语义错误 |

### 编图后自检

1. 俯视：每栋有门立面法线与到最近道路段的水平向量夹角应较小（建议 \|Δyaw\| \< 45°，十字路口转角可放宽到朝向主路）。
2. Unity 街景：沿道路行走时门/主立面朝向街道，而不是统一朝世界 +Z/南。
3. 抽查转角地块：朝向约定的主路，而非任意对角线。

## 实例 footprint 不得重叠（P0 — lot / scatter）

多栋建筑用 `ExtractCentroid` → `StaticMeshSpawner` / `CopyMeshToPoints` 放置时，**相邻实例的水平 AABB 不得相交**。

### 尺寸关系

| 量 | 要求 |
|---|---|
| `LotSubdivision.minSize` | ≥ 最大建筑主体边长 + 巷道间隙（建议 ≥ 1.0–2.0 m） |
| 相邻质心距离 | ≥ `(footprint_a + footprint_b) / 2 + gap` |
| `PrimitiveTransform` shrink | 只缩小 lot 面，**不移动**质心；不能单独靠 shrink 拉开建筑 |

当前 lot-city 原型：Tall/Med/Short 主体约 3.4 / 3.6 / 4.2 m → `minSize` 默认应 ≥ **8**（短边至少容纳最大栋 + 间隙）。

### 推荐拓扑

```text
LotSubdivision(minSize ≥ maxFootprint + gap)
  → [shrink pads] → [避让道路] → ExtractCentroid
  → [PointRelax(radius ≈ maxHalfWidth + gap/2)]   # 不规则 lot 的安全网
  → tag / blast by type → StaticMeshSpawner → MergeSpawnPoints
```

`PointRelax.radius`（或 `@pscale`）按**建筑半宽 + 间隙/2** 设，不要用远小于 footprint 的值。

### 反模式

| ❌ 错误 | 原因 |
|---------|------|
| `minSize=4` 却 spawn 边长 3.4–4.2 的固定原型 | 质心间距常 < 两栋半宽之和 → 墙体穿插 |
| 只把 lot shrink 到 0.85，不改 `minSize` | 质心不动，重叠依旧 |
| 三路 spawn 共用过密点云且无 relax | 不同类型仍会 AABB 相交 |

### 编图后自检

1. Cook 后取所有 spawn 点，算最近邻距离，应 ≥ 两栋半宽之和 + gap。
2. Unity 俯视：楼与楼之间可见空隙，无墙穿屋顶。

## 立面开口：门窗不得重叠（P0）

同立面上的门与窗是**互斥占用**的开口，不得靠视觉叠层“糊”在一起。

### 门洞保护区

对每个门，以其 AABB 在立面切向（通常局部 X）与高度向（Y）外扩 **margin ≥ 0.15–0.20 m**，得到保护区。任意窗框/玻璃的 AABB **不得进入**该区。

用窗框（或最大开口件）半宽做间距下限：

```text
min_|center_x| ≥ door.width/2 + margin + opening.width/2
```

其中 `opening.width` 取窗框与玻璃宽度的较大者。两列居中对称时：

```text
CopyMesh(axis=x, count=2).translateX = 2 * min_|center_x|
TransformMesh.translateX = -min_|center_x|
```

并校验外侧不超出墙面半宽减去边距（建议 ≥ 0.05–0.10 m）。

### 一楼 / 有门楼层

| 情况 | 做法 |
|---|---|
| 立面中心有门，两侧仍要窗 | 只放左右 bay；列间距必须满足门洞保护区（上表） |
| 均匀 `CopyMesh` 网格会穿过门洞 | **禁止**；改为：上层整网 + 一楼单独两侧窗，或整网列心移出保护区 |
| 门上有亮窗 | 亮窗是门模块的一部分，不算“窗网格”；仍不得与侧窗 AABB 相交 |

高度向：一楼侧窗可与门同层，但 **X 向必须清出门洞**；不要用“窗压在门上”冒充门联窗。

### 反模式

| ❌ 错误 | 原因 |
|---------|------|
| 2 列窗 `ΔX` 过小，列心 ≈ ±0.7，门宽 ≈ 1.0 | 窗 AABB 切入门洞（lot-city 历史 bug） |
| 全楼层同一 `CopyMesh` 网格穿过中门 | 一楼中间“窗”叠在门上 |
| 只调 Z 深度、不改 X/Y | 深度偏移不能消除立面投影重叠 |
| 门窗共用一个 Merge 后靠材质区分 | 几何仍相交，近景穿帮 |

### 编图后自检

1. 列出每扇门与每扇窗的立面 AABB（含框）。
2. 确认门保护区与窗不相交；外侧不穿出墙体。
3. Unity 正视立面：门两侧可见墙/框间隙，窗不压门扇。
