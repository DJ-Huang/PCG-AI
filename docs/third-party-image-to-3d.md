# 第三方图生 3D API（Meshy / Tripo / …）

PCG-AI 把云端「图生 3D」厂商接入成 **Graph 节点**：Editor 侧负责鉴权、HTTP、轮询与本地缓存；`pcg-core` / `pcg-server` 只负责像 `ImportMesh` 一样读入已下载的网格。

本页是用户说明 + 后续厂商扩展清单。节点属性以 `schema/node-manifest.json` 为准。

---

## 设计原则

| 原则 | 做法 |
|------|------|
| 密钥不进工程 | API Key 只存本机 `EditorPrefs`，不写 `PcgProjectSettings.asset` / `.pcg` |
| 网络不进 C++ | 长轮询、HTTPS、Credits 全在 Unity Editor；C++ 只读本地文件 |
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

与 Meshy 相同：Settings 填 Key → 图中添加节点 → 指定 Source Image / Image URL → Inspector **Generate** → 保存 GLB 到 `Assets/` → Preview / Cook。

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

- 目录：`<Project>/Library/PCG/TripoCache/<sha256>.glb`

### 相关代码

| 职责 | 路径 |
|------|------|
| API Key | `PcgTripoSettings.cs` |
| HTTP 客户端 | `PcgTripoClient.cs` |
| Cook 前解析 | `PcgTripoResolver.cs` |
| Inspector Generate | `PcgTripoGenerateAction.cs` |
| C++ 元素 | `assembly_elements.cpp`（`Tripo3DGenerator`） |

---

## 扩展下一厂商（例如 Tripo）

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
