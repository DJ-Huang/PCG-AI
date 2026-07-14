# Source-Driven 文档验证规则

**触发条件**：涉及 Unity API / Shader 语法 / 渲染管线的代码生成或修改

**加载方式**：非开发任务直接 Read；开发任务中事实核查场景按需 Read 本 workflow 文件

---

## 核心原则

代码中引用的引擎 API 和 Shader 语法必须基于**当前版本**的官方文档验证，而非训练数据中的记忆。过时的 API 调用和废弃的 Shader 语法是隐蔽 bug 的主要来源。

## 四步流程

### DETECT — 检测需要验证的内容

扫描任务描述和代码中的引擎 API 和 Shader 语法关键词：

| 类别 | 关键词示例 |
|------|-----------|
| Unity 渲染 API | `ScriptableRenderPass`、`ScriptableRenderFeature`、`RenderPass`、`RenderTarget`、`RenderingUtils`、`ReAllocateIfNeeded` |
| Shader 语法 | `SAMPLE_TEXTURE2D`、`TEXTURE2D`、`SAMPLER`、`HLSLPROGRAM`、`multi_compile`、`shader_feature` |
| URP 管线 | `UniversalRendererData`、`RenderObjectsPass`、`PostProcessData` |
| 材质/属性 | `MaterialProperty`、`ShaderProperty`、`SerializedProperty` |

### FETCH — 获取官方文档

通过 `web_search` 或 `web_fetch` 获取官方文档：

```
1. web_search("Unity {API名} {版本} documentation")
2. 选择 docs.unity3d.com 的结果
3. web_fetch 获取页面内容
4. 提取 API 签名、参数、返回值、废弃说明
```

### VERIFY — 验证代码

对照文档验证代码，对每个验证点标记状态：

| 标记 | 含义 | 条件 |
|------|------|------|
| `[VERIFIED]` | 已验证正确 | 对照当前官方文档确认 API 用法正确 |
| `[UNVERIFIED]` | 待验证 | 无法获取文档或文档不包含此 API |
| `[DEPRECATED]` | 已废弃 | 官方文档标记为废弃，需替换 |

### CITE — 标注来源

在 plan.md 或代码中标注文档来源：

```markdown
- `SAMPLE_TEXTURE2D` → [VERIFIED] Unity 2022 LTS Shader Reference
- `ScriptableRenderPass.Execute()` → [VERIFIED] Unity 2022 LTS API Reference
- `ReAllocateIfNeeded()` → [UNVERIFIED] 未在官方文档找到，基于代码库使用模式推断
```

## 源权威层级

| 优先级 | 来源 | 信任度 | 说明 |
|--------|------|--------|------|
| 1 | Unity Manual (docs.unity3d.com/Manual) | 🟢 高 | 官方使用指南 |
| 2 | Unity API Reference (docs.unity3d.com/ScriptReference) | 🟢 高 | 官方 API 文档 |
| 3 | Unity Shader Reference (docs.unity3d.com/Manual/SL-*) | 🟢 高 | 官方 Shader 文档 |
| 4 | Unity Blog (blog.unity.com) | 🟡 中 | 官方博客，可能有预览功能 |
| 5 | GitHub Unity-Technologies/Graphics | 🟡 中 | 官方仓库源码 |
| 6 | 社区教程 / 论坛 | 🔴 低 | 需交叉验证 |

## 与 Spec-Driven 工作流的集成

| 阶段 | 集成点 |
|------|--------|
| Phase 2（plan.md） | 涉及引擎 API 的步骤标注 `[SOURCE-VERIFY]`，提醒执行阶段必须验证 |
| Phase 4（执行） | 实现引擎 API 调用前，先 FETCH → VERIFY，确认 API 正确再写代码 |
| 画面验证断点 | Shader 代码必须先通过 source-driven 验证，再进入画面验证 |
