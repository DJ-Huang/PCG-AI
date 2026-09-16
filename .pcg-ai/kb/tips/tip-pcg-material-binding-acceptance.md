---
id: tip-pcg-material-binding-acceptance
name: PCG 材质绑定采用图、Cook、预览三层验收
categories: [PCG, Material, Validation]
tags:
  - type/quick-tip
  - area/pcg
  - area/material
verified_status: limited
verified_by: wooden-cabin.pcg 的保存图、pcg-server Cook 与 Web Material Preview 复验（2026-08-11）
verified_date: 2026-08-11
common_assumption: 只检查节点连接、Cook metadata 或某一次编辑器预览之一，就足以确认最终材质可用。
---

## 要点

验收 PCG 材质时按以下顺序同时确认：

1. **图层**：每个可见部件的 `Material.out` 已连接到对应 `AssignMaterial.material`。
2. **Cook 层**：重新 Cook 的 mesh 含正确 material slots / `pbrMaterials`，纹理 URI 可解析。
3. **呈现层**：从保存文件重新加载后切换到 Material Preview，确认纹理和表面参数实际显示在目标部件。

任一层失败都不能把材质绑定判为完成：图层能发现漏接，Cook 层能发现序列化或槽位缺失，呈现层能发现路径、加载和渲染映射问题。

## 适用场景

PCG-AI Web 图或其他将图数据 Cook 成 mesh material slots、再由实时预览绑定 PBR 材质的工作流。具体字段名以各运行时的 Cook 格式和材质节点契约为准。

## 验证方法

保存图后执行一次固定 seed Cook，检查输出材质槽与纹理路径；关闭或重新加载预览目标，再以 Material Preview 人眼确认可见部件的最终材质。

## 原始记录

2026-08-11 wooden-cabin.pcg 参考图资产制作会话；待补充独立 WorkLog 路由。
