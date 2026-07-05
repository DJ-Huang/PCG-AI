# PCG Graph AI

Web React Flow 编辑器 → Graph JSON → C++ 核心 → Unity 场景预览。

本文档是**完整操作手册**：从环境准备、Web 编辑、Unity 联调到 IL2CPP 发布。

---

## 目录

1. [系统要求](#系统要求)
2. [仓库结构](#仓库结构)
3. [首次克隆后](#首次克隆后)
4. [Web 编辑器（必做）](#web-编辑器必做)
5. [Unity 编辑器](#unity-编辑器)
6. [C++ 核心编译（Windows）](#c-核心编译windows)
7. [端到端编辑循环（M2）](#端到端编辑循环m2)
8. [IL2CPP 发布（M3）](#il2cpp-发布m3)
9. [示例 Graph 文件](#示例-graph-文件)
10. [常见问题](#常见问题)
11. [CI 与脚本](#ci-与脚本)

---

## 系统要求

| 组件 | 要求 | 说明 |
|------|------|------|
| **Node.js** | 18+ | 仅 Web 编辑器需要；macOS / Windows / Linux 均可 |
| **Visual Studio 2022** | x64 工具链 | 编译 `pcg-core`；**仅 Windows** |
| **CMake** | 3.20+ | 与 VS 2022 配合 |
| **Unity** | 2022.3+ | 已用 Tuanjie 1.6.x 验证；**建议 Windows**（原生插件为 Win64） |

> **macOS 用户**：可编译 `libPcgCore.dylib` 在 Unity Editor（Apple Silicon）联调 Run / 预览；IL2CPP Player 仍依赖 Windows 静态链。

---

## 仓库结构

```
PCG-AI/
├── pcg-core/              C++ 核心（CMake，DLL + LIB 双产物）
│   ├── include/pcg_api.h    对外 C API（唯一公开接口）
│   ├── src/                 实现
│   ├── tests/               冒烟测试
│   └── build/               CMake 输出（gitignore）
│
├── schema/                Graph JSON 契约（Web / C++ / Unity 共用）
│   ├── graph-schema.json    JSON Schema v1
│   ├── graph-schema-v2.json JSON Schema v2（UE PCG 对齐，Phase 4.0）
│   ├── node-manifest.json   节点 Pin/参数定义（编辑器消费）
│   ├── example.pcg     示例图
│   └── editor-export.pcg   Web → Unity 热更新目标（运行后生成）
│
├── web/pcg-editor/        Vite + React + @xyflow/react（Web 编辑器）
│   ├── package.json         ← npm 命令在此目录执行
│   └── src/
│       ├── nodes/           三种自定义节点
│       ├── graphSchema.ts   与 Graph JSON v1 对齐的 TS 类型
│       └── exportGraph.ts   导出与 Send to Unity
│
├── Unity/                 Unity 工程（PcgPlugin）
│   └── Assets/PcgPlugin/
│       ├── Runtime/         PcgNative、PcgGraphLoader、PcgPreview
│       ├── Editor/          菜单、Graph 监视、IL2CPP 链接
│       └── Plugins/x86_64/  PcgCore.dll / PcgCore.lib
│
├── scripts/               Windows 构建与校验脚本（PowerShell）
├── examples/              分发用示例图
└── docs/DEMO.md           端到端演示补充说明
```

**数据流：**

```
Web 画布编辑 → Graph JSON → pcg-core 执行 → Unity Gizmo 预览
```

---

## 首次克隆后

1. 克隆仓库后，**不要在仓库根目录**执行 `npm install`——根目录没有 `package.json`。
2. Web 依赖安装在子目录：`web/pcg-editor/`（见下一节）。
3. 若根目录误生成 `package-lock.json`，可删除，不影响使用。
4. Unity 侧首次打开 `Unity/` 工程，等待脚本编译完成。

---

## Web 编辑器（必做）

Web 编辑器是独立前端子工程，所有 npm 命令必须在 `web/pcg-editor` 下执行。

### 安装依赖

**Windows（PowerShell）**

```powershell
cd web\pcg-editor
npm install
```

**macOS / Linux**

```bash
cd web/pcg-editor
npm install
```

### 启动开发服务器

```bash
npm run dev
```

浏览器打开：**http://localhost:5173/**

### 界面说明

| 操作 | 说明 |
|------|------|
| **+ ParseConfig** | 添加配置解析节点（`seed`、`density`） |
| **+ SpawnPoints** | 添加点生成节点（`count`、`radius`） |
| **+ PlaceInScene** | 添加场景放置节点（`prefab`、`scale`） |
| 拖拽连线 | 按节点 handle 连接；非法连接会被拒绝 |
| **Export JSON** | 下载 `graph.pcg` 到本机（不依赖 dev server 写盘） |
| **Import JSON** | 从本机加载 `.pcg`（Unity Graph **Export…** 或 **Export JSON** 产物） |
| **Send to Unity** | 将当前图写入 `schema/editor-export.pcg`（**必须** `npm run dev` 运行中） |

默认画布已包含一条三节点流水线：`ParseConfig → SpawnPoints → PlaceInScene`。

### 其他 npm 命令

```bash
npm run build    # 生产构建（dist/）
npm run preview  # 预览生产构建
npm run lint     # oxlint 检查
```

### Send to Unity 原理

开发模式下，Vite 插件暴露 `POST /api/export-graph`，将 JSON 写入仓库内 `schema/editor-export.pcg`。  
**生产构建（`npm run build`）不包含此 API**；离线场景请用 **Export JSON** 手动保存，再在 Unity 中 **Run Graph from File…** 加载。

---

## Unity 编辑器

### 打开工程

用 Unity Hub 打开仓库中的 `Unity/` 目录（不是仓库根目录）。

### 菜单项（顶部 **PCG**）

| 菜单 | 作用 |
|------|------|
| **PCG → Print PcgCore Version** | 验证原生库已加载；Console 应输出 `pcg-core 0.1.0`（M0 里程碑） |
| **PCG → Settings** | 配置监视路径、自动重载等 |
| **PCG → Set Watched Graph…** | 选择要监视的 `.pcg` 文件 |
| **PCG → Reload Watched Graph** | 手动重新执行监视中的图并更新预览 |
| **PCG → Run Graph from File…** | 从任意路径选择 JSON 执行（不依赖 Web） |

### 场景预览

执行图后，场景中会出现 **PCG Preview** 对象，Scene 视图显示青色球体 Gizmo 表示生成点。

### 验证 M0（原生库就绪）

1. 完成 [C++ 核心编译](#c-核心编译windows) 并将 `PcgCore.dll` 复制到 `Plugins/x86_64/`。
2. 重启 Unity（若 DLL 曾被占用）。
3. **PCG → Print PcgCore Version** → Console 打印版本号即成功。

---

## C++ 核心编译

### Windows（x64）

原生库为 **Windows x64**；Unity Editor 使用 `PcgCore.dll`，IL2CPP Player 使用静态链接的 `PcgCore.lib`。

### 方式一：推荐脚本（一键构建 + 拷贝 + 测试）

在仓库根目录 PowerShell 中：

```powershell
.\scripts\build-pcg-core.ps1 -CopyToUnity -RunTests
```

产物自动复制到 `Unity/Assets/PcgPlugin/Plugins/x86_64/`。

### 方式二：手动 CMake

```powershell
cd pcg-core
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

输出：

- `pcg-core/build/Release/PcgCore.dll` — Unity Editor
- `pcg-core/build/Release/PcgCore.lib` — IL2CPP Standalone Win64

手动拷贝 DLL：

```powershell
Copy-Item pcg-core\build\Release\PcgCore.dll Unity\Assets\PcgPlugin\Plugins\x86_64\
```

| 模式 | 原生产物 | PluginImporter |
|------|----------|----------------|
| Unity Editor | `PcgCore.dll` | Editor: 开，Standalone: 关 |
| IL2CPP Player | `PcgCore.lib` | Editor: 关，Standalone Win64: 开 |

### macOS（Apple Silicon Editor）

Unity Editor 使用 `libPcgCore.dylib`（`Plugins/macOS/`）；IL2CPP macOS Player 尚未接入。

**前置**：Xcode Command Line Tools + CMake 3.20+（`brew install cmake`）。

在仓库根目录：

```bash
./scripts/build-pcg-core.sh --copy-to-unity --run-tests
```

产物复制到 `Unity/Assets/PcgPlugin/Plugins/macOS/`。重启 Unity 后 **PCG → Print PcgCore Version** 应输出 `pcg-core 0.1.0`。

| 模式 | 原生产物 | PluginImporter |
|------|----------|----------------|
| Unity Editor (macOS) | `libPcgCore.dylib` | Editor ARM64: 开 |
| IL2CPP Player (macOS) | 未接入 | — |

---

## 端到端编辑循环（M2）

完整链路：**Web 改图 → JSON → C++ 执行 → Unity 预览**。

### 步骤 1：启动 Web

```bash
cd web/pcg-editor
npm install    # 首次
npm run dev
```

### 步骤 2：在浏览器编辑

1. 打开 http://localhost:5173
2. 调整节点参数（如 `seed`、`count`、`radius`）
3. 点击 **Send to Unity**  
   - 成功提示：`Saved to schema/editor-export.pcg`  
   - 失败常见原因：未运行 `npm run dev`，或不在 `web/pcg-editor` 目录启动

### 步骤 3：Unity 加载并预览

1. 打开 `Unity/` 工程
2. **PCG → Set Watched Graph…** → 选择 `schema/editor-export.pcg`  
   （也可在 **PCG → Settings** 中配置同一路径）
3. 开启 **Auto Reload**，或手动 **PCG → Reload Watched Graph**
4. Scene 视图中 **PCG Preview** 上的青色球体应随参数变化

更细的演示说明见 [docs/DEMO.md](docs/DEMO.md)。

---

## IL2CPP 发布（M3）

面向 Windows Standalone 运行时（非 Editor）。

### 工程设置

1. **File → Build Settings** → Platform：**Windows**
2. **Player Settings → Other Settings**
   - Scripting Backend：**IL2CPP**
   - Architecture：**x86_64**
3. 将含 `PcgRuntimeRunner` + `PcgPreview` 的场景加入 Build（如 `Assets/Scenes/SampleScene`）

### 场景运行时组件

1. 创建空物体 `PCG Runtime`
2. 添加：`PcgPreview`、`PcgRuntimeRunner`
3. `PcgRuntimeRunner` 在 `Start` 时加载 `StreamingAssets/pcg/demo.pcg`

### 构建前确保 lib 就绪

```powershell
.\scripts\build-pcg-core.ps1 -CopyToUnity -RunTests
```

### 构建后校验

```powershell
.\scripts\verify-release-package.ps1 -PlayerBuildPath "Build\Windows"
```

**预期 Player 日志：**

```
[PCG] Runtime executing graph: .../StreamingAssets/pcg/demo.pcg (core pcg-core 0.1.0)
[PCG] Runtime OK — 100 points generated.
```

**发布包不应包含：**

- `PcgCore.dll`（仅 Editor 用的动态库）
- 任何 `pcg-core` 的 `.cpp` 源码  

静态符号通过 `PcgCore.lib` 链入 `GameAssembly.dll`。

---

## 示例 Graph 文件

| 文件 | 用途 |
|------|------|
| `examples/phase41-demo.pcg` | Phase 4.1 地形采样 + 点阵 + Spawner 流水线 |
| `schema/example.pcg` | 与 schema 对齐的参考图 |
| `schema/editor-export.pcg` | Web **Send to Unity** 写入的热更新目标 |
| `Unity/Assets/StreamingAssets/pcg/demo.pcg` | Player 运行时默认输入 |

Graph 契约定义：`schema/graph-schema.json`（版本 `1.0`）。

---

## 常见问题

### Web

| 现象 | 处理 |
|------|------|
| `ENOENT: no such file or directory, open '.../package.json'` | 在**仓库根目录**执行了 `npm install`；应 `cd web/pcg-editor` 后再执行 |
| **Send to Unity** 失败 | 确认 `npm run dev` 正在运行；检查浏览器控制台与终端无报错 |
| 端口 5173 被占用 | 关闭占用进程，或 `npx vite --port 5174` 临时换端口 |

### Unity Editor

| 现象 | 处理 |
|------|------|
| `DllNotFoundException` | 重新运行 `build-pcg-core.ps1 -CopyToUnity`；关闭 Unity 后重拷 DLL |
| 没有 **PCG** 菜单 | 查看 Console 中 `PcgPlugin.Editor` 编译错误 |
| 预览无变化 | 确认监视路径指向正确的 `.pcg`；手动 **Reload Watched Graph** |
| `Copy-Item` 失败 | Unity 锁定 DLL；脚本会写 `.dll.new`，关 Unity 后手动替换 |

### IL2CPP

| 现象 | 处理 |
|------|------|
| `LNK2019 pcg_*` 未解析 | 确认 `PcgIl2CppBuildProcessor` 存在；`PcgCore.lib` 在 `Plugins/x86_64`；重跑 `build-pcg-core.ps1 -CopyToUnity` |
| `LNK1181 ... PCG.obj` | 工程路径含空格时 il2cpp 可能错误拆分 `--linker-flags`；已自动复制 lib 到 `%TEMP%\PcgCoreIl2CppLink\` |
| CRT 链接错误 | 使用 `/MT` 重建：`.\scripts\build-pcg-core.ps1 -CopyToUnity` |

---

## CI 与脚本

| 资源 | 说明 |
|------|------|
| `.github/workflows/pcg-core-ci.yml` | Windows `windows-latest`：Release 构建 + `ctest` |
| `scripts/build-pcg-core.ps1` | Windows：配置、编译、可选测试与拷贝到 Unity |
| `scripts/build-pcg-core.sh` | macOS：配置、编译、可选测试与拷贝到 Unity |
| `scripts/verify-release-package.ps1` | IL2CPP 构建产物校验 |

---

## 里程碑速查

| 里程碑 | 验收方式 |
|--------|----------|
| **M0** | Unity：**PCG → Print PcgCore Version** → Console 输出 `pcg-core 0.1.0` |
| **M2** | Web **Send to Unity** → Unity **Reload Watched Graph** → Scene Gizmo 更新 |
| **M2.5** | Unity **Graph Editor** Run + JSON↔Web round-trip + `ctest` 绿 |
| **M3** | IL2CPP Windows 构建 + `verify-release-package.ps1` 通过 + Player 日志正常 |
| **M4** | Phase 4.1：13 UE 原语节点 + manifest 驱动 Unity Graph + `ctest` 绿 |
