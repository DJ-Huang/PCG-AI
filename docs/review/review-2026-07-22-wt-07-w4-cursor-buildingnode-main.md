# Code Review: wt/07-w4-cursor-buildingnode -> main

## 审查范围

- Source: `wt/07-w4-cursor-buildingnode` (`567dd96acf2113a593416f1e0f281861a07483a6`)
- Target: `main` (`6255495ef0469c7565b525aecff8cfab525b58d2`)
- Merge base: `6255495ef0469c7565b525aecff8cfab525b58d2`
- Diff: `main...HEAD`，338 files，约 63,109 additions / 16,687 deletions
- 工作区在审查开始时无未提交变更；未把工作区内容混入审查范围。

## 变更概述

本分支同时引入 PCG native 节点与几何算法、Graph v3/外部子图、Unity GraphView 编辑能力、多 prototype scatter、manifest/schema 扩展及 lot-city 示例。关键数据流是：Graph authoring -> 子图解析/展开 -> 参数覆盖 -> native ABI -> mesh/point binary -> Unity 展示；主要风险集中在外部子图资产所有权、跨版本二进制契约、平台插件同步和编辑器状态事务。

规则依据：`pcg/project-engineering` 要求 C++ 注册、构建源、manifest、测试一致，native 修改同步到 Unity 实际加载目录，Graph Parameter 绑定目标有效。经验库额外提示 PCG cook 生命周期、C ABI 契约和 native shared-library 同步风险。

## Findings

### F1 [Critical] 保存 consumer 会把访问过的子图资产旧快照写回磁盘
- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphView.cs:1619`
- Trigger: 在 consumer A 中进入 linked subgraph；随后从另一个窗口修改并保存同一 `.pcgsubgraph`；再保存未修改该子图的 consumer A。
- Impact: consumer A 无条件将会话中的旧副本覆盖新文件，造成已保存用户数据丢失。
- Evidence: `EnsureExternalNavigationLoaded` 在 `2165` 将资产克隆进 `m_RootDocument`，`2198` 仅记录“访问过”的 GUID，没有 dirty/version 状态；`TryFlushExternalNavigationAssets` 在 `1600-1619` 遍历所有记录并直接 `File.WriteAllText`。外部变更协调只刷新可见 node snapshot，不提供冲突检测。
- Fix: 只 flush 本窗口实际编辑过的 linked asset；记录加载版本/hash，并在写入前检测磁盘变化，冲突时阻止保存或要求重新加载。
- Test: 两个编辑器窗口打开同一 linked asset；窗口 B 修改保存后，窗口 A 只保存 consumer；断言子图文件字节不变。修复前该测试会被旧快照覆盖。

### F2 [Major] Windows 原生插件未随 C++ 变更更新
- File: `Unity/Assets/PcgPlugin/Plugins/x86_64/PcgCore.dll`，注册证据 `pcg-core/src/elements/topology_parity_elements.cpp:491`
- Trigger: 在仓库主要支持的 Windows Unity Editor/IL2CPP 环境运行本分支新增节点或调用新 ABI。
- Impact: Windows 运行时加载的 DLL/LIB 不含本分支新增节点和 `pcg_execute_graph_v10`，会得到 unknown node 或入口点缺失；macOS 与 Windows 行为分叉。
- Evidence: `main...HEAD` 仅更新 macOS `.dylib/.a`，未更新 `Plugins/x86_64/PcgCore.dll/.lib`。对 DLL 执行 strings 检查找不到 `PointRelax`、`MergeSpawnPoints`、`CarveSpline`、`pcg_execute_graph_v10`，而提交的 macOS dylib 包含新增节点字符串。
- Fix: 用当前 source 构建并提交 Windows DLL/LIB，核对导出符号、版本和哈希；CI 应比较 registry/ABI 与各平台分发产物。
- Test: Windows clean checkout 加载插件，断言 v10 entry point 可调用，并执行至少一个本分支新增节点图。

### F3 [Major] point binary 改变 scale 布局但仍声明 version 1
- File: `pcg-core/include/pcg_api.h:79`, `pcg-core/src/data/pcg_point_binary.cpp:255`, `Unity/Assets/PcgPlugin/Runtime/PcgResultParser.cs:472`
- Trigger: 新 managed parser 读取旧 native v1 payload，尤其当前分支未更新的 Windows 插件输出带 `Scale` 的 points。
- Impact: 旧 v1 每点写一个 scalar scale，新代码按三个 float 读取；直接解析会报 truncated，缓冲区复用路径还可能把后续/残留字节误读成 scaleY/scaleZ 并错位 rotation。
- Evidence: `main` 的 writer 对 `PCG_POINT_ATTR_SCALE` 写 4 bytes/point；HEAD 在相同 `PCG_POINT_BINARY_VERSION 1` 下写 12 bytes/point，managed `required` 也固定增加 12 bytes/point。解析器读取了 version 字段但未按版本分支。
- Fix: vector scale 升为 point binary v2；保留 v1 scalar parser并扩为 uniform scale；managed 复制长度从 payload header version 推导。
- Test: 用 v1 scalar-scale+rotation fixture 和 v2 XYZ-scale+rotation fixture 分别解析，断言 v1 得到 uniform scale且 rotation offset 正确。

### F4 [Major] Tuanjie 实际 GUID 格式被 32-hex 校验拒绝
- File: `Unity/Assets/PcgPlugin/Runtime/PcgSubgraphAssetTypes.cs:120`, `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphView.cs:1657`
- Trigger: 在当前 Tuanjie 工程中把真实 `.pcgsubgraph` 从 Project 拖进 GraphView，或解析使用其 AssetDatabase GUID 的 consumer。
- Impact: 拖放在 `IsValid` 处静默跳过；已保存引用在 resolver 中报 `Invalid SubgraphAsset.assetGuid`，linked-subgraph 功能无法用于真实资产。
- Evidence: 校验器只接受 32 位 hex；本工程新增 `.meta` 的实际 GUID 为 Tuanjie 长格式，例如 `linked-box.pcgsubgraph.meta:2`。拖放直接将 `AssetDatabase.AssetPathToGUID(path)` 传入该校验。现有测试全部使用手写 32-hex GUID，没有覆盖真实 AssetDatabase。
- Fix: 以 AssetDatabase 返回值为契约，不自行限定 Unity 32-hex；需要稳定 definition ID 时对原始 GUID 做 hash，同时保留原值给 AssetDatabase。
- Test: Editor integration test 创建真实 `.pcgsubgraph`，读取 `AssetPathToGUID`，拖入/序列化/重载/flatten 并成功 cook。

### F5 [Major] linked subgraph 端口快照刷新会让已有 edge 指向已脱离端口
- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgExternalSubgraphNodeView.cs:83`
- Trigger: visible linked subgraph 发生任何 reimport/reconcile，即使接口完全未变化。
- Impact: `SetSnapshot` 清空并重建 input/output port；已有 edge 仍引用旧 port，连线消失或进入不可修复的失配状态。
- Evidence: `RebuildPorts` 在 `134-140` 直接 `inputContainer.Clear()`/`outputContainer.Clear()` 并清字典，没有保留同 handle port，也没有迁移 incident edges。
- Fix: 对相同 handle/type 复用 port；真正变化时在删除前收集 edges，并将兼容连接迁移到新 port。
- Test: 连接 external node，依次 reconcile 相同 snapshot 和新增可选端口 snapshot；断言 edge endpoint 始终等于 node 当前 port。

### F6 [Major] 合法的子图输入 fan-out 因 edge id 重复而无法 flatten
- File: `Unity/Assets/PcgPlugin/Runtime/PcgGraphFlattener.cs:241`
- Trigger: 一个 subgraph input 在内部连接到两个或更多可执行节点。
- Impact: flatten 生成多个 endpoint edge 时复用原始 edge id，随后 `ValidateUniqueIds` 在 `313-321` 报 `Flattened edge id collision`，导入、预览、执行和导出均失败。
- Evidence: 双重循环扩展每个 source/target 组合，但 id 仅为 `prefix + edge.id`，`edgeCounter` 只用于空 id；fan-out 是图模型允许的常规拓扑。
- Fix: 为每个扩展 edge 生成确定且唯一的后缀，或不把非语义 edge id 唯一性当作 endpoint 合法性。
- Test: subgraph input 同时连接两个内部节点，flatten 后断言成功、得到两条唯一 edge 且 endpoint 正确。

### F7 [Major] MergedMesh 模式丢弃除 prototype 0 外的所有原型并泄漏 mesh
- File: `Unity/Assets/PcgPlugin/Runtime/PcgGraphComponent.cs:1419`
- Trigger: PCMS 返回多个 prototype，`scatterDisplayMode` 使用默认 `MergedMesh`，并重复 cook。
- Impact: 所有 points 都用 `prototypes[0].Mesh` 构建，建筑/植被变体和材质丢失；parser 创建的其余 Unity Mesh 未进入 ownership 路径且未 Destroy，Editor preview 重复 cook 会累积 native mesh 内存。
- Evidence: merged 分支在 `1423` 只取第一个 mesh，并在 `1429` 直接返回；按 `PointCount` 分区和 `SetOwnedSpawnPrototypes` 仅存在于 GPU 分支 `1435-1464`。现有测试只验证 PCMS parser 和 GPU command，不覆盖 merged 多原型。
- Fix: merged 分支按每个 prototype 的 `PointCount` 切片并合并对应几何/材质；无论成功或失败都明确销毁临时 prototype mesh。
- Test: 两个 bounds/material 明显不同的 prototype，各一个 point；断言 merged 输出同时包含二者，并在循环 cook/clear 后 mesh 数量不增长。

### F8 [Major] 失败导入会丢失依赖，修复子图后 consumer 不会自动恢复
- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphImporter.cs:44`
- Trigger: linked source 缺失或接口暂时不兼容导致 bake 失败，随后用户修复 source。
- Impact: failed consumer 保存空 dependency list，且没有 `DependsOnSourceAsset`；source 修复后 postprocessor 无法定位并重新导入 consumer，需要手工 reimport。
- Evidence: 依赖只从成功的 `resolveResult` 在 `58-63` 注册；失败分支 `52` 明确写入 `Array.Empty<string>()`。依赖扫描发生在 bake 之后。
- Fix: bake 前收集并注册至少 direct asset GUID/path，失败结果也保留已知依赖。
- Test: 构造 consumer -> broken source，确认导入失败；修复 source 后触发 import，断言 consumer 自动恢复成功。

### F9 [Major] RuntimeRunner 参数重排会重置用户 override
- File: `Unity/Assets/PcgPlugin/Editor/PcgRuntimeRunnerEditor.cs:98`
- Trigger: Graph 插入或重排参数，但原参数 ID 保持不变；随后刷新 runner Inspector。
- Impact: index 上 ID 不匹配时，代码用新参数默认值覆盖该位置所有序列化 override，用户配置静默丢失。
- Evidence: resize 后按 index 对比 `idProp`，`106-121` 在 mismatch 时调用 `FromParameter(param)` 写回全部 value；没有先按 `parameterId` 建立旧值映射。
- Fix: resize 前按 stable ID 收集旧 override，再按新参数顺序重建；仅新 ID 使用 default。
- Test: 自定义 `pB`，参数顺序从 `pA,pB` 改为 `pB,pA`，刷新后 `pB` 仍保留自定义值。

### F10 [Major] 子图资产 Save As 走普通 `.pcg` 序列化路径
- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphEditorWindow.cs:694`
- Trigger: 打开 `.pcgsubgraph` 后执行 Save As；或在 subgraph mode 中 New。
- Impact: Save As 固定导出普通 graph、扩展名 `.pcg`，丢失 inputs/outputs 等子图接口；New 也未复位 mode/panel，状态与文档类型不一致。
- Evidence: `SaveGraph` 在 `664-685` 按 `m_SubgraphAssetMode` 分支；`SaveAsGraph` 在 `694-710` 无分支，固定 `PcgGraphSerializer` 和 `PcgExtension`。`LoadDefaultGraph` 未重置 mode/UI。
- Fix: subgraph mode 使用 `TryExportSubgraphAssetDocument` + `PcgSubgraphAssetSerializer` + `.pcgsubgraph`；New 显式退出 subgraph mode并刷新 panels。
- Test: 对带接口和 nested definitions 的子图 Save As，round-trip 后断言 name、ports、nodes、edges、nested definitions 全部保留。

### F11 [Major] native registry 与 manifest 不一致，仓库验证门禁已失败
- File: `pcg-core/src/elements/topology_parity_elements.cpp:504`, `scripts/validate-manifest.py:106`
- Trigger: 运行 `python3 scripts/validate-manifest.py`，或从 manifest 驱动的 Unity/Web 编辑器创建 `CarveSpline`。
- Impact: native 可执行 `CarveSpline`，编辑器却无法创建/识别；当前验证脚本退出非零。同时 validator 不支持 Unity 已实现的 `visibleWhen.any`，使合法 `SortGeometry` 条件也被误报，CI 无法成为可信门禁。
- Evidence: 实测 validator 输出 `C++ elements: 118`, `Manifest nodes: 117`, 缺 `CarveSpline`，并对 `SortGeometry.*OrderingAttribute` 报 missing property/equals。三份 manifest SHA-256 完全一致，问题不是副本漂移。
- Fix: 明确 `CarveSpline` 是否公开：公开则补 manifest/docs/test，否则移除别名注册；validator 按 Unity parser 支持 `any`/`oneOf` 语法，同时继续拒绝真正 malformed 条件。
- Test: validator exit 0；新增 manifest type set 与 registry set 等价测试，以及 `visibleWhen.any` 正/反例。

### F12 [Major] Graph Parameter 可指向已移入子图的节点并被静默忽略
- File: `Unity/Assets/PcgPlugin/Examples/PCGDemo/stone-arch-bridge/stone-arch-bridge.pcg:579`
- Trigger: 加载 stone-arch-bridge 并修改 `bevelAmount` 参数。
- Impact: 参数仍以 `targetNode: arch_bevel` 指向已位于 `subgraph_1` 内的节点；runtime 只在 root nodes 查找目标，因此参数 UI 看似生效但 cook 结果不变。
- Evidence: `PcgGraphComponent.FindNode` 与 `PcgParameterApplicator` 都只扫描 `doc.nodes`；分支 graph 将 `arch_bevel` 放进 nested definition。该问题也说明新 graph validator 缺少参数绑定完整性检查。
- Fix: 通过子图接口暴露属性，或定义明确的 nested target path 并让所有 applicator 一致解析；现有示例先改成有效 root/interface binding。
- Test: 对 `bevelAmount` 设置两个显著不同值，断言输出几何发生预期变化；对所有 changed graphs 校验 target node/property/type。

### F13 [Major] 子图接口修改没有 Undo/Redo 事务
- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgSubgraphInterfacePanel.cs:72`
- Trigger: rename/retype/add/remove interface port，尤其删除已连接端口。
- Impact: Undo 无法恢复接口和被 reload 丢弃的 edge，属于编辑器数据可恢复性回归。
- Evidence: callbacks 直接修改 `m_Definition` 后调用 `NotifyInterfaceChanged`；没有在修改前 `RecordUndo/WithUndo`。remove 在 mutation 后 rebuild/reload。
- Fix: 每个接口 mutation 在修改前建立一个完整 graph transaction，再执行 reload/commit；drag/text 连续输入需要合并成单个 undo step。
- Test: 对 connected port 分别 add/rename/retype/remove，逐步 Undo/Redo，断言接口和 edges 完整恢复。

### F14 [Major] Blackboard 参数变化不会触发 live cook，range slider 也不进入 Undo
- File: `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphEditorWindow.cs:495`, `Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphBlackboard.cs:442`
- Trigger: 在启用 OnParameterChange cook 时增加/绑定/修改参数，或拖动 ranged default slider。
- Impact: 文档已变化但场景 preview 保持旧结果，直到发生其他 node edit；slider 修改还可能在后续 Undo 中丢失或跳过。
- Evidence: window 对 `OnParametersChanged` 只刷新 Inspector，不调用 `NotifyDocumentChanged`；cook bridge 只监听 `GraphDocumentChanged`。slider callback 直接赋值，只有 field commit 使用 `WithUndo`。
- Fix: 完成每个参数事务后发送一次 document change；slider 使用 begin/end drag 合并成一个 Undo transaction。
- Test: 修改 bound default 后断言只发起一次 cook；拖动多帧后一次 Undo 恢复原值。

### F15 [Minor] PCMS parser 的 Try API 会对恶意/损坏 size 抛异常
- File: `Unity/Assets/PcgPlugin/Runtime/PcgResultParser.cs:409`
- Trigger: PCMS header 中 count/mesh size 为负数、溢出或截断。
- Impact: `new byte[meshSizes[i]]`、`offset + size` 等可抛 `OverflowException`/allocation exception，而不是返回 false；此前已创建的 prototype mesh 也未清理。
- Evidence: count 算术、size 合法性和分配均未使用 checked/negative guard，失败返回前没有统一 dispose。
- Fix: checked 计算 header/offset，拒绝负 count/size 和 trailing inconsistency，finally 清理部分创建的 meshes。
- Test: negative size、huge count、overflow、truncated entry fixtures 均返回 false、不抛异常、无 mesh 泄漏。

### F16 [Minor] 提交了机器相关抓取产物和 Python bytecode
- File: `url-content-out/artstation-kQ3wgn/manifest.json:9`
- Trigger: 在其他机器 clone 后消费 manifest，或运行 `git diff --check`。
- Impact: manifest 内绝对路径指向本机 `/Users/djhuang/...`，不可移植；抓取 HTML 带大量 trailing whitespace，`.pyc` 与运行环境绑定并增加无关 diff。
- Evidence: 分支新增整套 `url-content-out` 和 `scripts/__pycache__/validate-manifest.cpython-313.pyc`；`git diff --check main...HEAD` 对抓取 HTML 产生大量错误。
- Fix: 移除临时抓取/bytecode，或仅保留必要且使用相对路径的发布资产；补 `.gitignore`。
- Test: clean checkout 中 manifest 路径可解析；`git diff --check main...HEAD` 无输出。

## 确认的死代码

- `PcgCreateSplineSceneHandles.cs:112` 的 `s_AvailableGroups`、`s_LastParsedJson`、`GroupJson*`、`GroupInfo`、`ParseGroupsFromJson` 无调用方。
- `PcgRuntimeRunnerEditor.cs:12` 的 `m_Target` 只赋值未读取。
- `PcgGraphBuildValidator.cs:61` 的 `context` 参数未使用。

## 预测风险

- `PcgGraphSerializer` 根据字符串内容推断 JSON 类型；全数字 32 字符 GUID 可能被序列化为 number。当前真实 Tuanjie GUID 已先被 F4 阻断，修复 GUID 支持时应一并改为结构字段强类型序列化。
- `PcgParameterApplicator` 未像 `PcgGraphComponent` 一样跳过 hidden/non-exposed 参数，旧 override 可能继续覆盖已隐藏参数。
- changed `.pcg/.pcgsubgraph` 缺少可执行的仓库级 v3 validator；原 `.cursor/.../validate_pcg.py` 已删除，但文档仍引用旧命令。

## 测试与验证

- `ctest --test-dir <temp-review-build> --output-on-failure`: 40/40 passed。
- `python3 scripts/validate-manifest.py`: failed，缺 `CarveSpline`，并误拒绝 `visibleWhen.any`。
- 三份 `node-manifest.json`: SHA-256 一致。
- changed graph JSON: 均可做基础 JSON parse；语义绑定问题见 F4/F6/F12。
- Unity linked-subgraph tests: 6/6 passed，但均使用手写 32-hex GUID，未覆盖 AssetDatabase/Tuanjie GUID、fan-out、端口 reconcile、Undo 或并发保存。
- Unity Editor suite 的既有执行结果为 35/37；两个失败是 height-field 场景测试。本次未把这两项归因于对应变更，因为尚未形成充分因果证据。
- `git diff --check main...HEAD`: failed，主要来自提交的抓取 HTML trailing whitespace。
- Web build 未验证：当前 `web/pcg-editor` 环境缺少 `tsc`。

## 结论

`blocked`。F1 存在可达的数据覆盖风险；F2-F14 包含平台插件不可用、ABI 兼容破坏、linked-subgraph 主路径失效、编辑器连接/Undo 数据丢失和多 prototype 错误。修复后应以本报告 head SHA 为基线做增量 re-review，并优先补真实 Tuanjie AssetDatabase、跨窗口保存、v1/v2 point fixture、fan-out flatten 和 Windows clean-plugin 测试。
