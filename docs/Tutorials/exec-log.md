# 教程执行日志

## 2026-07-15 Phase 0-6 全流程

### 任务契约
- 输出目录：`docs/Tutorials/`
- 目标读者：全栈开发者（熟悉 C++/C#/TS/React，不了解本工程）
- 学习目标：架构 + 核心算法 + 调试验证
- Git 范围：仅当前分支 `dev/07-w3`
- HEAD：`f50d744f44d56836d37e2f559d1e5781d0790a02`
- 源码证据模式：working tree（仅 `.codely-cli/settings.json` 有改动）

### 步骤
- Step 1：读取 README.md, AGENTS.md, CODELY.md，确认任务契约 ✅
- Step 2：扫描 Git 历史（78 commits, Jul 2 → Jul 14），识别 24 个里程碑 ✅
- Step 3：扫描当前仓库结构，识别 10 个核心模块 ✅
- Step 4：精读关键源码 ✅
- Step 5：创建 `.git-scan-record.md` ✅
- Step 6：创建 `.tutorial-evidence.md`（55 条证据 + 2 个待确认）✅
- Step 7：设计学习路径（10 篇文档），用户确认 ✅
- Step 8：逐篇生成教程文档 ✅
  - 00-overview.md ✅
  - 01-graph-json-and-schema.md ✅
  - 02-c-api-layer.md ✅
  - 03-graph-executor.md ✅
  - 04-data-model.md ✅
  - 05-element-system.md ✅
  - 06-geometry-kernel.md ✅
  - 07-unity-runtime.md ✅
  - 08-unity-graph-editor.md ✅
  - 09-end-to-end-debug.md ✅
- Step 9：生成 index.md ✅
- Step 10：全局一致性检查 ✅
  - 术语一致：PcgGeometry/PcgMeshData/PcgDataCollection/IPcgElement 跨文档统一
  - 模块名一致：C API/Graph Executor/Data Model/Element System/Geometry Kernel/Unity Runtime/Unity Editor/Web Editor/Build & CI
  - 文档编号连续：00-09 + index
  - 证据 ID 引用：E-001~E-055 全部在 .tutorial-evidence.md 中定义
  - 源码路径：全部标明 path 和 commit `f50d744`
  - HEAD SHA 一致：所有文档引用 `f50d744`
- Step 11：覆盖检查 ✅
  - 10 个核心模块全部有对应教程
  - 每篇教程有源码证据和验证路径
  - 2 个已知证据缺口（U-001 Loop 细分, U-002 Boolean CSG 完整调用链）已记录
- Step 12：教学质量抽检 ✅
  - 抽检 3 条关键结论：
    1. "gather_inputs 优先级 Points > Geometry > Mesh > JSON" → E-010 → graph_executor.cpp:gather_inputs ✅
    2. "Cook Cache per-node input_hash" → E-011 → graph_executor.cpp cache try_get/put ✅
    3. "Mesh Binary v2 header 20 bytes" → E-019 → pcg_api.h PCG_MESH_BINARY_HEADER_SIZE ✅
- Step 13：更新 .git-scan-record.md 和 exec-log.md ✅

### 未完成步骤
- 无

### 关键文件
- `docs/Tutorials/index.md`：教程目录入口
- `docs/Tutorials/00-overview.md` 到 `09-end-to-end-debug.md`：10 篇教程
- `docs/Tutorials/.git-scan-record.md`：Git 扫描记录
- `docs/Tutorials/.tutorial-evidence.md`：证据台账
- `docs/Tutorials/exec-log.md`：执行日志（本文件）

### 验证结果
- PASS：10 篇文档全部生成，每篇包含学习目标、源码证据、实践闭环、自检题
- PASS：所有真实源码标识符与当前 HEAD `f50d744` 一致
- PASS：55 条证据全部 verified
- PASS：术语、模块名、文档编号、证据 ID、源码路径全局一致
- PASS：3 条关键结论双向抽检通过
- 静态验证：可执行实践因环境未配置 VS 2022 标记为静态验证，阻塞原因已记录

### 用户反馈
- 用户确认学习路径（10 篇）后开始生成
- 用户说"继续"后完成剩余文档
