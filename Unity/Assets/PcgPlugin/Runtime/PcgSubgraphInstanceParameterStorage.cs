using System;
using System.Collections.Generic;
using System.Globalization;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Per-instance promoted-parameter overrides stored on Subgraph / SubgraphAsset node data.
    /// </summary>
    public static class PcgSubgraphInstanceParameterStorage
    {
        public const string OverridesKey = "subgraphParameterOverrides";

        public static List<PcgParameterOverride> ReadOverrides(PcgNodeData data)
        {
            var result = new List<PcgParameterOverride>();
            if (data == null)
                return result;

            var raw = data.GetRaw(OverridesKey)?.ToString();
            if (string.IsNullOrWhiteSpace(raw))
                return result;

            var parsed = PcgMiniJson.Deserialize(raw);
            if (parsed is not List<object> entries)
                return result;

            foreach (var entryObj in entries)
            {
                if (entryObj is not Dictionary<string, object> entry)
                    continue;
                var overrideValue = new PcgParameterOverride
                {
                    parameterId = GetString(entry, "parameterId"),
                    name = GetString(entry, "name"),
                    type = GetString(entry, "type", "number"),
                    stringValue = GetString(entry, "stringValue"),
                };
                if (entry.TryGetValue("floatValue", out var floatObj))
                    float.TryParse(floatObj?.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out overrideValue.floatValue);
                if (entry.TryGetValue("intValue", out var intObj))
                    int.TryParse(intObj?.ToString(), NumberStyles.Integer, CultureInfo.InvariantCulture, out overrideValue.intValue);
                if (entry.TryGetValue("boolValue", out var boolObj))
                    bool.TryParse(boolObj?.ToString(), out overrideValue.boolValue);
                if (!string.IsNullOrEmpty(overrideValue.parameterId))
                    result.Add(overrideValue);
            }

            return result;
        }

        public static void WriteOverrides(PcgNodeData data, IEnumerable<PcgParameterOverride> overrides)
        {
            if (data == null)
                return;

            var list = new List<object>();
            if (overrides != null)
            {
                foreach (var item in overrides)
                {
                    if (item == null || string.IsNullOrEmpty(item.parameterId))
                        continue;
                    list.Add(new Dictionary<string, object>
                    {
                        ["parameterId"] = item.parameterId,
                        ["name"] = item.name ?? "",
                        ["type"] = item.type ?? "number",
                        ["floatValue"] = item.floatValue,
                        ["intValue"] = item.intValue,
                        ["boolValue"] = item.boolValue,
                        ["stringValue"] = item.stringValue ?? "",
                    });
                }
            }

            if (list.Count == 0)
            {
                data.RemoveRaw(OverridesKey);
                return;
            }

            data.SetRaw(OverridesKey, PcgMiniJson.Serialize(list));
        }

        public static PcgParameterOverride ResolveOverride(
            PcgNodeData data,
            PcgGraphParameter parameter)
        {
            if (parameter == null)
                return null;

            foreach (var item in ReadOverrides(data))
            {
                if (string.Equals(item.parameterId, parameter.id, StringComparison.Ordinal))
                    return item;
            }

            return PcgParameterOverride.FromParameter(parameter);
        }

        public static void SetOverrideValue(
            PcgNodeData data,
            PcgGraphParameter parameter,
            object value)
        {
            if (data == null || parameter == null)
                return;

            var overrides = ReadOverrides(data);
            var existing = overrides.Find(item =>
                string.Equals(item.parameterId, parameter.id, StringComparison.Ordinal));
            if (existing == null)
            {
                existing = PcgParameterOverride.FromParameter(parameter);
                overrides.Add(existing);
            }

            switch (parameter.type)
            {
                case "integer":
                    existing.intValue = Convert.ToInt32(value ?? 0, CultureInfo.InvariantCulture);
                    break;
                case "number":
                    existing.floatValue = Convert.ToSingle(value ?? 0f, CultureInfo.InvariantCulture);
                    break;
                case "boolean":
                    existing.boolValue = value is bool boolean
                        ? boolean
                        : bool.TryParse(value?.ToString(), out var parsed) && parsed;
                    break;
                case "vector3":
                    existing.stringValue = PcgVector3Property.NormalizeStored(value);
                    break;
                default:
                    existing.stringValue = value?.ToString() ?? "";
                    break;
            }

            WriteOverrides(data, overrides);
        }

        private static string GetString(Dictionary<string, object> dict, string key, string fallback = "")
        {
            return dict.TryGetValue(key, out var value) ? value?.ToString() ?? fallback : fallback;
        }
    }

    public static class PcgSubgraphAssetInstanceKeys
    {
        public const string PinnedContentHash = "pinnedAssetContentHash";
        public const string ResolvedContentHash = "resolvedAssetContentHash";
        public const string ResolvedSchemaVersion = "resolvedAssetSchemaVersion";
    }
}
