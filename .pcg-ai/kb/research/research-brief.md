# Research Brief: Unity PCG Tool with AI-Assisted Node Graph

## 问题陈述

在 Unity 2022 LTS 中构建一个 PCG（程序化内容生成）工具，用于创建地形/大世界、几何模型、纹理/材质。工具采用节点图架构，结合 AI 辅助创建和修改节点。目标是复用开源先进库作为底层 PCG 脚本库，将精力集中在 AI 交互层。

## 不可协商约束 (KAC)

| 约束 | 值 | 来源 |
|------|-----|------|
| Unity 版本 | Unity 2022 LTS (2022.3.x) | 用户确认 |
| 生成内容 | 地形/大世界 + 几何模型/Mesh + 纹理/材质 | 用户确认 |
| AI 交互方式 | MCP 协议 + 内置 LLM API 双通道 | 用户确认 |
| 开源优先 | 底层 PCG 库优先使用开源方案 | 用户明确要求 |
| 渲染管线 | 未指定（需确认 URP/HDRP/Built-in） | 待定 |

## 调研范围

### 包含
- Unity 节点图编辑器框架（Node Graph Editor Frameworks）
- 程序化噪声生成库（Noise Generation Libraries）
- 地形/大世界生成框架（Terrain/World Generation）
- 体素/Marching Cubes 实现（Voxel Terrain）
- 程序化 Mesh/几何生成库（Procedural Mesh/Geometry）
- 程序化纹理生成库（Procedural Texture Generation）
- 层级化程序生成架构（Layer-based ProcGen Architecture）
- Unity MCP 生态与 AI 集成方案

### 排除
- 运行时 PCG（聚焦编辑器工具）
- 特定游戏类型的关卡生成（如 Roguelike dungeon generation）
- 商业闭源工具（Houdini Engine、World Creator 等仅作参考）

## Grill 决策记录

### Q1: 生成内容范围
- **推荐**：地形 + 几何 + 纹理三方向并行
- **用户**：确认三项全选
- **状态**：resolved

### Q2: Unity 版本
- **推荐**：Unity 2022 LTS（成熟稳定，GraphView 可用，社区库兼容性好）
- **用户**：确认 Unity 2022 LTS
- **状态**：resolved
- **影响**：Unity 官方 Graph Toolkit（实验包）不可用，需使用社区节点框架

### Q3: AI 交互方式
- **推荐**：MCP + 内置 API 双通道（MCP 适合外部 AI agent 操控编辑器，内置 API 适合工具内嵌的自然语言交互）
- **用户**：确认两者都要
- **状态**：resolved

### Q4: 渲染管线
- **状态**：deferred — 不影响 PCG 节点图框架选型，但影响纹理生成库选择（Mixture 需 Custom Render Texture 支持）
- **补证阶段**：实现阶段确认

### Q5: 开源 vs 自研
- **用户明确**：优先复用开源先进库，精力集中在 AI 交互
- **状态**：resolved

## 假设列表与搜索议程

### H1: NodeGraphProcessor 是最佳节点图底座
- **理由**：2.7k stars, MIT, GraphView 基础, 有 Mixture 成功案例, 数据处理导向适合 PCG
- **搜索问题**：是否支持 Unity 2022 LTS？是否有 runtime 支持？API 扩展性如何？
- **搜索变体**：`NodeGraphProcessor Unity 2022`, `NodeGraphProcessor runtime`, `NodeGraphProcessor API custom node`

### H2: FastNoiseLite 是噪声层最佳选择
- **理由**：3.4k stars, MIT, C# 单文件, 支持 HLSL/GLSL, 多种噪声算法
- **搜索问题**：性能数据？是否支持 Burst？与 FastNoise2 的差距？
- **搜索变体**：`FastNoiseLite C# performance`, `FastNoiseLite Unity Burst`, `FastNoise2 vs FastNoiseLite`

### H3: ProBuilder + Unity-AI-ProBuilder 提供 AI Mesh 生成能力
- **理由**：Unity 官方 ProBuilder + 已有 MCP 工具的 AI 操控
- **搜索问题**：Unity-AI-ProBuilder 的 MCP 工具列表？兼容性？
- **搜索变体**：`Unity-AI-ProBuilder MCP tools`, `ProBuilder procedural mesh`, `Unity-AI-ProBuilder Unity 2022`

### H4: Mixture 提供节点化纹理生成
- **理由**：MIT, 基于 NodeGraphProcessor, GPU 工作流, ShaderGraph 集成
- **搜索问题**：是否兼容 Unity 2022？节点类型有哪些？
- **搜索变体**：`Mixture Unity 2022`, `Mixture node types`, `Mixture texture generation features`

### H5: LayerProcGen 提供大世界生成架构
- **理由**：MIT, 层级化无限生成, 确定性, 上下文感知
- **搜索问题**：是否兼容 Unity 2022？API 设计如何？
- **搜索变体**：`LayerProcGen Unity`, `LayerProcGen API`, `LayerProcGen examples`

### H6: Unity MCP 生态可支撑 AI ↔ PCG 节点图交互
- **理由**：官方 Unity MCP + 社区 Unity-MCP (IvanMurzak) 均有 MCP 工具注册机制
- **搜索问题**：是否支持自定义 MCP 工具注册？是否兼容 Unity 2022？
- **搜索变体**：`Unity MCP custom tool registration`, `Unity MCP Unity 2022`, `IvanMurzak Unity-MCP`
