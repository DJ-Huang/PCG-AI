# 多材质工作流设计与使用

## 目标

PICG 的多材质系统采用 Houdini SOP 的核心模型：材质是面（primitive/face）属性，Group 只负责选择面，`AssignMaterial` 负责写入材质名，最后一个赋值覆盖同一面上的旧值。Houdini 的 Material SOP 同样支持一组 `Group + Material` 赋值，底层写入 primitive `shop_materialpath`；同一 primitive 被多次赋值时最后一项生效。[SideFX Material SOP](https://www.sidefx.com/docs/houdini/nodes/sop/material.html)

PICG 不在 Graph JSON 中保存 Unity 资产引用。Graph 只保存稳定的材质名，例如 `body_paint`、`glass`、`rubber`；`PcgGraphComponent` 再把这些名字绑定到项目中的 Unity `Material` 资产。

## 节点体系

| 节点 | 职责 | 材质相关用法 |
|---|---|---|
| `FaceGroupByNormal` | 按面法线方向与扩散角创建 face group | 选择顶部、底部、侧向面等材质区域；对应 Houdini Group SOP 的法线选择思路 |
| `GroupCreate` | 创建 edge group | 为 Bevel 等拓扑节点建立边选择集 |
| `GroupCombine` | union/intersect/subtract 组合 group | 将多个 face group 合成材质区域 |
| `AssignMaterial` | 向全部面或选中 face group 写材质名 | `group` 为空表示全部面；填写一个或多个 face group 时只覆盖这些面 |
| `Output` | Sink 三角化与宿主输出 | 将面材质展开到三角形，生成材质槽表和 Unity SubMesh |

`AssignMaterial` 参数：

| 参数 | 类型 | 含义 |
|---|---|---|
| `group` | face `groupMultiSelect` | 逗号分隔的 face group；空值表示整份 geometry |
| `materialName` | string | 与 Unity Material Bindings 中 `materialName` 对应的稳定名称 |

## 推荐连线

```text
最终拓扑
  ↓
AssignMaterial(group="", materialName="body_paint")
  ↓
AssignMaterial(group="windows", materialName="glass")
  ↓
AssignMaterial(group="tires", materialName="rubber")
  ↓
Output
```

例如给盒子的顶面单独赋材质：先用 `FaceGroupByNormal(outputGroup="top", direction=(0,1,0), spreadAngle=5)` 建立 `top` 面组，再用一个全局 `AssignMaterial` 写底材，最后用 `AssignMaterial(group="top")` 覆盖顶面。

这对应 Houdini Material SOP 的多条 assignment。PICG 用可链式节点表达列表，优点是每次覆盖都可单独预览、参数化、禁用或插入到分支中。规则如下：

- 空 `group` 是基础/兜底材质，通常放在第一层。
- 后续节点只覆盖命中的面；未命中的面保留旧材质。
- 同一面属于多个所选 group 时，下游最后一个 `AssignMaterial` 生效。
- 材质赋值应放在 Boolean、Subdivide、Bevel 等最终拓扑修改之后，避免新生成的面缺少明确材质归属。
- `MergeMesh` 会拼接两侧的面材质数组，不再使用“第一个材质获胜”。
- Group 表达式沿用当前子集：单组、`a - b`、`a & b`；多选字段用逗号组合多个表达式。

## Unity 绑定

在场景对象的 `PcgGraphComponent` Inspector 中展开 **Material Bindings**：

1. 新增一项，`materialName = body_paint`，指定车漆 Material。
2. 新增一项，`materialName = glass`，指定玻璃 Material。
3. 新增一项，`materialName = rubber`，指定橡胶 Material。
4. Run Graph。生成 Mesh 的 SubMesh 顺序由首次遇到的材质名稳定决定，`MeshRenderer.sharedMaterials` 按同一顺序设置。

未绑定的名字和空材质名使用 `Mesh Material` 作为 fallback，不会让对应 SubMesh 消失。

## 数据流与兼容性

```text
PcgGeometry.face_materials[face]
  → Sink stable fan triangulation
PcgMeshData.material_slots + triangle_materials[triangle]
  → Mesh Binary v3 material section
Unity Mesh.subMeshCount + SetTriangles
  → PcgGraphComponent Material Bindings
MeshRenderer.sharedMaterials
```

- 无材质表的 Mesh 仍写 Mesh Binary v2，旧消费者和既有 sizing API 不变。
- 有材质表时写 v3：24-byte header 后附可变长 UTF-8 slot names 与逐三角形 slot index。
- Geometry Binary 使用新增 face-material chunk；旧 reader 会跳过未知 chunk。
- 旧的单材质 `mesh_metadata.material` 继续保留；单槽图仍可被旧的 metadata 消费者识别。

## 当前边界

- 材质参数 override、材质混合/分层、每实例材质不在本期范围。
- Mesh-only 上游没有 polygon face group；此时 `AssignMaterial.group` 必须为空。需要 Group 驱动时，应保持 `PcgGeometry` 管线到最终 Sink。
- 拓扑节点新建面的材质继承策略尚未统一，因此推荐在最终拓扑之后赋材质。
