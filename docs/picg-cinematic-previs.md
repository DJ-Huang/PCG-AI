# PICG 影视预演工作流

PICG（程序化智能内容生成）把程序化场景和镜头编排放在同一个 Web 工程中。场景在 **Graph** 中生成；相机、轨迹、镜头属性和输出规格在 **Cameras** 中编辑。两者共用预览、时间采样和本地 Agent 接口。

## 启动与示例

在工程根目录运行 `./scripts/run-picg-web.sh`，打开 `http://127.0.0.1:5173`。

点击 **Open**，输入 `examples/tests/picg-camera-previs.picg`，会同时读取旁边的 `.picgshot`。示例是一个带材质的简易预演场地，以及 4 秒的相机轨迹、焦距和焦点动画。点击 **Cameras** 开始操作。

## 操作相机

1. 在 Cameras 图中选择 Camera，左侧 **Scene** 显示相机、视锥、轨迹和 XYZ 操作轴。超出画面的新选中相机会自动定位；预览中按 **F** 可对准选中相机或路径。
2. **W / Move** 平移，**E / Rotate** 旋转；可切换 World / Local。**Snap** 使用 0.1 米位移和 5° 旋转步长。
3. Auto key 关闭时，移动或旋转会创建或复用 **Camera → Transform → Shot Output**。Transform 保存位移与旋转偏移，可在 Inspector 中数值编辑。它也会作用于整条相机轨迹。
4. **Camera view** 按输出宽高比显示实际镜头，锁定普通轨道导航，避免查看时意外改变镜头。**Guides** 显示三分线和 90% 安全框，不进入视频。
5. **Use view** 将当前 Scene 视角用于镜头。预览区右侧边缘可拖动调整宽度。

Transform 的旋转是相机朝向偏移，不会围绕世界原点旋转整条轨迹。世界坐标使用米；相机侧使用 Three.js 的右手 Y-up 坐标，几何节点侧仍沿用原有 Unity 坐标转换约定。

## 轨迹与属性曲线

**+ Motion Curve → Camera** 会添加轨迹、连接当前 Camera，并创建从 0 到 1 的路径进度关键帧。

- 在 Scene 点击黄色路径点，或在 Inspector 点击 P1/P2 等按钮，用 XYZ 轴拖动；也可输入坐标、增删点和切换闭环。
- **Follow path** 沿路径切线朝向；**Look at target** 始终朝向相机的目标位置。目标本身也能打关键帧。
- 下方 **Animation** 提供位置、目标、up、焦距、焦点、光圈、曝光、镜头偏移、传感器和裁剪距离等数值通道。**Path progress** 控制相机沿轨迹的位置与速度，0 是起点，1 是终点。
- **+ Channel key** 在播放头处记录当前通道；拖动曲线上的点改变时间和数值，也可精确输入帧号和值。关键帧会对齐帧。
- 曲线编辑只移动或删除当前属性，保留同帧的其他属性；XYZ 向量以一组姿态属性存储。时间线上的菱形则代表该时刻的一组属性，拖动它会移动整组。
- 支持线性、渐入、渐出、平滑和 step 保持。插值方式描述到达该关键帧的区间。
- **Auto key** 开启时，Inspector 和相机操作轴将修改记录到当前帧。关闭时，姿态操作改变 Transform；已有镜头属性关键帧可直接修改，未打关键帧的属性修改相机基础值。

连接的 Motion Curve 持有可编辑轨道。Camera 自身旧关键帧仍可读取和采样，连接轨迹提供路径位置，轨迹上的显式属性关键帧最后覆盖对应通道，再应用 Transform。手动打位置关键帧会覆盖相应的路径位置；只调整镜头或路径速度时，编辑焦距、焦点或 Path progress 即可。

## 播放与输出

| 操作 | 快捷键 / 入口 |
| --- | --- |
| 播放 / 暂停当前镜头 | Cameras 页空格 |
| 前后 1 帧 | 左右箭头 |
| 前后 10 帧 | Shift + 左右箭头 |
| 新增相机 | Shift + A 或右键菜单 |
| 位移 / 旋转 | W / E |
| 对准选择 | 预览或相机图内 F |
| 撤销 / 重做 | Cmd/Ctrl + Z / Shift + Z |
| 保存 / 另存 | Cmd/Ctrl + S / Shift + S |
| 打开工程 | Cmd/Ctrl + O |

在 **Shot Output** Inspector 中命名镜头、选择 HD/UHD、2.39:1、竖屏等规格。时间线也可设置时长、帧率和分辨率。缩短时长时会保留已有关键帧；先移动或删除超出新范围的关键帧，再缩短镜头。

**Export H.264 MP4** 按 `frame / fps` 逐帧采样和编码，不依赖屏幕播放帧率。浏览器不支持 H.264 时尝试 VP9 WebM。导出隐藏网格、操作轴和轨迹，保留镜头画幅、属性动画与景深。支持取消并恢复编辑视角。当前输出是无音轨的镜头预演视频，材质预览与景深不属于离线路径追踪渲染。

视频自动保存到工作区 `exports/previs/`，并提供 **Download video / Watch video**。本地保存不可用时保留浏览器下载入口；新导出使用独立文件名，不覆盖之前的镜头。

## 保存与兼容

- **Save / Save As** 保存工作区的 `.picg` 几何图及同名 `.picgshot` 相机文件；**Open** 成对读取。
- **Export project** 将包含几何和完整相机数据的单个 `.picgproject` 保存到 `exports/documents/`，状态栏显示实际路径，可用 **Import** 恢复；资源路径保留原引用，不会把外部纹理和模型打包进去。
- **Export shot / Import shot** 单独交换相机设置。导出的 `.picgshot` 同样保存在 `exports/documents/`，并提供下载链接。导入单个几何图会初始化新镜头；使用完整工程或继续导入 shot 可恢复镜头。
- 旧 `.pcg` 文件、`pcg_*` MCP 工具名、Unity 菜单和已有资源标识继续兼容。新文件与主要 Web/Agent 产品入口使用 PICG。
- 源码目录、库 ABI、资源协议及用户存储位置保留兼容，例如 `web/pcg-editor`、`pcg-server`、`pcg-resource://`。不需要移动已有工程或重新登录供应商。

## Agent 镜头编辑

MCP 地址仍为 `http://127.0.0.1:17890/mcp`。工具列表以 **picg_** 为主要前缀，旧 **pcg_** 名称仍可调用。

先读 `picg_get_editor_context` 和 `picg_get_shot`，用最新 `shotHash` 调用 `picg_apply_shot_ops`。支持相机、Motion Curve、Transform、关键帧、连线、选择和镜头规格的原子编辑。整个批次先校验，再作为一次撤销提交；旧 hash 返回 `shot_conflict`，无效批次不会部分修改文档。

以下参数中的 `ifShotHash` 必须替换为刚读取的值；多页面时还需指定选定的 `editorSessionId`：

```json
{
  "ifShotHash": "<latest shotHash>",
  "operations": [
    { "op": "set_shot", "name": "Dolly and focus", "durationSeconds": 4, "fps": 24, "width": 1280, "height": 720 },
    { "op": "upsert_camera", "id": "hero", "name": "Hero", "camera": { "position": [5, 2.5, 6], "target": [0, 1, 0], "focalLengthMm": 28 } },
    { "op": "upsert_motion_curve", "id": "rail", "cameraId": "hero", "lookMode": "target", "controlPoints": [[5, 2.5, 6], [3, 2, 5], [0.5, 1.8, 4.8], [-1.8, 1.7, 4.5]] },
    { "op": "set_transform", "cameraId": "hero", "translation": [0, 0, 0], "rotationEulerDeg": [0, 0, 0] },
    { "op": "set_keyframes", "nodeId": "rail", "mode": "replace", "keyframes": [
      { "id": "in", "timeSeconds": 0, "interpolation": "linear", "value": { "pathProgress": 0, "focalLengthMm": 28, "focusDistance": 8 } },
      { "id": "out", "timeSeconds": 4, "interpolation": "ease-in-out", "value": { "pathProgress": 1, "focalLengthMm": 42, "focusDistance": 4.6 } }
    ] },
    { "op": "select", "nodeId": "hero" }
  ]
}
```

随后用 `picg_preview_shot` 定位时间，`picg_capture_preview` 采集取景证据，最后用 `picg_save_graph` 保存图与镜头文件。几何编图继续走 live manifest、图操作、校验、cook、capture、save 流程。

Agent 可用 `picg_export_shot({ifShotHash, editorSessionId?, timeoutMs?})` 导出当前镜头，取得实际本地视频路径。长镜头应等待当前编码结束，不要因一次观察超时重复启动导出。
