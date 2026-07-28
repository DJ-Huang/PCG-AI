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
            var doc = new PcgSubgraphAssetDocument
            {
                version = "1.0",
                name = assetName,
                subgraphs = new List<PcgSubgraphDefinition>(),
            };
            doc.ApplyDefaultPassthroughInterface();
            return doc;
        }

        /// <summary>
        /// Repairs legacy assets where <c>inputs[]</c>/<c>outputs[]</c> were empty but interface
        /// nodes (or edges) exist. Returns true when any field was changed.
        /// </summary>
        public bool RepairLegacyEmptyInterface()
        {
            var repaired = false;
            if (inputs.Count == 0)
                repaired |= TryInferPortsFromInterfaceEdges(Direction.Input);
            if (outputs.Count == 0)
                repaired |= TryInferPortsFromInterfaceEdges(Direction.Output);

            if (inputs.Count == 0 && outputs.Count == 0)
            {
                ApplyDefaultPassthroughInterface();
                return true;
            }

            return repaired;
        }

        private enum Direction
        {
            Input,
            Output,
        }

        private bool TryInferPortsFromInterfaceEdges(Direction direction)
        {
            var interfaceNodeType = direction == Direction.Input
                ? PcgStructuralNodeTypes.SubgraphInput
                : PcgStructuralNodeTypes.SubgraphOutput;
            var interfaceNodeId = nodes?
                .FirstOrDefault(node => node?.type == interfaceNodeType)?.id;
            if (string.IsNullOrEmpty(interfaceNodeId))
                return false;

            var inferred = new List<PcgSubgraphPort>();
            foreach (var edge in edges ?? Enumerable.Empty<PcgGraphEdgeRecord>())
            {
                if (edge == null)
                    continue;
                if (direction == Direction.Input &&
                    edge.source == interfaceNodeId &&
                    !string.IsNullOrEmpty(edge.sourceHandle))
                {
                    inferred.Add(new PcgSubgraphPort
                    {
                        id = edge.sourceHandle,
                        name = edge.sourceHandle,
                        pinType = "Any",
                    });
                }
                else if (direction == Direction.Output &&
                         edge.target == interfaceNodeId &&
                         !string.IsNullOrEmpty(edge.targetHandle))
                {
                    inferred.Add(new PcgSubgraphPort
                    {
                        id = edge.targetHandle,
                        name = edge.targetHandle,
                        pinType = "Any",
                    });
                }
            }

            if (inferred.Count == 0)
                return false;

            var target = direction == Direction.Input ? inputs : outputs;
            var seen = new HashSet<string>(target.Select(port => port.id), StringComparer.Ordinal);
            foreach (var port in inferred)
            {
                if (seen.Contains(port.id))
                    continue;
                target.Add(port);
                seen.Add(port.id);
            }

            return inferred.Count > 0;
        }

        private void ApplyDefaultPassthroughInterface()
        {
            const string defaultInputId = "in_1";
            const string defaultOutputId = "out_1";
            const string fallbackInputNodeId = "subgraph_input";
            const string fallbackOutputNodeId = "subgraph_output";

            nodes ??= new List<PcgGraphNodeRecord>();
            edges ??= new List<PcgGraphEdgeRecord>();

            if (inputs.Count == 0)
            {
                inputs.Add(new PcgSubgraphPort
                {
                    id = defaultInputId,
                    name = "Input",
                    pinType = "Any",
                });
            }

            if (outputs.Count == 0)
            {
                outputs.Add(new PcgSubgraphPort
                {
                    id = defaultOutputId,
                    name = "Output",
                    pinType = "Any",
                });
            }

            var inputNodeId = nodes
                .FirstOrDefault(node => node?.type == PcgStructuralNodeTypes.SubgraphInput)?.id
                ?? fallbackInputNodeId;
            if (!nodes.Any(node => node?.id == inputNodeId))
            {
                nodes.Add(new PcgGraphNodeRecord
                {
                    id = inputNodeId,
                    type = PcgStructuralNodeTypes.SubgraphInput,
                    position = new PcgGraphPosition { x = 120f, y = 80f },
                    data = new PcgNodeData(),
                });
            }

            var outputNodeId = nodes
                .FirstOrDefault(node => node?.type == PcgStructuralNodeTypes.SubgraphOutput)?.id
                ?? fallbackOutputNodeId;
            if (!nodes.Any(node => node?.id == outputNodeId))
            {
                nodes.Add(new PcgGraphNodeRecord
                {
                    id = outputNodeId,
                    type = PcgStructuralNodeTypes.SubgraphOutput,
                    position = new PcgGraphPosition { x = 120f, y = 320f },
                    data = new PcgNodeData(),
                });
            }

            var inputPortId = inputs[0].id;
            var outputPortId = outputs[0].id;
            var hasPassthrough = edges.Any(edge =>
                edge != null &&
                edge.source == inputNodeId &&
                edge.target == outputNodeId &&
                edge.sourceHandle == inputPortId &&
                edge.targetHandle == outputPortId);
            if (!hasPassthrough)
            {
                edges.Add(new PcgGraphEdgeRecord
                {
                    id = "iface_passthrough",
                    source = inputNodeId,
                    target = outputNodeId,
                    sourceHandle = inputPortId,
                    targetHandle = outputPortId,
                });
            }
        }

        public static void ApplyDefinitionToAssetDocument(
            PcgSubgraphDefinition definition,
            PcgSubgraphAssetDocument assetDoc)
        {
            if (definition == null || assetDoc == null)
                return;

            assetDoc.inputs = definition.inputs?.Select(ClonePort).ToList() ?? new List<PcgSubgraphPort>();
            assetDoc.outputs = definition.outputs?.Select(ClonePort).ToList() ?? new List<PcgSubgraphPort>();
            assetDoc.nodes = definition.nodes?.Select(node => node?.Clone()).ToList() ?? new List<PcgGraphNodeRecord>();
            assetDoc.edges = definition.edges?.Select(edge => edge?.Clone()).ToList() ?? new List<PcgGraphEdgeRecord>();
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
