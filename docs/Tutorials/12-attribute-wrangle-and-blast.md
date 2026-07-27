# Attribute Wrangle 与 Blast

这两个节点提供 Houdini 风格的通用属性表达式与删除能力。目标是用组合表达悬链、断桥等效果，而不是为每种模型增加专用节点。

## Attribute Wrangle

`AttributeWrangle` 当前以 Point 模式遍历 `Point`、`Spline`、`Geometry` 或 legacy `Mesh` 输入，并保持输入数据类型不变。

支持的写入目标：

- `@P.x`、`@P.y`、`@P.z`：修改位置分量。
- `@name`：在 `PcgPointData` 上创建或更新数值属性。Spline/Geometry 没有逐点任意属性通道，因此这些输入只支持位置写入。
- `@curveu`、`@ptnum`、`@numpt`、`@primnum`、`@numprim` 是只读内建变量。

表达式支持 `=`, `+=`, `-=`, `*=`, `/=`, `%=`，算术、比较与逻辑运算，以及以下函数：

```text
abs sin cos tan sinh cosh sqrt exp log floor ceil round
min max pow clamp lerp ch chf chi rand chramp vector
```

`parameters` 是数值 JSON 对象，使用 `chf("name")` / `chi("name")` 读取。`ramps` 是颜色渐变 JSON 对象，使用 `chramp("name", t)` 在 0–1 处采样 RGB；支持 `linear` 与 `constant` 插值。Detail 模式下可用 `v@attr = vector(...)` 写入三元组 detail 属性，并支持 `float`/`int` 局部变量声明与重赋值。

```json
{
  "type": "AttributeWrangle",
  "data": {
    "runOver": "points",
    "expression": "@P.y -= chf(\"sag\") * 4.0 * @curveu * (1.0 - @curveu);",
    "parameters": "{\"sag\":3.0}"
  }
}
```

Spline 的 `@curveu` 按累计弧长归一化，首尾分别为 0 和 1。Point/Geometry 没有显式 `curveu` 时按元素序号归一化；Point 自带数值 `curveu` 属性时优先使用该值。

## Blast

`Blast` 通过 `group`、`expression` 或两者的交集选择元素，再删除选中项。`deleteNonSelected=true` 时反转删除范围。

| 输入 | Entity | Group | Expression |
|------|--------|-------|------------|
| Point | Points | 同名 truthy 点属性 | 点变量与数值属性 |
| Spline | Points | spline 同名属性 | `@curveu`, `@ptnum`, `@P.*` |
| Geometry | Points | point group | 点变量 |
| Geometry | Primitives | face group 表达式 | `@primnum`, `@numprim`, primitive centroid `@P.*` |

删除 Spline 中间的点不会把缺口两端重新连接；节点会把连续保留区间输出为多条 open spline。下面的表达式删除 U=0.4–0.6 的中段：

```json
{
  "type": "Blast",
  "data": {
    "entity": "points",
    "expression": "@curveu >= 0.4 && @curveu <= 0.6",
    "deleteNonSelected": false
  }
}
```

Geometry primitive 删除默认启用 `removeUnusedPoints`，会重建点、面和 point/face/edge group 索引，并保留 Color、UV 与材质通道。

## 吊绳桥拓扑

```text
CreateSpline → ResampleSpline → AttributeWrangle(sag)
                                      ├→ SweepAlongSpline (主索)
                                      └→ Blast(U) → 后续木板/吊索分支
```

节点解析或执行失败会返回包含元素编号和表达式偏移的错误；不会静默忽略未知变量、未知参数、除零或非有限结果。
