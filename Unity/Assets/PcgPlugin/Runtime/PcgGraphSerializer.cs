using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Manifest property info for editor-driven serialization.
    /// Set by Editor code via <see cref="PcgGraphSerializer.ManifestLookup"/>.
    /// </summary>
    public sealed class ManifestPropertyInfo
    {
        public string type;
        public object defaultValue;
    }

    /// <summary>
    /// Serializes Graph JSON v1 aligned with schema/graph-schema.json and Web exportGraph.ts.
    /// </summary>
    public static class PcgGraphSerializer
    {
        /// <summary>
        /// Editor-side callback: returns manifest properties for a node type, or null if unknown.
        /// Runtime leaves this null — serialization falls back to type inference.
        /// </summary>
        public static Func<string, Dictionary<string, ManifestPropertyInfo>> ManifestLookup;

        public static string ToJson(PcgGraphDocument doc, bool pretty = true)
        {
            var indent = pretty ? "  " : "";
            var nl = pretty ? "\n" : "";
            var sb = new StringBuilder(512);
            sb.Append('{').Append(nl);
            sb.Append(indent).Append("\"version\": \"1.0\"").Append(',').Append(nl);
            sb.Append(indent).Append("\"nodes\": [").Append(nl);
            for (var i = 0; i < doc.nodes.Count; i++)
            {
                AppendNode(sb, doc.nodes[i], pretty, indent);
                if (i < doc.nodes.Count - 1)
                    sb.Append(',');
                sb.Append(nl);
            }

            sb.Append(indent).Append(']').Append(',').Append(nl);
            sb.Append(indent).Append("\"edges\": [").Append(nl);
            for (var i = 0; i < doc.edges.Count; i++)
            {
                AppendEdge(sb, doc.edges[i], pretty, indent);
                if (i < doc.edges.Count - 1)
                    sb.Append(',');
                sb.Append(nl);
            }

            sb.Append(indent).Append(']').Append(',').Append(nl);

            sb.Append(indent).Append("\"parameters\": [").Append(nl);
            for (var i = 0; i < doc.parameters.Count; i++)
            {
                AppendParameter(sb, doc.parameters[i], pretty, indent);
                if (i < doc.parameters.Count - 1)
                    sb.Append(',');
                sb.Append(nl);
            }

            sb.Append(indent).Append(']').Append(nl);
            sb.Append('}');
            return sb.ToString();
        }

        public static bool TryFromJson(string json, out PcgGraphDocument doc, out string error)
        {
            doc = null;
            error = null;

            if (string.IsNullOrWhiteSpace(json))
            {
                error = "JSON is empty.";
                return false;
            }

            try
            {
                var root = PcgMiniJson.Deserialize(json) as Dictionary<string, object>;
                if (root == null)
                {
                    error = "Root is not a JSON object.";
                    return false;
                }

                if (!root.TryGetValue("version", out var versionObj) || versionObj?.ToString() != "1.0")
                {
                    error = "Unsupported or missing version (expected 1.0).";
                    return false;
                }

                doc = new PcgGraphDocument { version = "1.0" };

                if (root.TryGetValue("nodes", out var nodesObj) && nodesObj is List<object> nodesList)
                {
                    foreach (var nodeObj in nodesList)
                    {
                        if (nodeObj is not Dictionary<string, object> nodeDict)
                            continue;

                        var record = new PcgGraphNodeRecord
                        {
                            id = GetString(nodeDict, "id"),
                            type = GetString(nodeDict, "type"),
                        };

                        if (nodeDict.TryGetValue("position", out var posObj) && posObj is Dictionary<string, object> posDict)
                        {
                            record.position = new PcgGraphPosition
                            {
                                x = GetFloat(posDict, "x"),
                                y = GetFloat(posDict, "y"),
                            };
                        }

                        record.data = PcgNodeData.DefaultForType(record.type);
                        if (nodeDict.TryGetValue("data", out var dataObj) && dataObj is Dictionary<string, object> dataDict)
                            MergeData(record.type, record.data, dataDict);

                        doc.nodes.Add(record);
                    }
                }

                if (root.TryGetValue("edges", out var edgesObj) && edgesObj is List<object> edgesList)
                {
                    foreach (var edgeObj in edgesList)
                    {
                        if (edgeObj is not Dictionary<string, object> edgeDict)
                            continue;

                        doc.edges.Add(new PcgGraphEdgeRecord
                        {
                            id = GetString(edgeDict, "id"),
                            source = GetString(edgeDict, "source"),
                            target = GetString(edgeDict, "target"),
                            sourceHandle = GetString(edgeDict, "sourceHandle", "out"),
                            targetHandle = GetString(edgeDict, "targetHandle", "in"),
                        });
                    }
                }

                if (root.TryGetValue("parameters", out var paramsObj) && paramsObj is List<object> paramsList)
                {
                    foreach (var paramObj in paramsList)
                    {
                        if (paramObj is not Dictionary<string, object> paramDict)
                            continue;

                        doc.parameters.Add(new PcgGraphParameter
                        {
                            id = GetString(paramDict, "id"),
                            name = GetString(paramDict, "name"),
                            type = GetString(paramDict, "type", "number"),
                            defaultValue = GetString(paramDict, "default"),
                            exposed = GetBool(paramDict, "exposed", true),
                            targetNode = GetString(paramDict, "targetNode"),
                            targetProperty = GetString(paramDict, "targetProperty"),
                            hasRange = GetBool(paramDict, "hasRange", false),
                            minValue = GetFloat(paramDict, "min", 0f),
                            maxValue = GetFloat(paramDict, "max", 1f),
                        });
                    }
                }

                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        private static void MergeData(string type, PcgNodeData data, Dictionary<string, object> dict)
        {
            foreach (var (key, value) in dict)
                data.SetRaw(key, value);
        }

        private static void AppendNode(StringBuilder sb, PcgGraphNodeRecord node, bool pretty, string indent)
        {
            var inner = pretty ? indent + "  " : "";
            sb.Append(inner).Append('{').Append(pretty ? "\n" : "");
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"id\": ").Append(JsonString(node.id)).Append(',').Append(pretty ? "\n" : "");
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"type\": ").Append(JsonString(node.type)).Append(',').Append(pretty ? "\n" : "");
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"position\": { \"x\": ")
                .Append(node.position.x.ToString(CultureInfo.InvariantCulture))
                .Append(", \"y\": ")
                .Append(node.position.y.ToString(CultureInfo.InvariantCulture))
                .Append(" }").Append(',').Append(pretty ? "\n" : "");
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"data\": ");
            AppendData(sb, node.type, node.data);
            sb.Append(pretty ? "\n" : "").Append(inner).Append('}');
        }

        private static void AppendData(StringBuilder sb, string type, PcgNodeData data)
        {
            sb.Append('{');

            var manifestProps = ManifestLookup?.Invoke(type);
            if (manifestProps != null)
            {
                var first = true;
                foreach (var (key, propInfo) in manifestProps)
                {
                    if (!first) sb.Append(", ");
                    first = false;
                    var value = data.GetRaw(key) ?? propInfo.defaultValue;
                    AppendJsonProperty(sb, key, value, propInfo.type);
                }

                // Append non-manifest keys (e.g., __nodeTitle) that are stored
                // in PcgNodeData but not declared in the manifest.
                var manifestKeys = new HashSet<string>(manifestProps.Keys);
                foreach (var (key, value) in data.EnumerateRaw())
                {
                    if (manifestKeys.Contains(key))
                        continue;
                    if (!first) sb.Append(", ");
                    first = false;
                    sb.Append('"').Append(key).Append("\": ");
                    AppendInferredValue(sb, value);
                }

                sb.Append('}');
                return;
            }

            // Fallback: serialize raw key-values with type inference
            var firstRaw = true;
            foreach (var (key, value) in data.EnumerateRaw())
            {
                if (!firstRaw) sb.Append(", ");
                firstRaw = false;
                sb.Append('"').Append(key).Append("\": ");
                AppendInferredValue(sb, value);
            }

            sb.Append('}');
        }

        private static void AppendInferredValue(StringBuilder sb, object value)
        {
            var str = value?.ToString() ?? "";
            if (int.TryParse(str, NumberStyles.Integer, CultureInfo.InvariantCulture, out _))
                sb.Append(str);
            else if (float.TryParse(str, NumberStyles.Float, CultureInfo.InvariantCulture, out _))
                sb.Append(str);
            else if (str == "true" || str == "false")
                sb.Append(str);
            else
                sb.Append(JsonString(str));
        }

        private static void AppendJsonProperty(StringBuilder sb, string key, object value, string type)
        {
            sb.Append('"').Append(key).Append("\": ");
            switch (type)
            {
                case "integer":
                    sb.Append(Convert.ToInt32(value, CultureInfo.InvariantCulture));
                    break;
                case "number":
                    sb.Append(Convert.ToSingle(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture));
                    break;
                case "boolean":
                    sb.Append(Convert.ToBoolean(value) ? "true" : "false");
                    break;
                case "enum":
                    sb.Append(JsonString(value?.ToString() ?? ""));
                    break;
                case "texture2d":
                    sb.Append(JsonString(value?.ToString() ?? ""));
                    break;
                default:
                    sb.Append(JsonString(value?.ToString() ?? ""));
                    break;
            }
        }

        private static void AppendEdge(StringBuilder sb, PcgGraphEdgeRecord edge, bool pretty, string indent)
        {
            var inner = pretty ? indent + "  " : "";
            sb.Append(inner).Append('{');
            sb.Append("\"id\": ").Append(JsonString(edge.id));
            sb.Append(", \"source\": ").Append(JsonString(edge.source));
            sb.Append(", \"target\": ").Append(JsonString(edge.target));
            if (!string.IsNullOrEmpty(edge.sourceHandle))
                sb.Append(", \"sourceHandle\": ").Append(JsonString(edge.sourceHandle));
            if (!string.IsNullOrEmpty(edge.targetHandle))
                sb.Append(", \"targetHandle\": ").Append(JsonString(edge.targetHandle));
            sb.Append('}');
        }

        private static void AppendParameter(StringBuilder sb, PcgGraphParameter param, bool pretty, string indent)
        {
            var inner = pretty ? indent + "  " : "";
            sb.Append(inner).Append('{');
            sb.Append("\"id\": ").Append(JsonString(param.id));
            sb.Append(", \"name\": ").Append(JsonString(param.name));
            sb.Append(", \"type\": ").Append(JsonString(param.type));
            sb.Append(", \"default\": ").Append(InferredJsonValue(param.defaultValue, param.type));
            sb.Append(", \"exposed\": ").Append(param.exposed ? "true" : "false");
            sb.Append(", \"targetNode\": ").Append(JsonString(param.targetNode));
            sb.Append(", \"targetProperty\": ").Append(JsonString(param.targetProperty));
            sb.Append(", \"hasRange\": ").Append(param.hasRange ? "true" : "false");
            sb.Append(", \"min\": ").Append(param.minValue.ToString(CultureInfo.InvariantCulture));
            sb.Append(", \"max\": ").Append(param.maxValue.ToString(CultureInfo.InvariantCulture));
            sb.Append('}');
        }

        private static string InferredJsonValue(string str, string type)
        {
            return type switch
            {
                "integer" => str,
                "number" => str,
                "boolean" => str,
                _ => JsonString(str),
            };
        }

        private static string JsonString(string value)
        {
            var sb = new StringBuilder(value.Length + 2);
            sb.Append('"');
            foreach (var c in value)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default: sb.Append(c); break;
                }
            }

            sb.Append('"');
            return sb.ToString();
        }

        private static string GetString(Dictionary<string, object> dict, string key, string fallback = "")
        {
            return dict.TryGetValue(key, out var value) ? value?.ToString() ?? fallback : fallback;
        }

        private static bool GetBool(Dictionary<string, object> dict, string key, bool fallback = false)
        {
            if (!dict.TryGetValue(key, out var value) || value == null)
                return fallback;

            return value switch
            {
                bool b => b,
                string s => string.Equals(s, "true", StringComparison.OrdinalIgnoreCase),
                _ => fallback,
            };
        }

        private static float GetFloat(Dictionary<string, object> dict, string key)
        {
            if (!dict.TryGetValue(key, out var value) || value == null)
                return 0f;

            return Convert.ToSingle(value, CultureInfo.InvariantCulture);
        }

        private static float GetFloat(Dictionary<string, object> dict, string key, float fallback)
        {
            if (!dict.TryGetValue(key, out var value) || value == null)
                return fallback;

            return Convert.ToSingle(value, CultureInfo.InvariantCulture);
        }
    }
}
