# Research Report: Unity PCG Tool with AI-Assisted Node Graph

> 调研日期: 2026-06-27 | 目标: Unity 2022 LTS URP | AI: MCP + 内置 LLM API 双通道
> 
> 更新: 2026-06-27 — 补充节点框架深度对比、LayerProcGen 成熟度评估、Infinigen 对比

---

## 1. 执行摘要

本报告调研了在 Unity 2022 LTS 中构建 AI 辅助 PCG 工具所需的开源库生态。结论是**可以完全基于开源库搭建底层 PCG 框架**，将开发精力集中在 AI 交互层。

### 推荐技术栈一览

| 层级 | 推荐库 | Stars | License | 理由 |
|------|--------|-------|---------|------|
| **节点图框架** | [NodeGraphProcessor](https://github.com/alelievr/NodeGraphProcessor) | 2.7k | MIT | GraphView 基础, 数据处理导向, 有 Mixture 成功案例 |
| **噪声生成** | [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) | 3.4k | MIT | C# 单文件, 多算法, 易集成 |
| **噪声生成(进阶)** | [FastNoise2](https://github.com/Auburn/FastNoise2) | — | MIT | SIMD 10x 性能, 节点图架构, 需 C++ 绑定 |
| **大世界架构** | [LayerProcGen](https://github.com/runevision/LayerProcGen) | — | MIT | 层级化无限生成, 确定性 |
| **Mesh 编辑** | [ProBuilder](https://github.com/Unity-Technologies/com.unity.probuilder) (官方) | — | MIT | Unity 原生, Package Manager |
| **Mesh AI 操控** | [Unity-AI-ProBuilder](https://github.com/IvanMurzak/Unity-AI-ProBuilder) | 47 | Apache-2.0 | 13 个 MCP 工具, 已验证兼容 2022.3 |
| **纹理生成** | [Mixture](https://github.com/alelievr/Mixture) | 1.3k | MIT | GPU 工作流, 基于 NodeGraphProcessor |
| **体素地形(参考)** | [Eldemarkki/Marching-Cubes](https://github.com/Eldemarkki/Marching-Cubes-Terrain) + [Javier-Garzo](https://github.com/Javier-Garzo/Marching-cubes-on-Unity-3D) | — | MIT | Burst+Jobs 实现, 参考代码 |
| **AI ↔ Unity** | [IvanMurzak/Unity-MCP](https://github.com/IvanMurzak/Unity-MCP) (社区) | — | — | 兼容 2022.3, 已有 ProBuilder MCP 工具 |
| **AI ↔ Unity(官方)** | [Unity MCP](https://docs.unity3d.com/Packages/com.unity.ai.assistant@2.0) (2.0.0-pre.1) | — | — | 官方方案, 预览版, 兼容性待验证 |

---

## 2. 详细库分析

### 2.1 节点图编辑器框架（PCG 编辑器底座）

这是整个 PCG 工具的核心骨架。节点图框架决定了用户如何构建、连接、执行 PCG 节点。

#### 🏆 NodeGraphProcessor — 首选推荐

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/alelievr/NodeGraphProcessor |
| **Stars** | 2.7k |
| **License** | MIT |
| **Unity 版本** | 2020.2+（✅ 兼容 Unity 2022 LTS） |
| **技术基础** | Unity UIElements + GraphView + C# 4.7 |
| **安装** | OpenUPM: `openupm add com.alelievr.node-graph-processor` |

**核心能力：**
- **三种图类型**：Conditional graphs（条件图）、Dependency graphs（依赖图）、Processing graphs（处理图）— 完美匹配 PCG 数据流
- **完整编辑器 UX**：Minimap、Relay Nodes（中继节点）、Node Creation Menu、Graph Parameters、Groups、Node Settings/Messages、Stacks、Sticky Notes、Vertical Ports、Drag-and-Drop
- **性能**：基于 GraphView，处理大型图性能良好
- **API 文档**：alelievr.github.io/NodeGraphProcessor + GitHub Wiki
- **扩展性**：简单的 C# API 创建新节点和自定义视图

**为什么选它：**
1. **数据处理导向** — PCG 本质是数据流处理（噪声→高度图→Mesh），NodeGraphProcessor 的 processing graph 模式天然适配
2. **Mixture 成功验证** — 同作者的 Mixture 纹理生成工具完全基于 NodeGraphProcessor 构建，证明了它可用于生产级程序化内容生成
3. **MIT 许可** — 无商业限制
4. **GraphView 原生** — Unity 官方 GraphView 技术栈，长期可维护

**风险：**
- 作者个人项目，维护频率取决于作者（但 497 commits, 2.7k stars 说明社区活跃）
- 非官方包，Unity 版本升级时可能有兼容性风险

#### xNode — 备选方案

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/Siccity/xNode |
| **License** | MIT |
| **Unity 版本** | 2018.3+ |
| **特点** | 极简, 最小足迹, 运行时支持 |

**适用场景**：如果需要更轻量的框架或运行时节点图执行。但数据处理能力不如 NodeGraphProcessor。

#### ❌ Unity Graph Toolkit — 不可用

Unity 官方实验性包（com.unity.graphtoolkit），仅支持 Unity 6+。Unity 2022 LTS 不可用。

#### ❌ Graph Tools Foundation — 不可用

底层框架（com.unity.graphtools.foundation），同样仅限 Unity 6。

---

### 2.2 噪声生成库（地形/纹理基础）

#### 🏆 FastNoiseLite — 首选推荐（C# 原生）

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/Auburn/FastNoiseLite |
| **Stars** | 3.4k |
| **License** | MIT |
| **最新版本** | v1.1.1 (2024-03-05) |
| **Unity 包** | https://github.com/shniqq/FastNoiseLite-Unity |

**噪声算法：**
- OpenSimplex2 / OpenSimplex2S
- Perlin
- Value / Value Cubic
- Cellular (Voronoi)
- Domain Warp（OpenSimplex2-based + Grid Gradient）
- Fractal 选项（FBM, Ridged, PingPong）— 适用于所有噪声类型

**性能（C++, Intel 7820X @ 4.9GHz）：**

| 噪声类型 | 3D (M pts/s) | 2D (M pts/s) |
|----------|-------------|-------------|
| Value | 64.13 | 114.01 |
| Perlin | 47.93 | 92.83 |
| OpenSimplex | 36.83 | 71.30 |
| Cellular | 12.49 | 39.15 |

**为什么选它：**
1. **C# 单文件** — 直接拖入 Unity 项目即可使用，零依赖
2. **HLSL/GLSL 版本** — 可在 Shader 中直接使用噪声，GPU 端生成
3. **16 种语言端口** — 极其便携，算法一致性有保障
4. **Web Preview App** — 在线可视化调试噪声参数

#### 🚀 FastNoise2 — 进阶推荐（C++ SIMD, 高性能场景）

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/Auburn/FastNoise2 |
| **License** | MIT |
| **技术** | C++17, SIMD (SSE2/SSE4.1/AVX2/AVX512/NEON/WASM SIMD) |

**性能对比（3D Value, AVX2）：494.49M pts/s — 比 Lite 快 7.7x**

**关键特性：**
- **节点图架构** — 噪声计算以节点图组织，所有操作（add/multiply/blend）在 SIMD 管线内完成
- **可视化 Node Editor** — 创建噪声树，序列化为字符串加载到代码中
- **2D/3D 预览** — 纹理/高度图和 Mesh 预览，生成无限维度
- **自定义节点** — SIMD-agnostic 接口，写一次代码自动编译所有 SIMD 架构
- **线程安全** — 单个节点树可跨线程并发调用

**使用方式**：需要 C++ 原生插件 + C# P/Invoke 绑定。适合对性能有极高要求的大规模地形生成场景。

**推荐策略**：默认使用 FastNoiseLite（C# 原生），在性能瓶颈时通过 Native Plugin 接入 FastNoise2。

---

### 2.3 大世界生成架构

#### 🏆 LayerProcGen — 推荐

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/runevision/LayerProcGen |
| **License** | MIT |
| **平台** | Unity / 任何 C# 兼容引擎 |

**核心理念：**
- **层级化** — 生成过程组织为数据层（Layer），每层由 Layer + Chunk 类对组成
- **无限** — 伪无限（受 32 位整数范围限制），基于整数世界坐标
- **确定性** — 相同种子 + 坐标 = 相同输出
- **上下文感知** — 支持 chunk 周围环境影响自身生成（解决"chunk 边界"问题）

**关键认知**：LayerProcGen **不包含任何生成算法**，它是组织生成流程的架构框架。你在 Layer 中使用 FastNoiseLite 等库做实际生成。

**为什么需要它：**
- 大世界生成的核心难题不是算法，而是**如何组织层与层之间的依赖关系**
- 地形层 → 生物群系层 → 植被分布层 → 道路网络层 → 建筑放置层
- LayerProcGen 提供了确定性的层间依赖管理

---

### 2.4 Mesh / 几何模型生成

#### ProBuilder（Unity 官方）

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/Unity-Technologies/com.unity.probuilder |
| **License** | MIT |
| **安装** | Unity Package Manager → `com.unity.probuilder` |

**能力**：在 Unity 编辑器内构建、编辑、纹理化自定义几何体。Mesh 数据存储在 `ProBuilderMesh` 组件中，按需编译为 `UnityEngine.Mesh`。

#### 🏆 Unity-AI-ProBuilder — AI Mesh 操控（关键发现）

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/IvanMurzak/Unity-AI-ProBuilder |
| **Stars** | 47 |
| **License** | Apache-2.0 |
| **最新版本** | 1.2.22 (2026-06-25) |
| **兼容 Unity** | ✅ 2022.3.62f3, 2023.2.22f1, 6000.3.1f1 |
| **基础平台** | IvanMurzak/Unity-MCP |

**13 个 MCP 工具：**

| 工具 | 功能 |
|------|------|
| `probuilder-create-shape` | 创建基础体（立方体、球体、圆柱等） |
| `probuilder-get-mesh-info` | 读取网格数据（面、顶点、边） |
| `probuilder-extrude` | 挤出面 |
| `probuilder-bevel` | 倒角 |
| `probuilder-delete-faces` | 按索引或方向删除面 |
| `probuilder-set-face-material` | 为特定面设置材质 |
| `probuilder-flip-normals` | 翻转法线 |
| `probuilder-set-pivot` | 设置网格轴心点 |
| `probuilder-merge-objects` | 合并多个 ProBuilder 网格 |
| `probuilder-subdivide-edges` | 细分边 |
| `probuilder-connect-edges` | 连接边 |
| `probuilder-bridge` | 桥接边选择 |
| `probuilder-create-poly-shape` | 创建自定义多边形网格 |

**为什么重要**：这是现成的 AI → Mesh 操控通道。你可以直接复用或扩展这些 MCP 工具，让 AI 通过自然语言创建和修改几何模型。

#### ProceduralToolkit — 备选

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/Syomus/ProceduralToolkit |
| **License** | MIT |

提供建筑生成器等示例代码，可作为程序化建筑/结构生成的参考。但编辑器支持有限，是纯编程工具包。

---

### 2.5 纹理 / 材质生成

#### 🏆 Mixture — 推荐

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/alelievr/Mixture |
| **Stars** | 1.3k |
| **License** | MIT |
| **基础** | NodeGraphProcessor |
| **技术** | GPU 工作流 (Custom Render Texture API) |
| **渲染管线** | 全部（URP/HDRP/Built-in） |

**核心能力：**
- **GPU 实时生成** — 基于 Custom Render Texture API，所有纹理在 GPU 端生成
- **ShaderGraph 集成** — 通过 ShaderGraph 自定义节点，C# API 扩展
- **节点类型**：噪声节点、分形节点、混合节点、地球高度图、法线混合、流体模拟、场景捕获（HDRP）
- **输出**：Texture2D, RenderTexture, Custom Render Texture

**兼容性注意**：当前版本面向 Unity 6000.0，Unity 2022 LTS 需使用 previous releases（旧版本功能基本完整）。

**与 PCG 的关系**：Mixture 本身就是一个 PCG 纹理工具。你可以：
1. 直接使用 Mixture 作为纹理生成模块
2. 参考 Mixture 的架构，在 NodeGraphProcessor 上自建 PCG 纹理节点
3. 让 AI 通过 MCP 操控 Mixture 的图参数

---

### 2.6 体素地形（Marching Cubes）

没有完美的现成库，但有优秀的参考实现：

| 项目 | 特点 | 状态 |
|------|------|------|
| [Eldemarkki/Marching-Cubes-Terrain](https://github.com/Eldemarkki/Marching-Cubes-Terrain) | Unity Job System + Burst | ⚠️ 已停止维护 |
| [Javier-Garzo/Marching-cubes-on-Unity-3D](https://github.com/Javier-Garzo/Marching-cubes-on-Unity-3D) | 实时编辑, Chunk, Jobs+Burst | 活跃 |

**建议**：以这两个项目为参考，基于 Unity Burst + Job System + NativeArray 自主体素引擎。核心算法（Marching Cubes 查找表）是公开的，实现难度集中在 chunk 管理和 LOD 上。

---

### 2.7 AI 集成方案

#### 方案 A: IvanMurzak/Unity-MCP（社区方案 — 推荐 Unity 2022 LTS）

| 维度 | 详情 |
|------|------|
| **GitHub** | https://github.com/IvanMurzak/Unity-MCP |
| **兼容** | ✅ Unity 2022.3.62f3（已验证） |
| **已有扩展** | Unity-AI-ProBuilder, Unity-AI-Tools-Template |

**优势**：
- 已验证兼容 Unity 2022.3
- 已有 ProBuilder MCP 工具可直接复用
- 有 Tools Template 可快速创建自定义 MCP 工具
- 活跃维护（94 releases）

**工作方式**：AI Client (Cursor/Claude) → MCP (stdio) → Unity Editor → 工具执行

#### 方案 B: 官方 Unity MCP（预览版）

| 维度 | 详情 |
|------|------|
| **包名** | com.unity.ai.assistant 2.0.0-pre.1 |
| **兼容** | ⚠️ 文档位于 Unity 6 路径，2022 LTS 兼容性未验证 |

**优势**：
- Unity 官方维护
- 内置工具（场景管理、资产操作、脚本编辑、控制台）
- 自定义工具注册（attributes/interfaces/runtime APIs）
- 动态发现 + 多客户端支持

**风险**：预览版，可能需要 Unity 6。

#### 方案 C: 内置 LLM API 调用（自建）

在 Unity Editor 工具内直接集成 LLM API（OpenAI/Claude），实现：
- 自然语言 → 节点图操作（创建节点、修改参数、连接节点）
- 节点图状态 → LLM 上下文（让 AI 理解当前图结构）
- 自然语言 → 噪声参数生成（"给我一个有山脉和峡谷的地形"）

**实现路径**：
1. 定义节点图 JSON Schema（描述节点类型、端口、参数）
2. System Prompt 注入可用节点类型和当前图状态
3. LLM 返回结构化 JSON 操作指令
4. Unity 端解析 JSON 执行节点图操作

---

## 3. 推荐架构

```
┌─────────────────────────────────────────────────────────┐
│                    AI 交互层 (你的核心)                    │
│  ┌──────────────────┐    ┌──────────────────────────┐  │
│  │   MCP 通道        │    │   内置 LLM API 通道       │  │
│  │  (外部 AI Agent)   │    │  (工具内自然语言交互)      │  │
│  │  Cursor/Claude    │    │  OpenAI/Claude API       │  │
│  └────────┬─────────┘    └──────────┬───────────────┘  │
│           │                         │                    │
│           ▼                         ▼                    │
│  ┌──────────────────────────────────────────────────┐   │
│  │         PCG MCP 工具注册层 (自建)                  │   │
│  │  - pcg-create-node / pcg-connect-nodes            │   │
│  │  - pcg-set-noise-params / pcg-generate-terrain    │   │
│  │  - pcg-create-mesh / pcg-apply-material           │   │
│  │  - probuilder-* (复用 Unity-AI-ProBuilder)        │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│                  节点图框架层 (开源)                       │
│              NodeGraphProcessor                         │
│         (Processing Graph / Conditional Graph)          │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│                  PCG 算法层 (开源库)                      │
│  ┌─────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │FastNoise│  │LayerProc │  │ProBuilder│  │ Mixture  │ │
│  │  Lite   │  │   Gen    │  │ (Mesh)  │  │ (Texture)│ │
│  └─────────┘  └──────────┘  └──────────┘  └──────────┘ │
│  ┌─────────────────────────────────────────────────────┐│
│  │        体素引擎 (基于 Marching Cubes 参考实现)        ││
│  │        Burst + Job System + NativeArray              ││
│  └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────┘
```

---

## 4. 集成实施路径

### Phase 1: 基础框架搭建（1-2 周）

1. **安装 NodeGraphProcessor** — 通过 OpenUPM
2. **安装 FastNoiseLite** — 拖入 C# 单文件 或通过 FastNoiseLite-Unity 包
3. **创建 PCG 节点基类** — 继承 NodeGraphProcessor 的 Node，定义 PCG 节点接口
4. **实现核心节点**：
   - `NoiseNode`（FastNoiseLite 封装）— 输出噪声场
   - `TerrainHeightmapNode` — 噪声 → 高度图
   - `MeshGeneratorNode` — 高度图 → Mesh
   - `TextureOutputNode` — 高度图 → Texture2D

### Phase 2: AI 集成（2-3 周）

1. **安装 IvanMurzak/Unity-MCP** — 社区 MCP 平台
2. **安装 Unity-AI-ProBuilder** — 复用 Mesh MCP 工具
3. **注册自定义 PCG MCP 工具**：
   - `pcg-create-node` — AI 创建节点
   - `pcg-connect-nodes` — AI 连接节点端口
   - `pcg-set-param` — AI 修改节点参数
   - `pcg-execute-graph` — AI 触发图执行
   - `pcg-get-graph-state` — AI 读取当前图状态
4. **实现内置 LLM API 通道**：
   - 定义节点图 JSON Schema
   - System Prompt 设计
   - 自然语言 → JSON 操作指令解析

### Phase 3: 扩展功能（2-4 周）

1. **接入 LayerProcGen** — 大世界层级化生成
2. **接入 Mixture** — 纹理生成模块
3. **体素引擎** — 基于 Marching Cubes 参考实现
4. **性能优化** — 关键路径 Burst + Job System

### Phase 4: AI 增强体验（持续）

1. **AI 参数推荐** — 根据用户描述自动生成噪声参数
2. **AI 图优化** — 分析图结构，建议合并/简化
3. **AI 模板生成** — "生成一个有河流的峡谷地形" → 完整节点图
4. **FastNoise2 集成**（可选）— 性能瓶颈时通过 Native Plugin 接入

---

## 5. 库对比总表

| 库 | 用途 | Stars | License | Unity 2022 | 维护状态 | 集成难度 |
|----|------|-------|---------|-----------|---------|---------|
| NodeGraphProcessor | 节点图框架 | 2.7k | MIT | ✅ | 活跃 | ⭐ 低 |
| xNode | 节点图框架(轻量) | — | MIT | ✅ | 低活跃 | ⭐ 低 |
| FastNoiseLite | 噪声生成 | 3.4k | MIT | ✅ | 稳定 | ⭐ 极低 |
| FastNoise2 | 噪声生成(SIMD) | — | MIT | ⚠️ 需C++绑定 | 活跃 | ⭐⭐⭐ 高 |
| LayerProcGen | 大世界架构 | — | MIT | ✅ | 稳定 | ⭐⭐ 中 |
| ProBuilder | Mesh编辑 | — | MIT | ✅ | 官方维护 | ⭐ 低 |
| Unity-AI-ProBuilder | AI Mesh MCP | 47 | Apache-2.0 | ✅ | 活跃 | ⭐ 低 |
| Mixture | 纹理生成 | 1.3k | MIT | ⚠️ 需旧版 | 活跃 | ⭐⭐ 中 |
| Eldemarkki MC | 体素地形 | — | MIT | ✅ | ⛔ 停止 | ⭐⭐ 中(参考) |
| Javier-Garzo MC | 体素地形 | — | MIT | ✅ | 活跃 | ⭐⭐ 中(参考) |
| IvanMurzak Unity-MCP | AI↔Unity | — | — | ✅ | 活跃 | ⭐ 低 |
| 官方 Unity MCP | AI↔Unity | — | — | ⚠️ 待验证 | 预览 | ⭐ 低 |

---

## 6. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| NodeGraphProcessor 作者停更 | 高 | MIT 开源, 可 fork; GraphView 技术栈稳定 |
| Mixture 旧版功能不全 | 中 | 核心纹理生成功能在旧版中完整; 可参考自建节点 |
| 官方 Unity MCP 不兼容 2022 | 低 | 使用社区方案 IvanMurzak/Unity-MCP |
| FastNoise2 C++ 绑定复杂 | 中 | 先用 FastNoiseLite (C#), 性能瓶颈再接入 |
| 体素引擎无现成库 | 中 | 有两套参考实现, Marching Cubes 算法公开 |
| LLM 操作节点图的准确性 | 高 | 设计 JSON Schema 约束 + 验证层 + undo/redo |

---

## 7. 搜索追踪表

| 搜索关键词 | 来源类型 | URL |
|-----------|---------|-----|
| Unity PCG procedural content generation open source library | 通用 | [GitHub: ProceduralToolkit](https://github.com/Syomus/ProceduralToolkit), [Unity Discussions](https://discussions.unity.com/t/released-pcg-graph-visual-node-based-procedural-generation/1707098) |
| open source procedural generation framework node-based graph C# Unity | 通用 | [Unity Discussions: Sangria](https://discussions.unity.com/t/wip-sangria-a-procedural-node-graph-mesh-framework-for-unity/1720275), [LayerProcGen](https://github.com/runevision/LayerProcGen) |
| Unity node editor graph framework open source | 通用 | [NodeGraphProcessor](https://github.com/alelievr/NodeGraphProcessor), [xNode](https://github.com/Siccity/xNode) |
| Unity FastNoise noise generation library | 开源 | [FastNoiseLite](https://github.com/Auburn/FastNoiseLite), [FastNoiseLite-Unity](https://github.com/shniqq/FastNoiseLite-Unity) |
| Unity xNode node editor framework | 开源 | [xNode](https://github.com/Siccity/xNode), [Hey-xNode](https://github.com/JahnStar/Hey-xNode) |
| Unity ProBuilder mesh generation | 开源 | [ProBuilder](https://github.com/Unity-Technologies/com.unity.probuilder) |
| Unity marching cubes voxel terrain | 开源 | [Eldemarkki](https://github.com/Eldemarkki/Marching-Cubes-Terrain), [Javier-Garzo](https://github.com/Javier-Garzo/Marching-cubes-on-Unity-3D) |
| Unity Graph Toolkit official | 官方 | [Unity Docs](https://docs.unity3d.com/Packages/com.unity.graphtoolkit@0.1/manual/introduction.html) |
| LayerProcGen Unity | 开源 | [LayerProcGen](https://github.com/runevision/LayerProcGen), [80.lv报道](https://80.lv/articles/an-upcoming-open-source-framework-for-layer-based-infinite-procgen) |
| Unity-AI-ProBuilder AI MCP | 开源 | [Unity-AI-ProBuilder](https://github.com/IvanMurzak/Unity-AI-ProBuilder) |
| FastNoiseLite C# performance | 开源 | [FastNoiseLite README](https://github.com/Auburn/FastNoiseLite) |
| FastNoise2 SIMD node graph | 开源 | [FastNoise2](https://github.com/Auburn/FastNoise2), [Wiki](https://github.com/Auburn/FastNoise2/wiki) |
| Mixture Unity texture generation | 开源 | [Mixture](https://github.com/alelievr/Mixture) |
| Unity MCP server AI integration | 官方+社区 | [Unity MCP Docs](https://docs.unity3d.com/Packages/com.unity.ai.assistant@2.0/manual/unity-mcp-overview.html), [SourceForge](https://sourceforge.net/projects/unity-mcp.mirror) |
| Unity procedural city generation | 通用 | [ProceduralToolkit](https://github.com/Syomus/ProceduralToolkit), [CityScaper](https://discussions.unity.com/t/released-cityscaper-procedural-city-generator/538741) |
| Node Graph Tools 2024 Reddit | 社区 | [Reddit r/Unity3D](https://www.reddit.com/r/Unity3D/comments/1gh1tpk/node_graph_tools_in_2024) |
| Unity AI LLM generate node graph | 学术+社区 | [Unity Discussions](https://discussions.unity.com/t/procedural-content-generation-with-the-new-graph-toolkit/1693642), [PMC Paper](https://pmc.ncbi.nlm.nih.gov/articles/PMC12193870) |

---

## 8. 假设与边界

| 项目 | 状态 | 说明 |
|------|------|------|
| 渲染管线 | ✅ resolved | URP — Mixture 需 Custom Render Texture (2020.2+, URP 支持); ShaderGraph 节点可直接使用 URP 版本 |
| 官方 Unity MCP 2022 兼容性 | deferred | 需实际安装测试; 推荐先用社区方案 |
| FastNoise2 C++ 绑定方案 | deferred | 首选 FastNoiseLite C# 原生; 性能需求驱动时再评估 |
| 目标运行平台 | deferred | 编辑器工具, 不涉及运行时平台; 但如果生成的资产需在移动端使用, 需考虑纹理/Mesh 预算 |

---

## 9. 待补充信息

| 优先级 | 信息 | 说明 |
|--------|------|------|
| 🔴 高 | ~~渲染管线类型~~ | ✅ 已确认: URP |
| 🟡 中 | 是否需要运行时 PCG | 当前假设为编辑器工具; 如需运行时生成, 影响节点图框架选择 |
| 🟡 中 | 目标地形规模 | 决定是否需要 FastNoise2 SIMD 加速 / chunk 策略 |
| 🟢 低 | 团队规模 | 影响 MCP 工具设计复杂度 |

---

## 13. 补充调研：更多节点框架 + Houdini 式工具 + Infinigen 代码质量

### 13.1 完整节点框架对比表（终版）

| 框架 | Stars | License | Unity 2022 | 轻量性 | 定位 | 维护状态 |
|------|-------|---------|-----------|--------|------|---------|
| **xNode** | 3.7k | MIT | ✅ | ⭐⭐⭐⭐⭐ | 通用节点图底座 | 低活跃 |
| **NodeGraphProcessor** | 2.7k | MIT | ✅ | ⭐⭐⭐ | 数据处理节点图 | 活跃 |
| **UnityGeometryGraph** | — | MIT? | ✅ | ⭐⭐⭐ | 几何生成节点图(仿 Blender Geo Nodes) | 活跃 (769 commits) |
| **Forge** | — | — | ✅ | ⭐⭐⭐⭐ | 程序化 Mesh 工具包 | 低活跃 |
| **Sangria** | — | — | ❌ 需 GTK | ⭐⭐ | Burst 原生 mesh 框架 | WIP (个人项目) |
| **CosmoNode** | — | 商业 | ✅ | ⭐⭐ | Houdini 式程序建模(Editor+Runtime) | 已发布 |
| ~~Node_Editor_Framework~~ | — | MIT | ❌ | — | — | ⛔ 已废弃 |

#### UnityGeometryGraph — 仿 Blender Geometry Nodes

- GitHub: https://github.com/TeodorVecerdi/UnityGeometryGraph
- 769 commits，活跃开发
- 受 Blender Geometry Nodes 启发的节点化几何生成工具
- 同一作者还有 CodeGraph（可视化编程工具）
- **不是通用节点编辑器，是专门为几何生成设计的节点图**

#### Forge — Houdini SOP 式数据流工具包

- GitHub: https://github.com/rev087/forge
- 277 commits
- `Geometry` 类在 `Operator` 间传递，`Input()` / `Output()` 模式
- 数据流模型与 Houdini SOP 非常相似
- **可参考其 `Geometry` 类设计作为 MeshData 结构基础**

#### Sangria — 四域数据模型（WIP，仅参考）

- 四域数据模型: Detail / Primitive / Point / Vertex（与 Houdini SOP 体系接近）
- Burst 编译的原生容器
- 节点编辑器基于 Unity Graph Toolkit (GTK) — **⚠️ 仅限 Unity 6**
- 设计理念可参考但不可直接使用

#### CosmoNode — 已发布的 Houdini 式工具（商业，功能参考）

- 受 Blender Geometry Nodes 和 Houdini 启发
- Editor + Runtime 均支持
- ❌ 商业资产，不开源，可作为功能竞品参考

### 13.2 Infinigen 代码质量深度评估

**License**: BSD 3-Clause — 允许商用，需保留版权声明。

**代码架构**:
```
infinigen/
├── core/
│   ├── generator.py          # Generator 基类（极简）
│   ├── surface.py            # 表面操作
│   ├── constraints/          # 约束求解器（布局约束）
│   ├── nodes/
│   │   ├── node_transpiler/  # ⭐ 节点图转译器
│   │   ├── nodegroups/        # 预置节点组
│   │   ├── node_wrangler.py   # 节点图管理器
│   │   └── shader_utils.py    # Shader 工具
│   ├── placement/            # 资产放置算法
│   ├── rendering/            # 渲染配置
│   └── sim/                  # 物理仿真
├── terrain/                  # 地形生成
├── assets/                   # 资产生成器（植物/动物/岩石等）
├── datagen/                  # CV 数据生成
└── tools/                    # 工具脚本
```

**node_transpiler — 核心创新**: Python 代码定义 Blender 节点图 → transpiler.py 反向转译为可序列化节点图数据 → 可在 Blender 中可视化编辑。这是"代码优先 → 节点图"范式，与 AI 生成节点操作指令有思路交集。

**Generator 模式**: 极简基类，`distribution` 采样参数 + `generate()` 子类实现。每类资产（树/岩石/动物）继承实现。

**质量评分**:

| 维度 | 评分 | 说明 |
|------|------|------|
| 结构化 | ⭐⭐⭐⭐ | 模块划分清晰 |
| 文档 | ⭐⭐⭐ | 有 docstring 但不完整 |
| 测试 | ⭐⭐⭐ | 有 tests/ 目录，覆盖率不高 |
| 可移植性 | ⭐ | ⚠️ 极差 — 深度耦合 Blender Python API |
| 算法质量 | ⭐⭐⭐⭐⭐ | CVPR 论文级别 |
| 工程成熟度 | ⭐⭐⭐ | 学术研究代码，非生产级 |

**可复用价值**: 地形/植被/约束布局的**算法思路**可参考；node_transpiler 的"代码 ↔ 节点图"双向转换理念可借鉴；**具体代码无法直接移植**。

### 13.3 Houdini 式工具全景对比

| 工具 | 平台 | 开源 | Houdini 相似度 | Unity 集成 | 状态 |
|------|------|------|---------------|-----------|------|
| Houdini Engine | Unity 插件 | ❌ 商业 | ⭐⭐⭐⭐⭐ | ✅ 原生 | 成熟但昂贵 |
| Blender Geo Nodes | Blender | ✅ GPL | ⭐⭐⭐⭐ | ❌ 需导出 | 成熟 |
| CosmoNode | Unity | ❌ 商业 | ⭐⭐⭐⭐ | ✅ Editor+Runtime | 已发布 |
| BEngine | Unity | ❌ 商业 | ⭐⭐⭐ | ✅ 桥接 Blender | 已发布 |
| UnityGeometryGraph | Unity | ✅ | ⭐⭐⭐ | ✅ | 活跃开发中 |
| Forge | Unity | ✅ | ⭐⭐⭐ | ✅ | 低活跃 |
| Sangria | Unity | ✅ | ⭐⭐⭐⭐ | ❌ 需 Unity 6 | WIP |
| Infinigen | Blender/Python | ✅ BSD | ⭐⭐ | ❌ | 活跃（学术） |

### 13.4 推荐组合方案

**结论：Unity 生态不存在成熟的开源 Houdini 等价物。最佳路径是组合方案。**

推荐：**xNode（节点底座）+ Forge 数据流模式 + Infinigen 算法思路**

| 组件 | 来源 | 作用 |
|------|------|------|
| 节点图 UI + 数据模型 | xNode | 轻量底座, `[Input]/[Output]` 端口 |
| Geometry 数据流 | 参考 Forge 的 `Geometry` 类 | Houdini SOP 式数据在节点间流动 |
| 四域 mesh 模型 | 参考 Sangria 的设计 | Detail/Primitive/Point/Vertex 分层 |
| 程序化生成算法 | 参考 Infinigen 的算法思路 | 地形/植被/建筑生成逻辑 |
| AI 操作层 | MCP + LLM API | 自然语言 → 节点操作 |

推荐节点类型设计（参考 Houdini SOP）：
- **Primitive 节点**：Box, Sphere, Plane, Grid, Terrain
- **Filter 节点**：Subdivide, Bevel, Boolean, Merge, Transform
- **Scatter 节点**：点散布、植被分布
- **Noise 节点**：FastNoiseLite 封装
- **Attribute 节点**：颜色/UV/法线操作
