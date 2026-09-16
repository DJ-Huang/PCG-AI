---
id: "pit-react-flow-nodetypes-default-mapping"
name: "React Flow nodeTypes 必须显式映射每个类型字符串，不能用 { default: Component }"
severity: "medium"
rootCauseType: "逻辑误判"
techStack: ["React Flow", "TypeScript"]
tags: ["type/pitfall", "area/web-editor", "area/pcg"]
source_exec_log: "[[WorkLog/执行记录/PCG Block AI/log.md]]"
source_date: 2026-07-05
verified_status: "verified_true"
verified_by: "tsc + vite build 通过，用户确认节点显示正确"
verified_date: "2026-07-05"
---

## 常见假设

React Flow 的 `nodeTypes` 和普通 React 组件映射一样，可以用 `{ default: Component }` 作为 fallback，所有未匹配的节点类型都会用 default 组件渲染，组件内部仍然能通过 `props.type` 拿到原始节点类型。

## 根因

React Flow 在内部处理 `nodeTypes` 时，如果节点 `type` 在 `nodeTypes` 映射中找不到精确匹配，会将其 `type` 属性**替换为 `'default'`** 再传给组件。组件收到的 `props.type` 是 `'default'` 而非原始类型字符串（如 `'SpawnPoints'`、`'CreatePointGrid'`）。这导致组件内部根据 `type` 查找 manifest 定义时查不到，显示 "Unknown"。

## 正确做法

将每个节点类型字符串显式映射到组件：

```typescript
// ❌ 错误：所有节点 type 会被改为 'default'
const nodeTypes = { default: ManifestNode };

// ✅ 正确：每个类型显式映射
const nodeTypes = Object.fromEntries(
  getAllNodeTypes().map((def) => [def.type, ManifestNode])
);
```

如果节点类型来自外部 manifest（如 JSON），需要在运行时动态构建映射对象，而非依赖 `default` fallback。

## 来源

- 原始日志：`[[WorkLog/执行记录/PCG Block AI/log.md]]`（2026-07-05）
- 关键文件：`web/pcg-editor/src/App.tsx` — `nodeTypes` 定义
