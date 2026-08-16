# Claim Ledger: Unity PCG Tool with AI-Assisted Node Graph

> 所有 Claim 均基于 Web 搜索结果，附 URL 来源。

## ACH 矩阵

| 证据 \ 假设 | H1 NodeGraphProcessor | H2 FastNoiseLite | H3 ProBuilder+AI | H4 Mixture | H5 LayerProcGen | H6 Unity MCP |
|---|---|---|---|---|---|---|
| NodeGraphProcessor 2.7k stars, MIT, GraphView | C | N/A | N/A | C (依赖) | N/A | N/A |
| FastNoiseLite 3.4k stars, MIT, 16 语言 | N/A | C | N/A | N/A | N/A | N/A |
| FastNoise2 SIMD 性能 10x 于 Lite | N/A | I (更优替代) | N/A | N/A | N/A | N/A |
| Unity-AI-ProBuilder 兼容 2022.3 | N/A | N/A | C | N/A | N/A | C |
| Mixture 基于 NodeGraphProcessor | C (依赖) | N/A | N/A | C | N/A | N/A |
| Mixture 当前版本需 Unity 6000.0 | N/A | N/A | N/A | I (兼容性风险) | N/A | N/A |
| LayerProcGen MIT, C#, Unity | N/A | N/A | N/A | N/A | C | N/A |
| 官方 Unity MCP 2.0.0-pre.1 | N/A | N/A | N/A | N/A | N/A | C (但需 Unity 6?) |
| IvanMurzak/Unity-MCP 社区方案兼容 2022.3 | N/A | N/A | C | N/A | N/A | C |
| Eldemarkki Marching Cubes 已停止维护 | N/A | N/A | N/A | N/A | N/A | N/A |
| Unity Graph Toolkit 仅 Unity 6 | I (不可用) | N/A | N/A | N/A | N/A | N/A |

> C = 支持, I = 矛盾/风险, N/A = 不适用

---

## Claim Ledger

### CL-001: NodeGraphProcessor 基本信息与兼容性
- **Claim**: NodeGraphProcessor 是基于 Unity UIElements 和 GraphView 的节点图编辑框架，MIT 许可，2.7k stars，需要 Unity 2020.2+，当前开发版本为 2020.2.0f1
- **状态**: verified
- **来源**: https://github.com/alelievr/NodeGraphProcessor (GitHub README)
- **ACH**: H1-C

### CL-002: NodeGraphProcessor 功能特性
- **Claim**: 支持 conditional graphs、dependency graphs、processing graphs；提供 minimap、relay nodes、node creation menu、graph parameters、groups、node settings/messages、stacks、relay node packing、node inspector、improved edge connection、multi-window、field drawers、sticky notes、vertical ports、drag-and-drop object creation、renamable nodes
- **状态**: verified
- **来源**: https://github.com/alelievr/NodeGraphProcessor (GitHub README features section)
- **ACH**: H1-C

### CL-003: NodeGraphProcessor API 扩展性
- **Claim**: 提供 documented C# API 创建新节点和图，API 文档在 alelievr.github.io/NodeGraphProcessor，用户手册在 GitHub Wiki
- **状态**: verified
- **来源**: https://github.com/alelievr/NodeGraphProcessor (README 提及 API doc + wiki)
- **ACH**: H1-C

### CL-004: Mixture 是 NodeGraphProcessor 的成功案例
- **Claim**: Mixture 是 alelievr 开发的基于 NodeGraphProcessor 的节点化纹理生成工具，1.3k stars，MIT 许可
- **状态**: verified
- **来源**: https://github.com/alelievr/Mixture (README + NodeGraphProcessor README "Projects made with")
- **ACH**: H1-C, H4-C

### CL-005: Mixture 兼容性风险
- **Claim**: Mixture 当前版本兼容 Unity 6000.0，旧版本可使用 previous releases；需要 Custom Render Texture API（Unity 2020.2+）
- **状态**: verified
- **来源**: https://github.com/alelievr/Mixture (README "The current version is compatible with Unity 6000.0, for older versions you can use previous releases")
- **ACH**: H4-I (Unity 2022 需用旧版 Mixture)

### CL-006: FastNoiseLite 基本信息与功能
- **Claim**: FastNoiseLite 是 MIT 许可的开源噪声库，3.4k stars，支持 16 种语言（含 C#/HLSL/GLSL）；提供 OpenSimplex2、OpenSimplex2S、Perlin、Value、Value Cubic、Cellular(Voronoi) 噪声，Domain Warp，Fractal 选项；2D&3D 采样；支持 float/double
- **状态**: verified
- **来源**: https://github.com/Auburn/FastNoiseLite (GitHub README)
- **ACH**: H2-C

### CL-007: FastNoiseLite 性能数据
- **Claim**: C++ 版本在 Intel 7820X @ 4.9Ghz 上：3D Value 64.13M pts/s, 3D Perlin 47.93M, 3D OpenSimplex 36.83M, 3D Cellular 12.49M
- **状态**: verified
- **来源**: https://github.com/Auburn/FastNoiseLite (Performance Comparisons 表格)
- **ACH**: H2-C

### CL-008: FastNoise2 SIMD 性能优势
- **Claim**: FastNoise2 (AVX2) 性能远超 Lite：3D Value 494.49M vs 64.13M pts/s（约 7.7x），使用 SIMD 指令集（SSE2/SSE4.1/AVX2/AVX512/NEON/WASM SIMD）；采用节点图架构，整个计算融合在 SIMD 管线中；提供可视化 Node Editor 工具
- **状态**: verified
- **来源**: https://github.com/Auburn/FastNoise2 (GitHub README + Wiki)
- **ACH**: H2-I (FastNoise2 更优但需 C++ 绑定)

### CL-009: FastNoise2 节点图架构
- **Claim**: FastNoise2 采用模块化节点图，支持在代码或可视化 Node Editor 中创建噪声树；节点树可序列化为字符串加载；提供 2D 纹理/高度图和 3D 网格预览；支持自定义节点（SIMD-agnostic 接口）
- **状态**: verified
- **来源**: https://github.com/Auburn/FastNoise2 (GitHub README + Wiki)
- **ACH**: H2-I

### CL-010: FastNoiseLite-Unity 包
- **Claim**: shniqq/FastNoiseLite-Unity 是 FastNoiseLite 的 Unity 专用包，可通过 Package Manager 安装
- **状态**: verified
- **来源**: https://github.com/shniqq/FastNoiseLite-Unity (GitHub)
- **ACH**: H2-C

### CL-011: xNode 基本信息与兼容性
- **Claim**: xNode (Siccity) 是 MIT 许可的 Unity 节点编辑器，610 commits，支持 Unity 2018.3+，可通过 Git URL 或 OpenUPM 安装；最小足迹，适合状态机/对话系统/决策器
- **状态**: verified
- **来源**: https://github.com/Siccity/xNode (GitHub README)
- **ACH**: H1 (备选方案)

### CL-012: Hey-xNode (xNode 增强分支)
- **Claim**: JahnStar/Hey-xNode 是 xNode 针对 Unity 6 的增强分支，具有运行时编辑器功能和稳定性改进
- **状态**: verified
- **来源**: https://github.com/JahnStar/Hey-xNode (GitHub)
- **ACH**: H1 (备选, Unity 6 focused)

### CL-013: Unity Graph Toolkit 仅限 Unity 6
- **Claim**: Unity Graph Toolkit (com.unity.graphtoolkit) 是实验性包，基于 UI Toolkit，提供节点图编辑框架；Graph Tools Foundation (com.unity.graphtools.foundation) 是底层框架
- **状态**: verified
- **来源**: https://docs.unity3d.com/Packages/com.unity.graphtoolkit@0.1/manual/introduction.html, https://github.com/needle-mirror/com.unity.graphtools.foundation
- **ACH**: H1-I (不可用于 Unity 2022 LTS)

### CL-014: LayerProcGen 框架特性
- **Claim**: LayerProcGen (runevision) 是 MIT 许可的层级化无限程序生成框架，C#/Unity 兼容；支持无限、确定性、上下文感知的生成；不包含算法本身，是组织生成流程的架构框架
- **状态**: verified
- **来源**: https://github.com/runevision/LayerProcGen (GitHub README)
- **ACH**: H5-C

### CL-015: Eldemarkki Marching Cubes 已停止维护
- **Claim**: Eldemarkki/Marching-Cubes-Terrain 使用 Unity Job System + Burst 实现 Marching Cubes，但项目已停止开发（"This project is not developed anymore"）
- **状态**: verified
- **来源**: https://github.com/Eldemarkki/Marching-Cubes-Terrain (GitHub README)
- **ACH**: H3 (仅参考)

### CL-016: Javier-Garzo Marching Cubes 实现
- **Claim**: Javier-Garzo/Marching-cubes-on-Unity-3D 实现了 Marching Cubes 体素引擎，支持实时地形编辑，使用 Job System + Burst 生成 chunks
- **状态**: verified
- **来源**: https://github.com/Javier-Garzo/Marching-cubes-on-Unity-3D (GitHub)
- **ACH**: H3 (备选参考)

### CL-017: ProBuilder 官方信息
- **Claim**: ProBuilder (com.unity.probuilder) 是 Unity 官方包，MIT 许可，通过 Package Manager 分发；Mesh 数据存储在 ProBuilderMesh 组件中并编译为 UnityEngine.Mesh
- **状态**: verified
- **来源**: https://github.com/Unity-Technologies/com.unity.probuilder (GitHub README)
- **ACH**: H3-C

### CL-018: Unity-AI-ProBuilder MCP 工具列表
- **Claim**: Unity-AI-ProBuilder (IvanMurzak) 暴露 13 个 MCP 工具：create-shape, get-mesh-info, extrude, bevel, delete-faces, set-face-material, flip-normals, set-pivot, merge-objects, subdivide-edges, connect-edges, bridge, create-poly-shape；Apache-2.0 许可；兼容 Unity 2022.3.62f3、2023.2.22f1、6000.3.1f1；47 stars, 94 releases, 最新 2026-06-25
- **状态**: verified
- **来源**: https://github.com/IvanMurzak/Unity-AI-ProBuilder (GitHub README)
- **ACH**: H3-C, H6-C

### CL-019: Unity-AI-ProBuilder 构建于 Unity-MCP 平台
- **Claim**: Unity-AI-ProBuilder 构建于 IvanMurzak/Unity-MCP 平台（社区 Unity MCP 方案，非官方）
- **状态**: verified
- **来源**: https://github.com/IvanMurzak/Unity-AI-ProBuilder (README: "Built on top of the AI Game Developer platform")
- **ACH**: H6-C

### CL-020: 官方 Unity MCP 功能
- **Claim**: 官方 Unity MCP (com.unity.ai.assistant 2.0.0-pre.1) 连接 LLM agents 到 Unity Editor；架构为 AI Client → MCP(stdio) → Relay binary → IPC(named pipe/Unix socket) → Unity Editor(MCP Bridge) → McpToolRegistry；内置工具包括场景管理、资产操作、脚本编辑、控制台访问；支持自定义工具注册（attributes/interfaces/runtime APIs）；动态发现；多客户端支持
- **状态**: verified
- **来源**: https://docs.unity3d.com/Packages/com.unity.ai.assistant@2.0/manual/unity-mcp-overview.html
- **ACH**: H6-C

### CL-021: 官方 Unity MCP 版本兼容性不确定
- **Claim**: 官方 Unity MCP 标注为 2.0.0-pre.1（预览版），文档位于 Unity 6 路径下；未明确标注 Unity 2022 LTS 兼容性
- **状态**: partial
- **来源**: https://docs.unity3d.com/Packages/com.unity.ai.assistant@2.0/manual/unity-mcp-overview.html (URL 路径暗示 Unity 6)
- **ACH**: H6-I (兼容性风险)

### CL-022: ProceduralToolkit 功能
- **Claim**: ProceduralToolkit (Syomus) 是 Unity 程序化生成库，提供建筑生成器等示例；编程工具包，编辑器支持有限；可通过 Git Package Manager 安装
- **状态**: verified
- **来源**: https://github.com/Syomus/ProceduralToolkit (GitHub README)
- **ACH**: H3 (备选)

### CL-023: Sangria 框架状态
- **Claim**: Sangria 是 WIP 的程序化节点图 Mesh 框架，四域数据模型，节点编辑器，操作为静态 C# 方法；尚未正式发布
- **状态**: verified
- **来源**: https://discussions.unity.com/t/wip-sangria-a-procedural-node-graph-mesh-framework-for-unity/1720275 (Unity Discussions)
- **ACH**: H3 (仅参考, WIP)

### CL-024: Mixture 纹理生成特性
- **Claim**: Mixture 使用 GPU 工作流（Custom Render Texture API）实时生成纹理；支持所有渲染管线；可通过 ShaderGraph 自定义节点；提供分形节点、地球高度图、流体模拟、法线混合、场景捕获等功能
- **状态**: verified
- **来源**: https://github.com/alelievr/Mixture (GitHub README)
- **ACH**: H4-C

### CL-025: 多个 Unity MCP 社区实现存在
- **Claim**: 除官方 Unity MCP 外，社区有多个 MCP 实现：IvanMurzak/Unity-MCP、CoderGamester/mcp-unity 等，均允许 AI assistants 与 Unity Editor 交互
- **状态**: verified
- **来源**: https://mcpmarket.com/categories/game-development, https://sourceforge.net/projects/unity-mcp.mirror
- **ACH**: H6-C
