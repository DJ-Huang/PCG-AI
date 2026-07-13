# Checklist [S2026071301]

## Pass 1: 设计审查
- [x] 破坏性变更检测：bevel_geometry 返回类型 PcgMeshData→PcgGeometry, from_face_group→from_face_groups, exclude_groups 默认值移除, sweep edge groups 移除
- [x] C API / ABI 兼容性：mesh binary v2 内部格式与公开 size API / header 常量不一致
- [x] 架构合理性：split normals 算法独立于 bevel, transform_geometry 复用 geometry 拓扑
- [x] 重复代码：triangulate_geometry_shared 与 compute_split_normals 共享 fan triangulation 逻辑

## Pass 2: 实现审查
- [x] 命名规范：ShadeMode/NormalComputeOptions/face_origins 语义清晰
- [x] 性能检查：compute_split_normals fallback O(n²), Union-Find 无 union-by-rank
- [x] 二进制安全：v2 read path 有 index bounds check, v1 缺失
- [x] Unity interop：Points 分支未透传 result.Json，Node Info 无法显示 node_stats
- [x] read_u32 bounds check 不考虑 offset (pre-existing)

## Pass 3: 一致性审查
- [x] v1/v2 路径一致性：v2 有 index_count%3 和 index>=vertex_count 检查, v1 无
- [x] JSON key 与 C++ 字段名：fromFaceGroup (JSON) → from_face_groups (C++) 一致
- [x] C++/C# 常量一致性：MeshBinaryV2HeaderSize=20, MeshBinaryFlagHasNormals=0x1 一致
- [x] Editor 状态一致性：Node Info 使用 static LastCookResultJson，存在多组件串数据风险

## Pass 4: 安全验证
- [x] 完整方法体已读取：read_u32, add_triangle, add_quad, emit_geometry, bevel_geometry
- [x] face_origins 与 triangles 同步验证：add_triangle 是唯一 push 点, add_quad/add_polygon 均调用 add_triangle
- [x] compute_split_normals 三角形顺序与 build_group_stats 映射一致性验证
- [x] bevel_geometry VertexPush 路径 + out_geometry fallback 路径验证
- [x] 公开 size API 与实际 writer payload 对照验证：v1 header-only size 无法容纳 v2 normals payload
