using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Serializes Graph JSON v1 aligned with schema/graph-schema.json and Web exportGraph.ts.
    /// </summary>
    public static class PcgGraphSerializer
    {
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
            if (PcgNodeManifest.TryGet(type, out _))
            {
                foreach (var (key, value) in dict)
                    data.SetRaw(key, value);
                return;
            }

            switch (type)
            {
                case PcgNodeTypes.ParseConfig:
                    if (dict.TryGetValue("seed", out var seed))
                        data.seed = Convert.ToInt32(seed, CultureInfo.InvariantCulture);
                    if (dict.TryGetValue("density", out var density))
                        data.density = Convert.ToSingle(density, CultureInfo.InvariantCulture);
                    break;
                case PcgNodeTypes.SpawnPoints:
                    if (dict.TryGetValue("count", out var count))
                        data.count = Convert.ToInt32(count, CultureInfo.InvariantCulture);
                    if (dict.TryGetValue("radius", out var radius))
                        data.radius = Convert.ToSingle(radius, CultureInfo.InvariantCulture);
                    break;
                case PcgNodeTypes.PlaceInScene:
                    if (dict.TryGetValue("prefab", out var prefab))
                        data.prefab = prefab?.ToString() ?? "";
                    if (dict.TryGetValue("scale", out var scale))
                        data.scale = Convert.ToSingle(scale, CultureInfo.InvariantCulture);
                    break;
            }
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
            if (PcgNodeManifest.TryGet(type, out var def))
            {
                var first = true;
                foreach (var key in def.properties.Keys)
                {
                    if (!first) sb.Append(", ");
                    first = false;
                    var value = data.GetRaw(key) ?? def.properties[key].defaultValue;
                    AppendJsonProperty(sb, key, value, def.properties[key].type);
                }
                sb.Append('}');
                return;
            }

            switch (type)
            {
                case PcgNodeTypes.ParseConfig:
                    sb.Append("\"seed\": ").Append(data.seed)
                        .Append(", \"density\": ").Append(data.density.ToString(CultureInfo.InvariantCulture));
                    break;
                case PcgNodeTypes.SpawnPoints:
                    sb.Append("\"count\": ").Append(data.count)
                        .Append(", \"radius\": ").Append(data.radius.ToString(CultureInfo.InvariantCulture));
                    break;
                case PcgNodeTypes.PlaceInScene:
                    sb.Append("\"prefab\": ").Append(JsonString(data.prefab ?? ""))
                        .Append(", \"scale\": ").Append(data.scale.ToString(CultureInfo.InvariantCulture));
                    break;
            }

            sb.Append('}');
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

        private static float GetFloat(Dictionary<string, object> dict, string key)
        {
            if (!dict.TryGetValue(key, out var value) || value == null)
                return 0f;

            return Convert.ToSingle(value, CultureInfo.InvariantCulture);
        }
    }
}
