# 第三方图生 3D API（Meshy / Tripo / …）

PCG-AI 把云端「图生 3D」厂商接入成 **Graph 节点**：Editor 侧负责鉴权、HTTP、轮询与本地缓存；`pcg-core` / `pcg-server` 只负责像 `ImportMesh` 一样读入已下载的网格。

本页是用户说明 + 后续厂商扩展清单。节点属性以 `schema/node-manifest.json` 为准。

---

## 设计原则

| 原则 | 做法 |
|------|------|
| 密钥不进工程 | API Key 只存本机 `EditorPrefs`，不写 `PcgProjectSettings.asset` / `.pcg` |
| 网络不进 C++ | 长轮询、HTTPS、Credits 全在 Unity Editor；C++ 只读本地文件 |
| 缓存可复用 | 成功结果落在 `Library/PCG/<Vendor>Cache/`，同参数再 cook 不扣费 |
| 节点形态统一 | 无输入 pin，输出 `SpatialMesh`；cook 前注入绝对 `path`，执行侧与 `ImportMesh` 同路径 |

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
6. Cook。首次会弹进度条并请求 Meshy；成功后缓存 GLB，并把路径写入节点的 `path`（仅执行 JSON，不强制改盘上的 `.pcg` 作者数据）。

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
| `forceRegenerate` | `false` | 忽略本地缓存，重新调用 API |
| `path` | `""` | 缓存绝对路径；由 Resolver 在 cook 前注入 |

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
| 想强制重跑 | 勾选 `forceRegenerate`，或删对应 `MeshyCache` 文件 |

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
   - `PcgTripoResolver.TryPrepareForCook`  
   - 缓存目录：`Library/PCG/TripoCache/`  
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
| Meshy | `Meshy3DGenerator` | 已接入 |
| Tripo | `Tripo3DGenerator` | 待做 |
| （其他） | — | 按上方清单追加 |

---

## 参考

- [Meshy API](https://docs.meshy.ai/en/api)
- [pcg-server](./pcg-server.md)
- [ImportMesh（node-reference）](./node-reference.md#importmesh)
- [开发手册 §10 新增节点](./PCG-AI-Development-Manual.md#101-新增一个-c-节点)
