# Cable Generator

沿一条 spline 路径生成圆截面电缆/线缆网格。

## 输入

| pin | 类型 | 说明 |
|---|---|---|
| `path` | SpatialSpline | 电缆中心线路径 |

## 输出

| pin | 类型 | 说明 |
|---|---|---|
| `out` | SpatialGeometry | 扫掠后的电缆网格（两端加盖、平滑着色） |

## 参数

| 参数 | 类型 | 默认 | 范围 | 说明 |
|---|---|---|---|---|
| Segment Length | number | 0.25 | 0.05 – 2 | 重采样分段长度，越小越平滑 |
| Thickness | number | 0.03 | 0.002 – 0.5 | 电缆半径 |
| Sides | integer | 8 | 3 – 32 | 圆截面边数 |

## 内部结构

```
SubgraphInput(path) → ResampleSpline(spacing) → SweepAlongSpline(circle) → SubgraphOutput(out)
```

## 已知限制

- 不做悬垂（sag/重力形变）：当前节点集没有逐点形变节点，需要垂度时先在外部把路径 spline 调成目标形状再接入。
- 端盖固定开启；需要开口端时展开副本自行修改 `capStart/capEnd`。
