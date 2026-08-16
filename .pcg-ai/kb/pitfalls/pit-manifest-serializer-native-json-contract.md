---
id: "pit-manifest-serializer-native-json-contract"
name: "Manifest、序列化器与 Native JSON 解析类型契约漂移会导致 Editor 崩溃"
severity: high
rootCauseType: "序列化契约漂移 / 异常跨 native 边界"
techStack: ["Unity Editor", "C#", "C++", "nlohmann::json", "Unity Native Plugin"]
affectedPlatforms: ["PCG-AI Tuanjie Editor（macOS，pcg-core native plugin）"]
failedApi: "对跨语言 JSON 参数直接调用 nlohmann::json::value<T>"
alternativeApi: "manifest 支持类型校验 + 容错参数读取（number/bool/string 旧值兼容）"
relatedConcepts: []
relatedNotes: ["[[pit-native-plugin-build-target-mismatch]]", "[[WorkLog/执行记录/PCG Block AI/log-delete-houdini-parity]]"]
tags: ["type/pitfall", "area/serialization", "area/unity-editor", "area/native-plugin", "area/pcg"]
verified_status: limited
verified_by: "PCG-AI Delete preview cook：native string 参数回归测试通过，用户确认选中 Delete 不再 crash（2026-07-27）"
verified_date: "2026-07-27"
common_assumption: "只要 manifest 中写了 float/number，C# serializer 和 native parser 就会自动以相同 JSON 类型处理。"
---

## 问题

Unity 图编辑器的节点参数经过 manifest、C# 数据模型和 JSON serializer 后传入 native plugin。若 manifest 声明了 serializer 不支持的属性类型，数值可能被写成 JSON string；native 再以 `value<double>` 或 `value<int>` 直接读取时会抛 `nlohmann::json::type_error`。异常跨越 C ABI/插件边界后，Editor 可能以 `SIGABRT` 整进程退出，而不是报告普通节点执行错误。

## 根因

置信度：高（PCG-AI 已复现）。

`PcgNodeData` 将内部值保存为 string，而 serializer 只为已知类型（如 `integer`、`number`、`boolean`）输出 JSON 标量。Delete 节点把浮点参数声明为未支持的 `float`，于是字段被写为 string；native `parse_delete_options` 假设类型正确并直接取 number，导致类型异常逃逸。

根因不是 Inspector UI 构建，也不是 dylib 签名：预览 cook 进入 Delete native 执行路径后才崩溃。

## 修复方案

1. 将 manifest 属性类型限制为 serializer 的受支持类型；为 numeric/bool 参数生成标量 JSON。
2. native 参数读取使用无异常的类型安全入口：接受 JSON number/bool，以及可解析的旧式 string 值；缺失、非法、NaN/Inf 或越界时回退节点默认值。
3. 同时修复写入端与读取端：只改 manifest 无法修复旧资产，只加 native 容错会让新资产持续写入错误类型。
4. native plugin 更新后通过项目构建脚本部署共享库、重新签名，并重启 Editor 后验证实际加载版本。

## 验证方法

1. **复现**：构造节点 JSON，使一个 numeric 或 boolean 参数以 string 保存；执行进入 native 参数解析的 preview cook。
2. **改动**：修正 manifest 类型，并在 native 层对旧 string 值做受限解析与默认回退。
3. **通过标准**：
   - 新序列化 JSON 的 numeric/bool 字段为标量，不带引号；
   - 旧 string 参数图执行成功且语义与同值标量一致；
   - 非法字符串不抛 native 异常，按默认值执行；
   - 部署后的 Editor 选中/预览节点不再退出。

## 原始记录

- [[WorkLog/执行记录/PCG Block AI/log-delete-houdini-parity]] — 2026-07-27 Delete preview cook `json.type_error.302` / `SIGABRT` 修复。
