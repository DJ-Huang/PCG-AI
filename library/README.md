# PCG Builtin Subgraph Library

面向 PCG editor 搜索面板的内置 subgraph 库（对标 Houdini 内置 subnet，如 cable_generator）。
本目录是唯一真源；`web/pcg-editor/public/library/` 与
`Unity/Assets/PcgPlugin/Editor/BuiltinLibrary/` 都是构建期同步副本，**不要直接编辑副本**。

## 目录约定

```
library/
  library-index.json            ← 生成物，勿手改
  <category>/                   ← 一级目录 = 搜索分类（小写、单数、不再嵌套）
    <name>.pcgsubgraph          ← 资产本体（schema v2.0）
    <name>.libmeta.json         ← 库元数据 sidecar（id/displayName/keywords/...）
    <name>.md                   ← 可选：参数与用法文档
    <name>.png                  ← 可选：缩略图
```

- 条目 id 固定为 `pcg.lib:<目录名>:<文件名>`，构建脚本强制校验。
- 内置库只读；用户个人库放 `~/.pcg/library/`，id 前缀 `user.lib:`（后续接入）。

## 入库门槛

1. 接口完整：声明了 inputs 就必须有 `SubgraphInput` 节点；必须有 `SubgraphOutput`；pinType 不允许留空。
2. 节点/属性/pin 全部以 `schema/node-manifest.json` 为准；脚本会拒绝未知 type 和参数目标。
3. 参数可暴露 `integer`/`number`/`boolean`/`string`/`vector3`；类型必须与目标 manifest 属性一致。
4. 参数 `targetNode.targetProperty` 必须真实存在；默认值与节点内 data 保持一致。
5. 布局：提交前跑
   `python3 Agent/picg-extension/skills/shared/pcg-scripts/layout_pcg.py --in-place <asset>`（top-down，无上坡边）。
6. 多部件装配遵守 `pcg/assembly-bevel`：单件 bevel 完再 merge。
7. `keywords` 至少中英各一；`description` 一句话说清用途。
8. 当前不支持嵌套 subgraph；脚本会报明确错误。

## 修改与再生成

```bash
python3 scripts/build_library_index.py          # 重算 contentHash、生成索引、同步副本
python3 scripts/build_library_index.py --check  # CI 校验（含副本漂移）
```

`contentHash` 与 Unity `PcgSubgraphAssetContentHash.Compute` 字节级对齐（canonical
compact JSON 的 SHA-256）；实例升级检测直接复用该值。

## 编辑器接入

- Web：`NodeSearchPanel` 把 `library-index.json` 合并进搜索；选中后把 definition
  **拷贝**进当前文档 `subgraphs[]`（与库脱钩，`.pcg` 自包含）。
- Unity：`PcgGraphSearchWindow` 追加 `Library/<category>` 分组；选中后创建
  `SubgraphAsset` 引用节点（GUID 稳定，可走既有 Upgrade 流程）。
- cook：两端发往 pcg-core 的文档均为 inline subgraph，server 无需感知库。
