using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Serializes linked <c>.pcgsubgraph</c> assets (schema 1.0 / 2.0).
    /// </summary>
    public static class PcgSubgraphAssetSerializer
    {
        public static string ToJson(PcgSubgraphAssetDocument doc, bool pretty = true)
        {
            if (doc == null)
                throw new ArgumentNullException(nameof(doc));
            NormalizeInterface(doc);

            doc.version = doc.parameters != null && doc.parameters.Count > 0
                ? PcgSubgraphAssetMigration.Version20
                : doc.version ?? PcgSubgraphAssetMigration.Version10;
            if (doc.version == PcgSubgraphAssetMigration.Version20)
                doc.contentHash = PcgSubgraphAssetContentHash.Compute(doc);

            var indent = pretty ? "  " : "";
            var nl = pretty ? "\n" : "";
            var sb = new StringBuilder(512);
            sb.Append('{').Append(nl);
            sb.Append(indent).Append("\"version\": ").Append(JsonString(doc.version ?? PcgSubgraphAssetMigration.Version10)).Append(',').Append(nl);
            sb.Append(indent).Append("\"name\": ").Append(JsonString(doc.name ?? "")).Append(',').Append(nl);
            if (!string.IsNullOrEmpty(doc.contentHash))
            {
                sb.Append(indent).Append("\"contentHash\": ").Append(JsonString(doc.contentHash)).Append(',').Append(nl);
            }
            AppendPorts(sb, "inputs", doc.inputs, pretty, indent);
            sb.Append(',').Append(nl);
            AppendPorts(sb, "outputs", doc.outputs, pretty, indent);
            sb.Append(',').Append(nl);
            if (doc.version == PcgSubgraphAssetMigration.Version20)
            {
                PcgGraphSerializer.AppendParametersPublic(sb, doc.parameters, pretty, indent);
                sb.Append(',').Append(nl);
            }
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

                if (!PcgSubgraphAssetMigration.TryMigrateRoot(root, out error))
                    return false;

                var version = root.TryGetValue("version", out var versionObj)
                    ? versionObj?.ToString()
                    : PcgSubgraphAssetMigration.Version10;
                if (version != PcgSubgraphAssetMigration.Version10 &&
                    version != PcgSubgraphAssetMigration.Version20)
                {
                    error = $"Unsupported subgraph asset version '{version}'.";
                    return false;
                }

                doc = new PcgSubgraphAssetDocument
                {
                    version = version,
                    name = GetString(root, "name"),
                    contentHash = GetString(root, "contentHash"),
                };
                ParsePorts(root, "inputs", doc.inputs);
                ParsePorts(root, "outputs", doc.outputs);
                if (root.TryGetValue("parameters", out var paramsObj) && paramsObj is List<object> paramsList)
                    PcgGraphSerializer.ParseParametersPublic(paramsList, doc.parameters);
                if (!PcgGraphSerializer.TryParseNodesPublic(root, doc.nodes, allowInterface: true, out error))
                    return false;
                PcgGraphSerializer.ParseEdgesPublic(root, doc.edges);
                if (!PcgGraphSerializer.TryParseSubgraphsPublic(root, doc.subgraphs, allowInterface: true, out error))
                    return false;
                doc.RepairLegacyEmptyInterface();
                NormalizeInterface(doc);
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
                    .Append(", \"pinType\": ").Append(JsonString(string.IsNullOrEmpty(port?.pinType) ? "Any" : port.pinType));
                if (port?.anchorPlaced == true)
                {
                    sb.Append(", \"anchorPlaced\": true")
                        .Append(", \"anchorX\": ").Append(port.anchorX.ToString(CultureInfo.InvariantCulture))
                        .Append(", \"anchorY\": ").Append(port.anchorY.ToString(CultureInfo.InvariantCulture));
                }
                sb.Append('}');
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
                    anchorPlaced = GetBool(port, "anchorPlaced", false),
                    anchorX = GetFloat(port, "anchorX", 0f),
                    anchorY = GetFloat(port, "anchorY", 0f),
                });
            }
        }

        private static void NormalizeInterface(PcgSubgraphAssetDocument doc)
        {
            var definition = new PcgSubgraphDefinition
            {
                id = "__root__",
                name = doc.name,
                inputs = doc.inputs,
                outputs = doc.outputs,
                nodes = doc.nodes,
                edges = doc.edges,
            };
            PcgSubgraphContractUtility.Synchronize(definition);
            doc.inputs = definition.inputs;
            doc.outputs = definition.outputs;
            doc.nodes = definition.nodes;
            doc.edges = definition.edges;
        }

        private static string GetString(Dictionary<string, object> dict, string key, string fallback = "")
        {
            return dict.TryGetValue(key, out var value) ? value?.ToString() ?? fallback : fallback;
        }

        private static bool GetBool(Dictionary<string, object> dict, string key, bool fallback)
        {
            if (!dict.TryGetValue(key, out var value) || value == null)
                return fallback;
            return value is bool boolean
                ? boolean
                : bool.TryParse(value.ToString(), out var parsed) ? parsed : fallback;
        }

        private static float GetFloat(Dictionary<string, object> dict, string key, float fallback)
        {
            if (!dict.TryGetValue(key, out var value) || value == null)
                return fallback;
            try
            {
                return Convert.ToSingle(value, CultureInfo.InvariantCulture);
            }
            catch
            {
                return fallback;
            }
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
