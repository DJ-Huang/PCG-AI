using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Serializes linked <c>.pcgsubgraph</c> assets (version 1.0, no parameters).
    /// </summary>
    public static class PcgSubgraphAssetSerializer
    {
        public static string ToJson(PcgSubgraphAssetDocument doc, bool pretty = true)
        {
            if (doc == null)
                throw new ArgumentNullException(nameof(doc));

            var indent = pretty ? "  " : "";
            var nl = pretty ? "\n" : "";
            var sb = new StringBuilder(512);
            sb.Append('{').Append(nl);
            sb.Append(indent).Append("\"version\": ").Append(JsonString("1.0")).Append(',').Append(nl);
            sb.Append(indent).Append("\"name\": ").Append(JsonString(doc.name ?? "")).Append(',').Append(nl);
            AppendPorts(sb, "inputs", doc.inputs, pretty, indent);
            sb.Append(',').Append(nl);
            AppendPorts(sb, "outputs", doc.outputs, pretty, indent);
            sb.Append(',').Append(nl);
            sb.Append(indent).Append("\"nodes\": [").Append(nl);
            for (var i = 0; i < doc.nodes.Count; i++)
            {
                PcgGraphSerializer.AppendNodePublic(sb, doc.nodes[i], pretty, indent, writeInterface: true);
                if (i < doc.nodes.Count - 1)
                    sb.Append(',');
                sb.Append(nl);
            }
            sb.Append(indent).Append(']').Append(',').Append(nl);
            sb.Append(indent).Append("\"edges\": [").Append(nl);
            for (var i = 0; i < doc.edges.Count; i++)
            {
                PcgGraphSerializer.AppendEdgePublic(sb, doc.edges[i], pretty, indent);
                if (i < doc.edges.Count - 1)
                    sb.Append(',');
                sb.Append(nl);
            }
            sb.Append(indent).Append(']').Append(',').Append(nl);
            sb.Append(indent).Append("\"subgraphs\": [").Append(nl);
            for (var i = 0; i < doc.subgraphs.Count; i++)
            {
                PcgGraphSerializer.AppendSubgraphPublic(sb, doc.subgraphs[i], pretty, indent, writeInterface: true);
                if (i < doc.subgraphs.Count - 1)
                    sb.Append(',');
                sb.Append(nl);
            }
            sb.Append(indent).Append(']').Append(nl);
            sb.Append('}');
            return sb.ToString();
        }

        public static bool TryFromJson(string json, out PcgSubgraphAssetDocument doc, out string error)
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

                if (root.ContainsKey("parameters"))
                {
                    error = ".pcgsubgraph assets must not contain parameters.";
                    return false;
                }

                var version = root.TryGetValue("version", out var versionObj)
                    ? versionObj?.ToString()
                    : null;
                if (version != "1.0")
                {
                    error = "Unsupported or missing subgraph asset version (expected 1.0).";
                    return false;
                }

                doc = new PcgSubgraphAssetDocument
                {
                    version = "1.0",
                    name = GetString(root, "name"),
                };
                ParsePorts(root, "inputs", doc.inputs);
                ParsePorts(root, "outputs", doc.outputs);
                if (!PcgGraphSerializer.TryParseNodesPublic(root, doc.nodes, allowInterface: true, out error))
                    return false;
                PcgGraphSerializer.ParseEdgesPublic(root, doc.edges);
                if (!PcgGraphSerializer.TryParseSubgraphsPublic(root, doc.subgraphs, allowInterface: true, out error))
                    return false;
                doc.RepairLegacyEmptyInterface();
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        private static void AppendPorts(StringBuilder sb, string key, List<PcgSubgraphPort> ports, bool pretty, string indent)
        {
            sb.Append(indent).Append('"').Append(key).Append("\": [");
            for (var i = 0; i < ports.Count; i++)
            {
                if (i > 0)
                    sb.Append(',');
                if (pretty)
                    sb.Append(' ');
                var port = ports[i];
                sb.Append("{\"id\": ").Append(JsonString(port?.id ?? ""))
                    .Append(", \"name\": ").Append(JsonString(port?.name ?? ""))
                    .Append(", \"pinType\": ").Append(JsonString(string.IsNullOrEmpty(port?.pinType) ? "Any" : port.pinType))
                    .Append('}');
            }
            if (pretty && ports.Count > 0)
                sb.Append(' ');
            sb.Append(']');
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

        private static string GetString(Dictionary<string, object> dict, string key, string fallback = "")
        {
            return dict.TryGetValue(key, out var value) ? value?.ToString() ?? fallback : fallback;
        }

        private static string JsonString(string value)
        {
            var sb = new StringBuilder((value?.Length ?? 0) + 2);
            sb.Append('"');
            if (value != null)
            {
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
            }
            sb.Append('"');
            return sb.ToString();
        }
    }
}
