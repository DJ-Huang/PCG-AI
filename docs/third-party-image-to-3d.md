# 第三方图生 3D API（Meshy / Tripo / …）

PCG-AI 把云端「图生 3D」厂商接入成 **Graph 节点**：Editor 侧负责鉴权、HTTP、轮询与本地缓存。普通预览仍由 `ImportMesh` 路径读取下载网格；高保真程序化交付由 `pcg-server` 烘焙无拓扑定向采样，交给 `pcg-core` 的 `OrientedSdfSurface` 在 Cook 时重建新网格，再可接 `ActionRig` 建立语义组件、蒙皮和动画。来源若本来就是已绑定的自包含 GLB，则改走 `PreserveGltfRig`，保留原 skin/animation，不重新猜权重。

本页是用户说明 + 后续厂商扩展清单。节点属性以 `schema/node-manifest.json` 为准。

---

## 设计原则

| 原则 | 做法 |
|------|------|
| 密钥不进工程 | Unity Key 只存 `EditorPrefs`；Web Key 只存 pcg-server 受保护凭据库。两者都不写 `.pcg` / git |
| 云端调用与 Cook 解耦 | Unity 由 Editor、Web 由 pcg-server 显式执行 Generate；`pcg-core` / Cook 只读本地文件 |
| 缓存可复用 | 成功结果落在 `Library/PCG/<Vendor>Cache/`；同参数再 cook / preview 只读缓存、不扣费 |
| 禁止自动生成 | Preview / OnParameterChange / Run **从不**调云端；只有 Inspector **Generate** 才请求 API |
| 保存进工程 | Generate 成功后弹窗保存到 `Assets/`（勾选的 GLB/FBX 等同名写入）；节点 `path` 记主文件（优先 GLB）；下次打开 / cook 默认加载 |
| 节点形态统一 | 无输入 pin，输出 `SpatialMesh`；cook 前注入已解析的绝对 `path`，执行侧与 `ImportMesh` 同路径 |

```text
Source Image / URL
        │
        ▼
 Unity Resolver (Editor)
   · 读 API Key (EditorPrefs)
   · data URI / URL → 厂商 API
   · 轮询至 SUCCEEDED
   · 下载 GLB → Library/PCG/...Cache/
   · 把绝对 path 写入执行用 JSON
        │
        ▼
 pcg-server / PcgCore
   · Meshy3DGenerator (或未来 Tripo…)
   · Assimp 读 GLB → PcgGeometry
        │
        ▼
 下游 Mesh / Output
```

---

## 配置 API Key

### Web 编辑器

**Settings → 3D Generation → Tripo (Image to 3D)**，粘贴 Key 后 **Save key**。

- Key 由 pcg-server 存入本机受保护凭据库（`~/Library/Application Support/PCG-AI/credentials.json`，chmod 600；`PCG_AGENT_CREDENTIAL_STORE=keychain` 时走 macOS Keychain），**不进浏览器 localStorage、不进 `.pcg`、不进 git**
- 环境变量 `PCG_TRIPO_API_KEY`（可选 `PCG_TRIPO_BASE_URL`）优先于已存 Key；env 生效时 UI 锁定编辑
- 服务端端点：`GET /v1/third-party/tripo/status`、`PUT|DELETE /v1/third-party/tripo/config`；status 只回 `configured/source/baseUrl/keyHint`（末 4 位），永不回传完整 Key
- Generate：`POST /v1/third-party/tripo/generate`（同步 upload→create→poll→download，GLB 落 `library/web-cache/TripoCache/<sha256>.glb`，已 gitignore）；`GET /v1/third-party/cache/<file>` 供编辑器拉取缓存 GLB
- 节点 Inspector 底部 **Cloud Generation → Generate**：按 Source Image / Image URL + 模型参数生成，成功自动把缓存路径写回节点 `path`；再次同参请求命中缓存不扣费。`PCG_TRIPO_STUB_GLB=<glb>` 可置 stub 模式（不触网，CI/无 Key 调试用）

### 从 Tripo 参考到可编辑 PCG（Web）

`Tripo3DGenerator` 的 GLB 是视觉基准，不是最终程序化资产。生成成功后，Inspector 会出现 **High-Fidelity Proceduralize**：

1. 先用一张干净、无遮挡的单主体图片生成 Tripo GLB。不要直接提交角色板、九宫格或多视图拼图；Tripo 可能把每个视图都重建成一个重复主体。
2. 点击 **High-Fidelity Proceduralize**。本地服务把 GLB 测量成 `OPC1 v2`：每个采样只保留量化位置、定向法线、可选颜色与 UV，明确不保存 source faces / indices。它同时提取 GLB 内嵌 base-color、normal、ORM 图片；密集二进制都由服务直接写入节点，Agent 上下文只看到摘要。
3. Web 编辑器把 base-color 以 `canvasY = 1 - sourceV`、双线性过滤、sRGB→linear 的方式烘进稠密采样 RGBA。这样新拓扑不再跨原 UV atlas 岛插值；来源法线也按最近且朝向一致的测量转移。随后编辑器预置 `OrientedSdfSurface` 与无贴图 vertex-color 材质节点。前者在每次 Cook 时执行稀疏 oriented MLS-SDF splat，再以 Surface Nets 生成全新 quad 拓扑；`cellSize`、`supportRadiusCells`、`isoOffset`、UV 翻转和最大 active cells 都可编辑。Web 交互预览另有默认 ×2 cell scale 与 50 万三角形预算，超预算自动重试；最终 GLB 会重新按原始 `cellSize` 全质量 Cook。来源路径仅作 provenance，Cook 不重新打开 GLB。需要动作时在材质之后接 `ActionRig`，以 schema-v2 `componentTree` 明确区域、父子、pivot、刚性/平滑 skin 和 clips；不要把单一融合 shell 的自动分区当成真实来源零件。
4. 编辑器随后 Cook 参考节点，并以同一物理相机采集 `0° / +35° / -35° / 90° / 145° / 180°` 六个固定方位；每个方位都输出 `beauty`、`alpha-silhouette`、`semantic-id`、`depth`、`normal`、`roughness-material-id` 六个诊断通道，共 36 张 `1024×1024` PNG。
5. 证据和 `tripo-reference-manifest.json` 自动交给内置 Agent。每轮只通过 PCG MCP 改图并执行 top-down 布局、`validate → cook → capture`。最终像素门禁逐视图要求 silhouette IoU ≥ 0.98、双向轮廓距离 P95 ≤ 2 px；同时单独报告交集 RGB 误差与 Laplacian 表面噪声。任一视图失败都保持 `REWORK`，不得声称“像素级”或 `FINAL_ACCEPTED`。

缓存文件名中的 64 位十六进制值是“源图身份 + 生成参数”的请求键，不是 GLB 内容哈希。需要内容寻址时必须另行探测文件 SHA-256。

完整样例：

- [`examples/tripo-yoyo-puppy-reference.pcg`](../examples/tripo-yoyo-puppy-reference.pcg)：Tripo 参考图
- [`examples/tripo-yoyo-puppy-procedural.pcg`](../examples/tripo-yoyo-puppy-procedural.pcg)：不依赖 Tripo 缓存即可 Cook 的程序化角色
- [`examples/tripo-yoyo-puppy-animated.glb`](../examples/tripo-yoyo-puppy-animated.glb)：带 10 个骨骼/语义 visual components 与 `idle`、`walk` clips 的 Web glTF 导出
- [`examples/tripo-yoyo-puppy-asset-spec.md`](../examples/tripo-yoyo-puppy-asset-spec.md)：证据边界、语义部件、材质、参数和验收合同
- [`examples/tripo-yoyo-puppy-reference.png`](../examples/tripo-yoyo-puppy-reference.png)：从原角色板准入后的单主体参考
- [`examples/tripo-yoyo-puppy-evidence/README.md`](../examples/tripo-yoyo-puppy-evidence/README.md)：六视角对照、六种诊断通道、网格度量与最终验收

### 对齐 img2threejs 的方法与能力边界

[`img2threejs`](https://github.com/img2threejs/img2threejs) 并不是把任意单网格 GLB 自动识别成“绝对正确”的零件后再自动绑骨。它公开的 GLB character pipeline 把 GLB 当测量基准，先重建表面，再从模型实测比例建立 skeleton/weights；文档还明确说明来源通常没有 rig。其通用建模工作流是先写语义规格，再用可参数化几何或表面场重建，最后通过固定相机和确定性脚本逐轮验收。[官方 showcase](https://github.com/img2threejs/img2threejs-showcase) 的程序化案例会组合 primitive、extrude、tube、程序化材质和显式部件层级；例如 [War-Hauler 源码](https://github.com/img2threejs/img2threejs-showcase/blob/main/src/demos/warhauler/createWarHaulerModel.ts) 同时使用了 `ExtrudeGeometry`、`TubeGeometry`、重复零件和显式层级。

PCG-AI 在吸收这条重建/绑定路线之外，又为“来源本来就有正确 rig”的生产输入补了一条独立的无损保留路线，避免把原 skin 和 animation 丢掉后再猜一次：

```text
图片 / 未绑定 GLB
  → 高保真连续表面重建
  → componentTree + semantic regions（显式假设）
  → Bone/Skeleton + geodesic/rigid weights
  → SkinnedMesh + clips + 可选互斥拆件

已有 rig 的自包含 GLB
  → PreserveGltfRig
  → 原 skin.joints / inverse bind / JOINTS_0 / WEIGHTS_0
  → 原 node-index animation channels + 连续蒙皮外壳
```

第一条路线允许动画和拆件，但必须承认单一融合 shell 没有可证明的语义边界；第二条路线忠实保留原绑定，不声称能够从连续外壳恢复来源未提供的可拆组件。

本工程采用相同原则，但把 Three.js factory 换成可编辑 `.pcg` 图：

| img2threejs 方法 | PCG-AI 对应能力 | 状态 |
| --- | --- | --- |
| 先列 `detailInventory` / 语义部件，再分阶段构建 | AssetSpec、语义 Subgraph、图参数、Agent 的 camera → silhouette → form → accessory → material → lighting 修正顺序 | 已接入 |
| primitive、lathe、extrude、loft、curve sweep、CSG | `Box/Sphere/Capsule/Torus`、`RevolveMesh`、`ExtrudeMesh`、`LoftMesh`、`SweepAlongSpline`、`BooleanMesh` | 已有 |
| 有机曲线的变截面与真实尖端 | 新增 `TaperedSweep`：弧长采样、rotation-minimizing parallel-transport frame、逐站点椭圆半径/扭转、零半径端点收敛 | 本次接入 |
| GLB oriented point splat + Surface Nets | `OrientedSdfSurface`：OPC1 v2 拷贝量化测量值但不拷贝 faces/indices；稀疏 MLS-SDF + Surface Nets 在 Cook 时重建新拓扑；最终小狗图以 112,025 个测量样本生成 891,047 顶点 / 1,779,556 三角形 | 本次接入 |
| 多级 surface / 逐部件 cell size | Web 预览提供 Adaptive、Full、Medium ×2、Low ×3；节点可独立设置 `previewCellSizeScale`，并由 `previewTriangleBudget` 阻止超大 PCGR 进入 Three.js。全质量 Cook/导出继续使用 authored `cellSize` | 本次接入 |
| GLB 内嵌外观迁移 | base-color 烘入稠密线性 RGBA 场，最近朝向一致测量转移颜色/法线；避免新 Surface Nets 三角形跨 UV 岛。Web GLB exporter 写出 `COLOR_0`，无需外部图片 | 本次接入 |
| 语义部件树与保守拆件 | `ActionRig` schema v2 `componentTree`：父子、pivot/tip、region、priority、role、joint、detachable 统一定义；triangle ownership 互斥覆盖，不复制或丢失三角形 | 本次接入 |
| 实体测地线蒙皮 | 体素化模型内部的 geodesic distance 生成最多 4 个权重，避免距离很近但不连通的四肢串权；hair/detail/decal/panel 可强制 rigid skin | 本次接入 |
| Three.js 动画绑定与运行时控制 | 真正建立 `Bone` / `Skeleton` / `SkinnedMesh`；Preview/Review 自动显示 Animate 入口与 clip、播放/暂停、stop-to-bind-pose、时间轴、正反向倍速、loop 控件；运行时支持 quaternion/position/scale tracks，visual components 采用 root-direct identity bind，避免 component hierarchy 与 skin 的 double-transform | 本次接入 |
| 已有 rig 的 GLB 原样保留 | `PreserveGltfRig` 直接加载自包含 GLB，保留 `skin.joints`、inverse bind matrices、`JOINTS_0`、`WEIGHTS_0`、node-index animation channels 与连续 shell；导出返回完全相同的源字节 | 本次接入 |
| GLB 分层比例比较 | `metadata.meshBandProfile`：脚底对齐、身高归一化、每高度带 5–95 分位宽/深、完整宽度和横向/纵深中位中心；算法语义对齐 [mesh_reference_compare.py](https://github.com/img2threejs/img2threejs/blob/main/forge/stage4_review/mesh_reference_compare.py) | 本次接入 |
| 固定浏览器相机的多通道诊断 | `pcg_capture_preview(renderPass=...)` 支持 beauty、透明轮廓、语义 ID、线性深度、视空间法线、粗糙度/材质 ID；`Proceduralize with Agent` 自动采 6 × 6 | 本次接入 |
| 六视图像素门禁 | `compare_procedural_reference.py`：silhouette IoU、双向轮廓 mean/P95、交集 RGB、两侧 Laplacian noise，任一视图失败则非 FINAL_ACCEPTED | 本次接入 |
| Agent 大载荷隔离 | `pcg_bake_oriented_sdf` 在服务端直接把载荷加入 live graph；MCP 图读取自动隐藏 OPC/data URI 大字段，普通 patch 不会覆盖它们 | 本次接入 |
| 局部确定性函数修形 | `AttributeWrangle` 的受限 DSL；可改点/属性但不执行任意 JavaScript，图仍可复现和跨运行时验证 | 已有，纳入流程 |
| 投影纹理 | `ProjectTexture` 可做显式相机投影；高保真 GLB 路径默认把 base-color 采样为程序化颜色场 | 已有基础能力 |

以下是 img2threejs 的可选研究路线，本次没有伪装成已经完成：

- **多视图 visual hull**：把二值轮廓反投影成视锥并求交，再对体素边界焊接成网格。它适合可靠的正交多视图，但看不到的凹陷仍不可恢复；当前 PCG 的 `VDB/IsoOffset` 兼容节点只保留部分语义，并不是可验收的 visual-hull 实现。
- **投影优先的人像拟合**：2D landmarks → 参数化模板 → 相机求解 → 去光照 → 纹理投影。PCG 已有投影节点，但还没有通用 landmarks、de-light 和置信度链路。
- **外部学习型证据**：SAM2、Depth Anything、MediaPipe 等只作为可选先验；没有模型来源、版本和置信度时，不应让它们自动通过 PCG 验收。

`OrientedSdfSurface` 的算法路线参考 img2threejs 官方 [GLB character pipeline](https://github.com/img2threejs/img2threejs/blob/main/integrations/glb_character_pipeline/PIPELINE.md)、[SDF surface exporter](https://github.com/img2threejs/img2threejs/blob/main/integrations/glb_character_pipeline/python/export_sdf_surfaces.py) 和 [Surface codec](https://github.com/img2threejs/img2threejs-showcase/blob/main/src/demos/girl-character/surfaceCodec.ts)。动画侧则严格区分“重新绑定”与“原 rig 保留”：前者使用可编辑 component tree 和 geodesic/rigid skin，后者以 glTF skin/animation 索引为真源。PCG-AI 的表面重建实现为独立 C++ 稀疏场/OPC codec，并保留其 [Apache-2.0](https://github.com/img2threejs/img2threejs/blob/main/LICENSE) 来源说明。visual hull、landmark fitting 和 learned depth 仍保留为后续独立节点/服务，不与基础 Cook 隐式耦合。

### Unity 编辑器

1. **PCG → Settings**，或 **Edit → Project Settings → PCG AI**
2. 找到 **Meshy (Image to 3D)** 区块，粘贴 Key（格式通常为 `msy_…`）
3. Key 缓存在本机：`EditorPrefs` 键 `PCG.Meshy.ApiKey`
4. **Clear API Key** 可清除

后续 Tripo 等厂商会增加同级区块与独立 Pref 键（见下方扩展清单），互不覆盖。

> 密钥只在创建时完整显示一次；勿提交到 git、飞书公开文档或示例图。

---

## Meshy 3D Generator

| 项 | 值 |
|----|-----|
| 节点类型 | `Meshy3DGenerator` |
| 显示名 | Meshy 3D Generator |
| 分类 | Mesh |
| 官方文档 | [docs.meshy.ai — Image to 3D](https://docs.meshy.ai/en/api/image-to-3d) |
| Base URL | `https://api.meshy.ai` |
| 创建任务 | `POST /openapi/v1/image-to-3d` |
| 查询任务 | `GET /openapi/v1/image-to-3d/:id` |
| 测试场景 | `Assets/PICGGenerator/Scenes/PCGReview_ThirdPartyAPI.scene`（`PCG_Meshy_Test` + `meshy-image-to-3d.pcg`） |

### 用法

1. 启动 `pcg-server`（与普通 cook 相同）。
2. 在 Settings 填好 Meshy API Key。
3. 图中添加 **Meshy 3D Generator**。
4. 指定 **Source Image**（项目内 `Texture2D`），或填 **Image URL**（公网 URL / `data:` URI；URL 优先）。
5. 按需调整模型 / PBR / Remesh / Polycount / Scale / Axis。
6. 在节点 Inspector 底部勾选 **Save formats**（GLB / FBX，可多选），再点 **Generate**。Meshy 按勾选格式生成；下载后弹窗保存到 `Assets/`（同名多扩展名）。`path` 优先记 GLB，否则记 FBX。
7. 之后 Preview / Cook / 重新打开图，默认加载已保存的 `path`。取消保存时：若已有旧路径则保留；否则临时用 MeshyCache。

### 节点属性

| 属性 | 默认 | 说明 |
|------|------|------|
| `texture` | `""` | Source Image（`texture2d`） |
| `imageUrl` | `""` | 可选；`http(s)://` 或 `data:`，有值时覆盖 texture |
| `aiModel` | `latest` | `latest` / `meshy-6` / `meshy-5` |
| `shouldTexture` | `true` | 是否生成贴图 |
| `enablePbr` | `false` | PBR（metallic/roughness/normal 等） |
| `shouldRemesh` | `true` | Remesh |
| `targetPolycount` | `30000` | Remesh 目标面数（100–300000） |
| `scale` | `1.0` | 导入缩放 |
| `axisConversion` | `none` | `none` / `zUpToYUp` / `yUpToZUp` |
| `forceRegenerate` | `false` | 遗留开关；cook/preview 忽略（从不调 API）。重新生成请点 Generate |
| `path` | `""` | 保存后的模型路径（`Assets/….glb`）；Generate→Save 写入，cook 优先加载 |

### 缓存

- 目录：`<Project>/Library/PCG/MeshyCache/<sha256>.glb`
- Key 由节点 id、texture/imageUrl、模型参数与源图 PNG 哈希组成
- `Library/` 通常已被 Unity / git 忽略，不会进仓库

### 相关代码

| 职责 | 路径 |
|------|------|
| API Key | `Unity/Assets/PcgPlugin/Runtime/PcgMeshySettings.cs` |
| HTTP 客户端 | `Unity/Assets/PcgPlugin/Runtime/PcgMeshyClient.cs` |
| Cook 前解析 | `Unity/Assets/PcgPlugin/Runtime/PcgMeshyResolver.cs` |
| Settings UI | `PcgSettingsWindow.cs`、`PcgProjectSettingsProvider.cs` |
| C++ 元素 | `pcg-core/src/elements/assembly_elements.cpp`（`Meshy3DGenerator`） |
| Cook hash | `pcg-core/src/cook_hash.cpp`（与 `ImportMesh` 同路径指纹） |
| Manifest | `schema/node-manifest.json` → `scripts/sync-manifest.sh` |

### 常见问题

| 现象 | 处理 |
|------|------|
| `API key missing` | Settings 填 Key 后重试 |
| `assign a Source Image` | 指定 texture 或合法 `imageUrl` |
| 任务 FAILED / Credits | 查 [Meshy 控制台](https://www.meshy.ai/settings/api)；失败任务通常退还 Credits |
| cook 报未知节点 | 重建并重启 `pcg-server`（`scripts/build-pcg-server.sh`） |
| 想强制重跑 | 点 Inspector **Regenerate**，保存覆盖（或另存） |
| 改参数就 cook 报错 | 无已保存/`MeshyCache` 模型时不会自动生成；点 Generate 并保存后再 preview |

---

## Tripo 3D Generator

| 项 | 值 |
|----|-----|
| 节点类型 | `Tripo3DGenerator` |
| 显示名 | Tripo 3D Generator |
| 分类 | Mesh |
| 官方文档 | [Tripo OpenAPI](https://docs.tripo3d.ai/get-started/overview.html) |
| Base URL | `https://openapi.tripo3d.ai/v3` |
| 创建任务 | `POST /v3/generation/image-to-model` |
| 查询任务 | `GET /v3/tasks/:id` |
| 上传图片 | `POST /v3/files`（本地 texture 时） |

### 用法

Web：Settings 填 Key → 图中添加节点 → 指定 Source Image / Image URL → Inspector **Generate** → GLB 自动写入 Web 缓存并回填 `path` → Preview / Cook。需要程序化交付时继续点 **High-Fidelity Proceduralize**。

Unity：沿用 Editor Generate / 保存到 `Assets/` 的既有流程。

### 节点属性

| 属性 | 默认 | 说明 |
|------|------|------|
| `texture` | `""` | Source Image |
| `imageUrl` | `""` | 可选公网 URL / `data:` |
| `modelVersion` | `v3.1-20260211` | `v3.1-20260211` / `P1-20260311` / `v2.5-20250123` |
| `shouldTexture` | `true` | 是否生成贴图 |
| `enablePbr` | `false` | PBR |
| `faceLimit` | `30000` | 面数上限 |
| `scale` | `1.0` | 导入缩放 |
| `axisConversion` | `none` | 轴向转换 |
| `path` | `""` | Generate→Save 写入的 GLB 路径 |

### 缓存

- Web：`<Project>/library/web-cache/TripoCache/<request-key>.glb`
- Unity：`<Project>/Library/PCG/TripoCache/<request-key>.glb`
- 请求键包含源图身份、模型版本、texture/PBR、face limit；它不是下载后 GLB 的内容哈希

### 相关代码

| 职责 | 路径 |
|------|------|
| API Key | `PcgTripoSettings.cs` |
| HTTP 客户端 | `PcgTripoClient.cs` |
| Cook 前解析 | `PcgTripoResolver.cs` |
| Inspector Generate | `PcgTripoGenerateAction.cs` |
| C++ 元素 | `assembly_elements.cpp`（`Tripo3DGenerator`） |
| Web 凭据与 API | `pcg-server/src/third_party_service.cpp` |
| Web typed client | `web/pcg-editor/src/thirdPartyClient.ts` |
| base-color→OPC 颜色场 | `web/pcg-editor/src/orientedPointColorBake.ts` |
| 六视角 × 六通道程序化桥 | `web/pcg-editor/src/proceduralReference.ts` |
| oriented SDF / OPC1 v2 / Surface Nets | `pcg-core/src/elements/oriented_sdf_surface.cpp` |
| GLB→重建节点/PBR 烘焙服务 | `pcg-server/src/surface_reconstruction_service.cpp`、MCP `pcg_bake_oriented_sdf` |
| 六视图像素比较 | `web/pcg-editor/scripts/compare_procedural_reference.py` |
| GLB 分层轮廓测量 | `web/pcg-editor/src/meshBandProfile.ts` |
| 语义组件树、geodesic/rigid skin 与动画控制 | `web/pcg-editor/src/actionRuntime.ts`、`ActionRig` |
| 原 GLB rig/skin/animation 保留 | `web/pcg-editor/src/preservedGltfRuntime.ts`、`PreserveGltfRig`、`GET /v1/assets/preserved-gltf` |
| 带 skin/组件/动画的 GLB 导出 | `web/pcg-editor/src/previewGlbExport.ts` |
| 诊断渲染通道 | `web/pcg-editor/src/PreviewViewport.tsx`、`pcg-server/src/mcp_service.cpp` |
| 变截面曲线扫掠 | `pcg-core/src/elements/spline_algorithms.cpp`（`TaperedSweep`） |

---

## 扩展下一厂商

按 Meshy 同构扩展即可，避免在 C++ 里写 HTTPS。

### 清单

1. **Settings**  
   - `PcgTripoSettings`（或通用 `PcgThirdPartyApiSettings` 分 vendor 键）  
   - Pref 例：`PCG.Tripo.ApiKey`  
   - 在 Settings / Project Settings 增加 **Tripo** 区块  

2. **Client**  
   - `PcgTripoClient`：create → poll → download（对齐该厂商文档）  

3. **Resolver**  
   - `PcgTripoResolver.TryPrepareForCook`（**仅注入本地 path，禁止在 cook/preview 调 API**；优先用户保存路径，其次 vendor cache）  
   - 缓存目录：`Library/PCG/TripoCache/`  
   - Inspector **Generate** async action：下载后 **SaveFilePanelInProject** 保存到 `Assets/`，写入节点 `path`（对齐 `PcgMeshyGenerateAction`）  
   - 在 `PcgGraphComponent` / `PcgGraphLoader` / FBX export 路径里与 Meshy 一样、在 collect assets 之前调用  

4. **Manifest**  
   - 类型建议：`Tripo3DGenerator`  
   - 显示名：`Tripo 3D Generator`  
   - 分类：`Mesh`  
   - 输出：`out` / `SpatialMesh`  
   - `scripts/sync-manifest.sh`  

5. **C++**  
   - 复用 `execute_import_like`（或与 Meshy 共用同一 helper）  
   - `register_assembly_elements` + `cook_hash` 把新类型与 `ImportMesh` 同等处理  
   - 重建 `pcg-server`  

6. **文档 / 测试**  
   - 在本页增加「Tripo 3D Generator」小节  
   - EditorPrefs 单测 +（可选）用本地 fixture path 跑 graph 测试（不连真网）  

### 命名约定

| 项 | 约定 |
|----|------|
| 节点 `type` | `<Vendor>3DGenerator`（PascalCase） |
| Pref 键 | `PCG.<Vendor>.ApiKey` |
| 缓存目录 | `Library/PCG/<Vendor>Cache/` |
| Settings 分组标题 | `<Vendor> (Image to 3D)` |

### 不要做的事

- 不要把 API Key 放进 ScriptableObject / `.pcg` / 示例图  
- 不要在 `pcg-core` 里直接调云端（阻塞 cook、难取消、难测）  
- 不要为每个厂商复制一整份 Assimp 导入逻辑——共享 `execute_import_like`

---

## 路线图（占位）

| 厂商 | 节点 | 状态 |
|------|------|------|
| Meshy | `Meshy3DGenerator`（图生 3D，支持 1–4 张多视图） | 已接入 |
| Meshy | `MeshyTextTo3D`（文生 3D v2，Generate 自动 preview→refine） | 已接入 |
| Meshy | `MeshyMeshOps`（remesh / resize / uv-unwrap，GLB 输入） | 已接入 |
| Meshy | `MeshyRetexture`（文本/风格图重贴图，GLB 输入） | 已接入 |
| Meshy | `MeshyImageGen`（文生图/图生图，Texture 输出） | 已接入 |
| Tripo | `Tripo3DGenerator` | 已接入 |
| （其他） | — | 按上方清单追加 |

### Meshy 新节点要点

- **同一契约**：Generate 是唯一云端入口；cook/preview/auto-cook 只读本地 `path`（或缓存），永不调 API；无 Key 但有缓存仍可 cook。
- **GLB 输入节点**（`MeshyMeshOps` / `MeshyRetexture`）：Generate 时先把上游 `in` 边 cook 成几何，用 `PcgGlbWriter`（纯 C# glTF 2.0 导出）打成 GLB data URI 再提交。uv-unwrap 的 `model_url` 仅支持 `.glb`；resize/uv-unwrap 强制只产 GLB（仅 remesh 走 GLB/FBX 开关）。
- **`MeshyTextTo3D`**：`/openapi/v2/text-to-3d`，`shouldTexture=true` 时一次 Generate 串联 preview + refine 两个任务；`false` 只跑 preview（白模）。
- **`MeshyImageGen`**：输出 Texture pin，像素走 TextureRuntime（与 `ImageTexture` 同槽位机制），可直接喂给 `MeshNoiseDeform` 等纹理消费端；Generate 先把 PNG 存盘。
- **`Meshy3DGenerator` 多视图**：`texture2/3/4` 任一赋值即自动切 `/openapi/v1/multi-image-to-3d`（1–4 张）。
- 实现文件：`Runtime/PcgMeshyExtraResolvers.cs`（4 个 resolver + `PcgThirdPartyResolvers.TryPrepareAll` 统一链）、`Editor/Graph/PcgMeshyExtraGenerateActions.cs`（4 个 Generate action）、`Runtime/PcgGlbWriter.cs`、`Editor/PcgMeshyUpstreamExport.cs`。

---

## 参考

- [Meshy API](https://docs.meshy.ai/en/api)
- [pcg-server](./pcg-server.md)
- [ImportMesh（node-reference）](./node-reference.md#importmesh)
- [开发手册 §10 新增节点](./PCG-AI-Development-Manual.md#101-新增一个-c-节点)
