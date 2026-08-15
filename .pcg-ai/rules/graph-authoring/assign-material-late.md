---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/assign-material-late
source_path: PCG AI Rule/Graph Authoring/assign-material-late.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: assigned from in-editor complexity issue (AssignMaterial per micro-part)
verified_date: 2026-08-11
---

# AssignMaterial 只在最后阶段统一进行

当一个子图包含大量“微小部件”（例如 porch baluster / fence spindles / 细分石块 / 小装饰件）时，**不要**在每个小部件的链路上都重复连接 `AssignMaterial`。

## 规则
1. **默认策略：延迟 Assign**
   - 让每个部件只做几何变换/UV生成/合并（`CreateBoxMesh/TransformMesh/CopyMesh/MergeMesh/BevelMesh/...`）。
   - 在该子图“最后一段输出”附近（接近 `SubgraphOutput` 或最终 `MergeMesh`）再做一次/少次数量的 `AssignMaterial`。
2. **避免 N 部件 × N 次 Assign**
   - 若你看到连线呈“每个小部件一套 material/UV/assign 链路”，通常就是冗余。
   - 目标是：同一材质只需要绑定到“代表这类部件的合并结果 mesh 流”上。
3. **需要多材质时：用面/组分配，而不是对每个实例单独 Assign**
   - 当同一个合并网格里存在不同材质区域时，优先使用 `AssignMaterial.group`（或等价的 face-group 机制）做一次分区绑定。
   - 不要把“同材质的多个部件”分别各自 `AssignMaterial`。
4. **UV 生成可以更早，但 Assign 不要太早**
   - `UVTexture` 可以按部件/投影方式生成；
   - 但只要下游最终能接受合并后的统一 material 绑定，就应把 `AssignMaterial` 延后到最后阶段统一连接。

## 原因
- `AssignMaterial` 是高耦合节点：过早绑定会导致每个部件都要走一条 material 连接链，造成 graph 规模膨胀、连线难维护、并增加调参错误概率。
- 将 AssignMaterial 延后通常不会改变最终外观（只要材质绑定发生在最终 mesh 结果形成之后）。

## 例外
- 若不同部件在最终合并前必须保持不同材质、且无法通过面/组分配表达（例如材质必须在几何操作前用于某种生成选择），才允许在中间阶段分别 Assign。

