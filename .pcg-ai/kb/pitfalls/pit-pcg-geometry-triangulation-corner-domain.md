---
id: pit-pcg-geometry-triangulation-corner-domain
name: "PCGG 几何二进制 TRIANGULATION 块索引 corner 域复制顶点，渲染须用 PCGM mesh blob"
severity: high
rootCauseType: 二进制格式域误判
techStack: ["PCG", "Geometry Binary", "pcg-server", "TypeScript", "three.js"]
affectedPlatforms: ["任何绕过 Unity C# 直接消费 pcg-server cook-result 的端（web/three.js、外部工具）"]
relatedConcepts: []
tags:
  - type/pitfall
verified_status: limited
verified_by: "PCG-AI web three.js 预览：TS 解析器 golden fixture 契约（box: 8 points / 24 corners / 36 indices）+ 浏览器渲染截图（2026-08-07）"
common_assumption: "cook-result 的 PCGG 几何块中 TRIANGULATION chunk 的索引直接指向同包 POINTS chunk 的顶点，拿到 points + triangles 就能渲染 mesh。"
---

## 问题

消费 pcg-server `/v1/cook` 返回的 cook-result（PCGR envelope）时，PCGG 几何块（`pcg_geometry_binary.cpp`）里 TRIANGULATION chunk（id=6）的索引**不指向**同包 POINTS chunk 的点。按"points + triangles 直接建 mesh"渲染会索引越界或得到乱面：box 只有 8 个 point，但三角化索引到 23。

## 根因

置信度：高（源码可见）。

写出端 `write_geometry_binary` 调 `triangulate_geometry`（非 `triangulate_geometry_shared`），后者为 flat shading **逐面复制顶点**（corner 域）：每个面角生成一个新顶点，三角索引指向这个复制的 corner 顶点列表。corner 顶点本身不在 PCGG 包里序列化，只有索引；POINTS chunk 仍是共享域的 8 点。Unity C# 侧从不读 TRIANGULATION chunk（跳过），所以该契约长期隐性。

## 修复方案

- 三角形渲染改用同 envelope 的 **PCGM mesh blob**（`pcg_mesh_binary.cpp`）：含复制后的顶点位置 + 索引 + 可选 normals/colors/uvs，与 Unity 预览 Mesh 同源。
- PCGG 块留给 points/edges 显示（POINTS + FACE_OFFSETS/FACE_INDICES 重建多边形轮廓边）。
- 域校验：TRIANGULATION 索引上界 = 边数 ≥3 的面的 corner 总数，不是 point_count。

## 验证方法

1. **复现**：TS 解析 golden box cook-result，按 POINTS 域校验 TRIANGULATION 索引 → "index 8 out of range (8 points)"。
2. **改动**：mesh 改解析 PCGM blob（24 vertices / 36 indices），edges 走 PCGG face loops。
3. **通过标准**：golden fixture 契约测试绿（`scripts/validate-cook-result-parse.py`）；浏览器 three.js 视口 box 渲染与 Unity SceneView 人眼一致。

## 原始记录

- 2026-08-07 web-threejs-preview 执行（web/pcg-editor `src/cookResult.ts`、`PreviewViewport.tsx`；fixtures `cook-result-v1-box.bin`）
