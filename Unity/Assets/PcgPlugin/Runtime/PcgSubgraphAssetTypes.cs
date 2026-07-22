using System;
using System.Collections.Generic;
using System.Linq;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// In-memory model for a linked <c>.pcgsubgraph</c> single-definition asset.
    /// </summary>
    [Serializable]
    public class PcgSubgraphAssetDocument
    {
        public string version = "1.0";
        public string name = "";
        public List<PcgSubgraphPort> inputs = new();
        public List<PcgSubgraphPort> outputs = new();
        public List<PcgGraphNodeRecord> nodes = new();
        public List<PcgGraphEdgeRecord> edges = new();
        public List<PcgSubgraphDefinition> subgraphs = new();

        public PcgSubgraphDefinition ToRootDefinition(string definitionId = "__root__")
        {
            return new PcgSubgraphDefinition
            {
                id = definitionId,
                name = name ?? "",
                inputs = inputs.Select(ClonePort).ToList(),
                outputs = outputs.Select(ClonePort).ToList(),
                nodes = nodes.Select(node => node?.Clone()).ToList(),
                edges = edges.Select(edge => edge?.Clone()).ToList(),
            };
        }

        public static PcgSubgraphAssetDocument FromDefinition(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                throw new ArgumentNullException(nameof(definition));

            return new PcgSubgraphAssetDocument
            {
                version = "1.0",
                name = definition.name ?? "",
                inputs = definition.inputs.Select(ClonePort).ToList(),
                outputs = definition.outputs.Select(ClonePort).ToList(),
                nodes = definition.nodes.Select(node => node?.Clone()).ToList(),
                edges = definition.edges.Select(edge => edge?.Clone()).ToList(),
                subgraphs = new List<PcgSubgraphDefinition>(),
            };
        }

        public PcgSubgraphAssetDocument Clone()
        {
            return new PcgSubgraphAssetDocument
            {
                version = version,
                name = name,
                inputs = inputs.Select(ClonePort).ToList(),
                outputs = outputs.Select(ClonePort).ToList(),
                nodes = nodes.Select(node => node?.Clone()).ToList(),
                edges = edges.Select(edge => edge?.Clone()).ToList(),
                subgraphs = subgraphs.Select(definition => definition?.Clone()).ToList(),
            };
        }

        public static PcgSubgraphAssetDocument CreateEmpty(string assetName = "Subgraph Asset")
        {
            // Empty linked asset: only interface nodes, no executable content.
            const string inputNodeId = "subgraph_input";
            const string outputNodeId = "subgraph_output";
            return new PcgSubgraphAssetDocument
            {
                version = "1.0",
                name = assetName,
                inputs = new List<PcgSubgraphPort>(),
                outputs = new List<PcgSubgraphPort>(),
                nodes = new List<PcgGraphNodeRecord>
                {
                    new()
                    {
                        id = inputNodeId,
                        type = PcgStructuralNodeTypes.SubgraphInput,
                        position = new PcgGraphPosition { x = 120f, y = 80f },
                        data = new PcgNodeData(),
                    },
                    new()
                    {
                        id = outputNodeId,
                        type = PcgStructuralNodeTypes.SubgraphOutput,
                        position = new PcgGraphPosition { x = 120f, y = 320f },
                        data = new PcgNodeData(),
                    },
                },
                edges = new List<PcgGraphEdgeRecord>(),
                subgraphs = new List<PcgSubgraphDefinition>(),
            };
        }

        private static PcgSubgraphPort ClonePort(PcgSubgraphPort port)
        {
            if (port == null)
                return null;
            return new PcgSubgraphPort
            {
                id = port.id,
                name = port.name,
                pinType = port.pinType,
            };
        }
    }

    public static class PcgAssetGuidUtility
    {
        /// <summary>
        /// Normalize AssetDatabase GUIDs for stable comparison.
        /// Classic Unity 32-hex GUIDs (optional hyphens) become lowercase hex without hyphens.
        /// Opaque engine GUIDs (e.g. Tuanjie long form) are preserved as AssetDatabase returns them.
        /// </summary>
        public static string Canonicalize(string guid)
        {
            if (string.IsNullOrWhiteSpace(guid))
                return "";
            var trimmed = guid.Trim();
            var noHyphen = trimmed.Replace("-", "");
            if (IsClassicUnityHex(noHyphen))
                return noHyphen.ToLowerInvariant();
            return trimmed;
        }

        public static bool IsValid(string guid)
        {
            var canonical = Canonicalize(guid);
            if (string.IsNullOrEmpty(canonical))
                return false;
            if (IsClassicUnityHex(canonical))
                return true;

            // Opaque AssetDatabase GUID contract: non-empty, no whitespace, bounded length.
            if (canonical.Length < 8 || canonical.Length > 128)
                return false;
            foreach (var c in canonical)
            {
                if (char.IsWhiteSpace(c))
                    return false;
            }
            return true;
        }

        public static string DefinitionIdForGuid(string guid)
        {
            var canonical = Canonicalize(guid);
            if (!IsValid(canonical))
                throw new ArgumentException("assetGuid is empty or invalid.", nameof(guid));
            using var sha = System.Security.Cryptography.SHA256.Create();
            var bytes = System.Text.Encoding.UTF8.GetBytes(canonical);
            var hash = sha.ComputeHash(bytes);
            var hex = BitConverter.ToString(hash, 0, 8).Replace("-", "").ToLowerInvariant();
            return "__ext_" + hex;
        }

        private static bool IsClassicUnityHex(string value)
        {
            if (value == null || value.Length != 32)
                return false;
            foreach (var c in value)
            {
                var isHex = (c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F');
                if (!isHex)
                    return false;
            }
            return true;
        }
    }
}
