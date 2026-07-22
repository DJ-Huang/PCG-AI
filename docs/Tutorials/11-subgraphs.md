# Subgraph / Subnet：把局部图封装为可复用节点

Subgraph（也可理解为 Houdini 风格的 Subnet）让一组 PCG 节点以一个普通节点的形式出现在主图中。它的目标不是引入另一套执行器：保存时仍是一份 `.pcg` 文件，运行时会先展开为现有的有向无环图（DAG），再走原有的校验、拓扑排序、Cook Cache 与执行流程。

它适合封装重复的几何处理链路，例如“输入 Mesh → Transform → 输出 Mesh”。同一份定义可以被主图中的多个 `Subgraph` 实例引用，也可以在定义内部继续使用其他 Subgraph。

## 在 Unity Graph Editor 中使用

1. 在画布中选择两个或更多节点。
2. 右键选择 **Create Subgraph from Selection**。
3. 编辑器会保留选中节点之间的连线；所有跨越选区边界的输入与输出连线会自动转换为 Subgraph 的接口端口。
   - 外部连入选区的每条边生成一个输入端口，命名为 `in_1`、`in_2`……
   - 选区连向外部的每条边生成一个输出端口，命名为 `out_1`、`out_2`……
   - 端口类型继承原连线两端已经确认的 pin type，因此不会把 Mesh、Point、Scalar 等连接混为一谈。
4. 主图中会留下一个 `Subgraph` 节点；双击它进入定义内部查看和编辑细节。
5. 使用窗口顶部面包屑或 **Back to Parent** 回到上一层。该导航栈支持嵌套 Subgraph。

创建后的定义会出现两类接口节点：

- `SubgraphInput`：把主图实例的输入端口传入内部图。
- `SubgraphOutput`：把内部图的结果传回主图实例的输出端口。

## JSON 格式

Subgraph 定义内联保存于根 `subgraphs` 数组，主图节点通过 `data.subgraphId` 引用它。下面是一个最小的 Mesh 透传/变换示例；实际 `pinType` 应与所连节点的端口类型一致。

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "source", "type": "MeshSource", "position": { "x": 0, "y": 0 } },
    {
      "id": "move_instance",
      "type": "Subgraph",
      "position": { "x": 280, "y": 0 },
      "data": { "subgraphId": "move_mesh" }
    },
    { "id": "result", "type": "Output", "position": { "x": 540, "y": 0 } }
  ],
  "edges": [
    { "source": "source", "target": "move_instance", "sourceHandle": "out", "targetHandle": "mesh" },
    { "source": "move_instance", "target": "result", "sourceHandle": "mesh", "targetHandle": "in" }
  ],
  "subgraphs": [
    {
      "id": "move_mesh",
      "name": "Move Mesh",
      "inputs": [{ "id": "mesh", "name": "Mesh", "pinType": "SpatialMesh" }],
      "outputs": [{ "id": "mesh", "name": "Mesh", "pinType": "SpatialMesh" }],
      "nodes": [
        { "id": "input", "type": "SubgraphInput", "data": { "portId": "mesh" } },
        { "id": "transform", "type": "TransformMesh", "data": { "translation": [0, 1, 0] } },
        { "id": "output", "type": "SubgraphOutput", "data": { "portId": "mesh" } }
      ],
      "edges": [
        { "source": "input", "target": "transform", "sourceHandle": "mesh", "targetHandle": "in" },
        { "source": "transform", "target": "output", "sourceHandle": "out", "targetHandle": "mesh" }
      ]
    }
  ]
}
```

接口映射规则很直接：`SubgraphInput` 的**输出** handle 必须等于某个 `inputs[].id`；`SubgraphOutput` 的**输入** handle 必须等于某个 `outputs[].id`。主图中 `Subgraph` 节点的 target/source handle 分别使用对应的输入/输出端口 id。

- Subgraph 可以是内联定义（保存在同一 `.pcg` 的 `subgraphs[]` 中），也可以是联动的外部 `.pcgsubgraph` 资产（graph version `3.0` 的 `SubgraphAsset` 节点）。
- 内联 Subgraph：复制 `.pcg` 即可携带实现。
- 联动 Subgraph Asset：源资产保存并导入后，所有实例跟随更新；进入 Cook / Player 前会在 Unity Host 侧 resolve + flatten 为自包含 v2 JSON。

## 联动 Subgraph Asset（Unity）

1. **Assets → Create → PCG → Subgraph Asset**，或在 Graph Editor 中对选区使用 **Create Subgraph Asset from Selection**。
2. 从 Project Browser 把 `.pcgsubgraph` 拖入任意 `.pcg`（或另一 `.pcgsubgraph`）画布，生成带接口快照的 `SubgraphAsset` 节点。
3. 双击该节点打开源资产；源接口非破坏变更会自动同步，破坏性变更保留 ghost port 并阻止 Cook。
4. Player / 构建只读取 importer 烘焙后的 flat `GraphJson`；原始 StreamingAssets v3 会被拒绝。

## 运行时行为与限制

解析器会递归展开定义，并把内部节点 id 加上实例路径前缀，例如 `move_instance/transform`。随后把主图边界连线重接到这些展开节点，因此下游执行器看到的仍是普通节点和普通边。

- 定义不存在、接口端口不存在，或接口节点与声明端口不匹配时，图校验会失败。
- 递归引用（A 引用 B，B 又直接或间接引用 A）会被拒绝，避免无限展开。
- `SubgraphInput` 与 `SubgraphOutput` 仅能作为定义内部的接口节点；它们不是可在根图单独执行的业务节点。
- 内联 Subgraph 不会生成外部资源文件；联动 `.pcgsubgraph` 则是独立资产，需通过 GUID 引用。

## 验证入口

- C++ 行为测试：[pcg-core/tests/test_subgraph.cpp](../../pcg-core/tests/test_subgraph.cpp)，覆盖输入/输出映射、实际 Mesh 执行和递归引用拒绝。
- 解析与展开：[pcg-core/src/graph_parser.cpp](../../pcg-core/src/graph_parser.cpp)。
- Unity Host resolve/flatten：[Unity/Assets/PcgPlugin/Runtime/PcgExternalSubgraphResolver.cs](../../Unity/Assets/PcgPlugin/Runtime/PcgExternalSubgraphResolver.cs)、[PcgGraphFlattener.cs](../../Unity/Assets/PcgPlugin/Runtime/PcgGraphFlattener.cs)。
- Unity 编辑器打包和导航：[Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphView.cs](../../Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphView.cs)。
- JSON 契约：[schema/graph-schema.json](../../schema/graph-schema.json)、[schema/graph-schema-v3.json](../../schema/graph-schema-v3.json)、[schema/subgraph-schema.json](../../schema/subgraph-schema.json)。

建议的人工验收：先对一段图执行一次并记录输出，再将其中至少两个节点打包成 Subgraph，确认生成的接口端口、双击进入/面包屑返回，以及重新执行后的结果都符合预期。
