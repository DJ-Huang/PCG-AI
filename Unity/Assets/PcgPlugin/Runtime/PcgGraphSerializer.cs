using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

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
    /// Serializes Graph JSON v1/v2/v3 aligned with the C++ runtime wire contract.
    /// v3 is authoring-only and may include linked <c>SubgraphAsset</c> nodes with interface snapshots.
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
            var version = NormalizeVersion(doc.version, doc);
            var writeInterface = version == "3.0";
            sb.Append(indent).Append("\"version\": ").Append(JsonString(version)).Append(',').Append(nl);
            sb.Append(indent).Append("\"nodes\": [").Append(nl);
            for (var i = 0; i < doc.nodes.Count; i++)
            {
                AppendNode(sb, doc.nodes[i], pretty, indent, writeInterface);
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

            sb.Append(indent).Append(']').Append(',').Append(nl);
            sb.Append(indent).Append("\"subgraphs\": [").Append(nl);
            for (var i = 0; i < doc.subgraphs.Count; i++)
            {
                AppendSubgraph(sb, doc.subgraphs[i], pretty, indent, writeInterface);
                if (i < doc.subgraphs.Count - 1)
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

                var version = root.TryGetValue("version", out var versionObj)
                    ? versionObj?.ToString()
                    : null;
                if (version != "1.0" && version != "2.0" && version != "3.0")
                {
                    error = "Unsupported or missing version (expected 1.0, 2.0, or 3.0).";
                    return false;
                }

                doc = new PcgGraphDocument { version = version };
                var allowInterface = version == "3.0";

                if (!TryParseNodes(root, doc.nodes, allowInterface, out error))
                    return false;

                ParseEdges(root, doc.edges);

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

                if (!TryParseSubgraphs(root, doc.subgraphs, allowInterface, out error))
                    return false;

                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        internal static void AppendNodePublic(StringBuilder sb, PcgGraphNodeRecord node, bool pretty, string indent, bool writeInterface) =>
            AppendNode(sb, node, pretty, indent, writeInterface);

        internal static void AppendEdgePublic(StringBuilder sb, PcgGraphEdgeRecord edge, bool pretty, string indent) =>
            AppendEdge(sb, edge, pretty, indent);

        internal static void AppendSubgraphPublic(StringBuilder sb, PcgSubgraphDefinition subgraph, bool pretty, string indent, bool writeInterface) =>
            AppendSubgraph(sb, subgraph, pretty, indent, writeInterface);

        internal static bool TryParseNodesPublic(Dictionary<string, object> root, List<PcgGraphNodeRecord> output, bool allowInterface, out string error) =>
            TryParseNodes(root, output, allowInterface, out error);

        internal static void ParseEdgesPublic(Dictionary<string, object> root, List<PcgGraphEdgeRecord> output) =>
            ParseEdges(root, output);

        internal static bool TryParseSubgraphsPublic(Dictionary<string, object> root, List<PcgSubgraphDefinition> output, bool allowInterface, out string error) =>
            TryParseSubgraphs(root, output, allowInterface, out error);

        private static string NormalizeVersion(string version, PcgGraphDocument doc)
        {
            if (version == "3.0" || (doc != null && doc.HasExternalSubgraphAssets()))
                return "3.0";
            if (version == "2.0")
                return "2.0";
            return "1.0";
        }

        private static bool TryParseNodes(Dictionary<string, object> root, List<PcgGraphNodeRecord> output, bool allowInterface, out string error)
        {
            error = null;
            if (!root.TryGetValue("nodes", out var nodesObj) || nodesObj is not List<object> nodesList)
                return true;
            foreach (var nodeObj in nodesList)
            {
                if (nodeObj is not Dictionary<string, object> nodeDict)
                    continue;
                if (!TryParseNodeRecord(nodeDict, allowInterface, out var record, out error))
                    return false;
                output.Add(record);
            }
            return true;
        }

        private static bool TryParseSubgraphs(Dictionary<string, object> root, List<PcgSubgraphDefinition> output, bool allowInterface, out string error)
        {
            error = null;
            if (!root.TryGetValue("subgraphs", out var subgraphsObj) || subgraphsObj is not List<object> subgraphsList)
                return true;
            foreach (var subgraphObj in subgraphsList)
            {
                if (subgraphObj is not Dictionary<string, object> subgraphDict)
                    continue;
                var subgraph = new PcgSubgraphDefinition
                {
                    id = GetString(subgraphDict, "id"),
                    name = GetString(subgraphDict, "name"),
                };
                ParsePorts(subgraphDict, "inputs", subgraph.inputs);
                ParsePorts(subgraphDict, "outputs", subgraph.outputs);
                if (!TryParseNodes(subgraphDict, subgraph.nodes, allowInterface, out error))
                    return false;
                ParseEdges(subgraphDict, subgraph.edges);
                output.Add(subgraph);
            }
            return true;
        }

        private static bool TryParseNodeRecord(
            Dictionary<string, object> nodeDict,
            bool allowInterface,
            out PcgGraphNodeRecord record,
            out string error)
        {
            error = null;
            record = new PcgGraphNodeRecord
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

            if (nodeDict.TryGetValue("subgraphInterface", out var interfaceObj))
            {
                if (!allowInterface)
                {
                    error = $"Node '{record.id}' has subgraphInterface but graph version does not allow it.";
                    return false;
                }

                if (interfaceObj is not Dictionary<string, object> interfaceDict)
                {
                    error = $"Node '{record.id}' subgraphInterface must be an object.";
                    return false;
                }

                record.subgraphInterface = ParseInterfaceSnapshot(interfaceDict);
            }

            if (record.type == PcgStructuralNodeTypes.SubgraphAsset)
            {
                if (!allowInterface)
                {
                    error = $"Node '{record.id}' type SubgraphAsset requires graph version 3.0.";
                    return false;
                }

                var guid = record.data.GetRaw("assetGuid")?.ToString() ?? "";
                if (!PcgAssetGuidUtility.IsValid(guid))
                {
                    error = $"Node '{record.id}' SubgraphAsset.assetGuid must be 32 hex characters.";
                    return false;
                }

                if (record.subgraphInterface == null)
                {
                    error = $"Node '{record.id}' SubgraphAsset requires subgraphInterface snapshot.";
                    return false;
                }
            }

            return true;
        }

        private static PcgSubgraphInterfaceSnapshot ParseInterfaceSnapshot(Dictionary<string, object> dict)
        {
            var snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = GetString(dict, "name"),
            };
            ParsePorts(dict, "inputs", snapshot.inputs);
            ParsePorts(dict, "outputs", snapshot.outputs);
            return snapshot;
        }

        private static void ParseEdges(Dictionary<string, object> root, List<PcgGraphEdgeRecord> output)
        {
            if (!root.TryGetValue("edges", out var edgesObj) || edgesObj is not List<object> edgesList)
                return;
            foreach (var edgeObj in edgesList)
            {
                if (edgeObj is not Dictionary<string, object> edgeDict)
                    continue;
                output.Add(new PcgGraphEdgeRecord
                {
                    id = GetString(edgeDict, "id"),
                    source = GetString(edgeDict, "source"),
                    target = GetString(edgeDict, "target"),
                    sourceHandle = GetString(edgeDict, "sourceHandle", "out"),
                    targetHandle = GetString(edgeDict, "targetHandle", "in"),
                    sourcePinType = GetString(edgeDict, "sourcePinType"),
                    targetPinType = GetString(edgeDict, "targetPinType"),
                });
            }
        }

        private static void ParsePorts(Dictionary<string, object> root, string key, List<PcgSubgraphPort> output)
        {
            if (!root.TryGetValue(key, out var portsObj) || portsObj is not List<object> ports)
                return;
            foreach (var portObj in ports)
            {
                if (portObj is not Dictionary<string, object> port)
                    continue;
                output.Add(new PcgSubgraphPort
                {
                    id = GetString(port, "id"),
                    name = GetString(port, "name"),
                    pinType = GetString(port, "pinType", "Any"),
                });
            }
        }

        private static void AppendSubgraph(StringBuilder sb, PcgSubgraphDefinition subgraph, bool pretty, string indent, bool writeInterface)
        {
            var inner = pretty ? indent + "  " : "";
            var deep = pretty ? inner + "  " : "";
            var nl = pretty ? "\n" : "";
            sb.Append(inner).Append('{').Append(nl);
            sb.Append(deep).Append("\"id\": ").Append(JsonString(subgraph.id)).Append(',').Append(nl);
            sb.Append(deep).Append("\"name\": ").Append(JsonString(subgraph.name)).Append(',').Append(nl);
            AppendPorts(sb, "inputs", subgraph.inputs, pretty, deep);
            sb.Append(',').Append(nl);
            AppendPorts(sb, "outputs", subgraph.outputs, pretty, deep);
            sb.Append(',').Append(nl);
            sb.Append(deep).Append("\"nodes\": [").Append(nl);
            for (var i = 0; i < subgraph.nodes.Count; i++)
            {
                AppendNode(sb, subgraph.nodes[i], pretty, deep, writeInterface);
                if (i < subgraph.nodes.Count - 1) sb.Append(',');
                sb.Append(nl);
            }
            sb.Append(deep).Append("],").Append(nl);
            sb.Append(deep).Append("\"edges\": [").Append(nl);
            for (var i = 0; i < subgraph.edges.Count; i++)
            {
                AppendEdge(sb, subgraph.edges[i], pretty, deep);
                if (i < subgraph.edges.Count - 1) sb.Append(',');
                sb.Append(nl);
            }
            sb.Append(deep).Append(']').Append(nl).Append(inner).Append('}');
        }

        private static void AppendPorts(StringBuilder sb, string key, List<PcgSubgraphPort> ports, bool pretty, string indent)
        {
            sb.Append(indent).Append('"').Append(key).Append("\": [");
            for (var i = 0; i < ports.Count; i++)
            {
                if (i > 0) sb.Append(',');
                if (pretty) sb.Append(' ');
                var port = ports[i];
                sb.Append("{\"id\": ").Append(JsonString(port.id))
                    .Append(", \"name\": ").Append(JsonString(port.name))
                    .Append(", \"pinType\": ").Append(JsonString(port.pinType)).Append('}');
            }
            if (pretty && ports.Count > 0) sb.Append(' ');
            sb.Append(']');
        }

        private static void MergeData(string type, PcgNodeData data, Dictionary<string, object> dict)
        {
            foreach (var (key, value) in dict)
                data.SetRaw(key, value);
        }

        private static void AppendNode(StringBuilder sb, PcgGraphNodeRecord node, bool pretty, string indent, bool writeInterface)
        {
            var inner = pretty ? indent + "  " : "";
            var nl = pretty ? "\n" : "";
            sb.Append(inner).Append('{').Append(nl);
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"id\": ").Append(JsonString(node.id)).Append(',').Append(nl);
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"type\": ").Append(JsonString(node.type)).Append(',').Append(nl);
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"position\": { \"x\": ")
                .Append(node.position.x.ToString(CultureInfo.InvariantCulture))
                .Append(", \"y\": ")
                .Append(node.position.y.ToString(CultureInfo.InvariantCulture))
                .Append(" }").Append(',').Append(nl);
            sb.Append(inner).Append(pretty ? "  " : "").Append("\"data\": ");
            AppendData(sb, node.type, node.data);
            if (writeInterface && node.subgraphInterface != null)
            {
                sb.Append(',').Append(nl);
                sb.Append(inner).Append(pretty ? "  " : "").Append("\"subgraphInterface\": ");
                AppendInterfaceSnapshot(sb, node.subgraphInterface, pretty, pretty ? inner + "  " : "");
            }
            sb.Append(nl).Append(inner).Append('}');
        }

        private static void AppendInterfaceSnapshot(StringBuilder sb, PcgSubgraphInterfaceSnapshot snapshot, bool pretty, string indent)
        {
            var nl = pretty ? "\n" : "";
            var inner = pretty ? indent + "  " : "";
            sb.Append('{').Append(nl);
            sb.Append(inner).Append("\"name\": ").Append(JsonString(snapshot.name ?? "")).Append(',').Append(nl);
            AppendPorts(sb, "inputs", snapshot.inputs, pretty, inner);
            sb.Append(',').Append(nl);
            AppendPorts(sb, "outputs", snapshot.outputs, pretty, inner);
            sb.Append(nl).Append(indent).Append('}');
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
            if (!string.IsNullOrEmpty(edge.sourcePinType))
                sb.Append(", \"sourcePinType\": ").Append(JsonString(edge.sourcePinType));
            if (!string.IsNullOrEmpty(edge.targetPinType))
                sb.Append(", \"targetPinType\": ").Append(JsonString(edge.targetPinType));
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
