---
id: pit-pcg-assign-material-binding
name: PCG 材质定义不会自动绑定到几何，必须连接 AssignMaterial
severity: high
rootCauseType: 数据流断开
techStack: [PCG-AI, pcg-server, Three.js, PBR]
affectedPlatforms: [PCG-AI Web 编辑器与 pcg-server Cook 输出]
relatedConcepts: []
tags:
  - type/pitfall
  - area/pcg
  - area/material
verified_status: limited
verified_by: wooden-cabin.pcg：Cook 的 PBR 材质槽、纹理路径与 Web Material Preview 三层验证（2026-08-11）
verified_date: 2026-08-11
common_assumption: 创建 Material 节点或在 Cook metadata 中看到材质定义，就会自动作用于同图的 mesh。
---

## 问题

PCG 图中存在有效的 `Material` 定义与纹理资源，但生成的几何在预览中仍使用默认材质，或预览输出没有对应的 PBR material slot。

## 根因

`Material` 仅产生 `Material` 数据；它不是全局材质设置。几何只有通过 `AssignMaterial` 的 `material` 输入显式接收该数据，Cook 才会将材质槽写入输出 mesh。

## 修复方案

按几何职责在对应子图中创建材质，并把 `Material.out` 连接到匹配 `AssignMaterial.material`。例如墙体、屋顶、石基和玻璃分别接到各自输出链路的 AssignMaterial，而不是只在根图放置未连接的 Material 节点。

## 验证方法

1. **复现**：创建或保留未连接的 Material 节点后 Cook；检查输出 mesh 的 material slots / `pbrMaterials`，并切换到材质预览。
2. **改动**：将每个 Material 输出连接到相应 `AssignMaterial.material` 输入后重新 Cook。
3. **通过标准**：Cook 输出含预期材质名和纹理路径；重新加载图后，材质预览能在对应部件显示非默认表面。

## 原始记录

2026-08-11 wooden-cabin.pcg 参考图资产制作会话；待补充独立 WorkLog 路由。
