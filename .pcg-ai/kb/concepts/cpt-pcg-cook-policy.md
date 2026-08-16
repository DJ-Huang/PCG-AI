---
id: cpt-pcg-cook-policy
name: "交互式 PCG Graph Cook 策略：事件触发、dirty 传播与预览预算"
category: Concept
tags: [type/concept, area/pcg, area/unity-editor, area/performance]
verified_status: limited
verified_by: "PCG-AI EveryFrame 崩溃/泄漏复现；SideFX Houdini cooking model"
verified_date: "2026-07-18"
source_exec_log:
  - "[[WorkLog/执行记录/PCG Block AI/log.md]]"
  - "[[WorkLog/执行记录/PCG Block AI/log-subgraph-system]]"
source_date: 2026-07-07
common_assumption: "只要把 Cook 放到异步线程，Editor 每帧全图重算就能获得流畅预览。"
---

## 结论

交互式 PCG 工具不应默认在 Editor 每帧无条件重算整张图。更稳健的策略是把四件事分开设计：

1. **触发策略**：Manual、参数/拓扑变化、拖拽结束、Play Mode 连续更新；
2. **失效传播**：只把变更节点及其下游标为 dirty；
3. **缓存键**：节点参数、输入内容、依赖版本与执行上下文共同决定缓存；
4. **预览预算**：低分辨率/低采样/低细分先返回，确认后再做最终质量。

异步只能避免主线程长时间阻塞，不能消除全局扫描、重复校验、缓存 miss、任务排队和几何算法本身的复杂度。

## 项目证据与适用范围

PCG-AI 的 `EveryFrame` 路径曾在输入为空时进入 native `from_json` 崩溃，并伴随反复创建 Mesh 未释放的问题。这证明该实现不适合无条件每帧 cook，但不能外推为“任何 PCG 系统都禁止每帧更新”。能否连续更新取决于节点纯度、资源生命周期、增量能力和预算。

SideFX Houdini 的官方 cooking 模型同样围绕依赖变化、手动更新与节点 cook 状态组织，而不是把所有节点固定为每帧全量执行。这支持事件/依赖驱动的方向，但具体枚举名和 PreviewQuality 档位仍是 PCG-AI 的产品设计。

## 推荐状态机

```text
input/parameter changed
  → validate minimal inputs
  → mark node + downstream dirty
  → cancel/supersede stale request
  → serve valid cached preview if available
  → execute within preview budget
  → atomically publish result
  → dispose superseded native/Unity resources
```

不要只用参数 hash：外部 Mesh、文件、seed、节点实现版本或平台能力变化也可能使旧缓存失效。发布结果时要带 request generation，避免慢任务覆盖较新的编辑结果。

## 验证方法

1. 空输入、断链、删除节点和快速连续改参均不得进入 native 崩溃路径。
2. 记录触发到入队、入队到执行、执行到首个可见结果的分段延迟。
3. 用计数器证明未受影响的上游节点未重复 cook。
4. 连续操作后检查 Mesh/native buffer/Task 数量回到稳定基线。
5. 慢请求结束时不得覆盖后来请求的结果。
6. 预览指定节点或子图失败时，错误必须保留在该目标范围；不得静默改为执行全图，否则会把无关节点错误伪装成预览失败。

## 要点

- 触发、增量失效、缓存和质量预算是四个独立控制面。
- 异步不是减少工作量的替代品。
- `EveryFrame` 只能作为有明确预算与生命周期保障的模式，而非 Editor 默认值。
- 预览目标、执行闭包与失败范围是同一份契约；失败时应 fail-closed，不能以全图 fallback 掩盖目标错误。
- 本文架构方向可复用，具体崩溃栈和枚举属于 PCG-AI 实证，故保持 `limited`。

## 来源

- [SideFX Houdini：Cooking](https://www.sidefx.com/docs/houdini/basics/cooking.html)
- 项目证据：[[WorkLog/执行记录/PCG Block AI/log.md]]（2026-07-07）
- 补充证据：[[WorkLog/执行记录/PCG Block AI/log-subgraph-system]]（2026-07-30）
