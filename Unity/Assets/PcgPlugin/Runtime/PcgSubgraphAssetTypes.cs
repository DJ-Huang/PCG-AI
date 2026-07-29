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
        public string contentHash = "";
        public List<PcgSubgraphPort> inputs = new();
        public List<PcgSubgraphPort> outputs = new();
        public List<PcgGraphParameter> parameters = new();
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
                parameters = parameters?.Select(PcgGraphParameterUtility.CloneParameter).ToList()
                    ?? new List<PcgGraphParameter>(),
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
                parameters = definition.parameters?.Select(PcgGraphParameterUtility.CloneParameter).ToList()
                    ?? new List<PcgGraphParameter>(),
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
                contentHash = contentHash,
                inputs = inputs.Select(ClonePort).ToList(),
                outputs = outputs.Select(ClonePort).ToList(),
                parameters = parameters?.Select(PcgGraphParameterUtility.CloneParameter).ToList()
                    ?? new List<PcgGraphParameter>(),
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

            repaired |= PcgSubgraphInterfaceRepair.EnsurePassthroughEdges(inputs, outputs, nodes, edges);
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

            PcgSubgraphInterfaceRepair.EnsurePassthroughEdges(inputs, outputs, nodes, edges);
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
                anchorPlaced = port.anchorPlaced,
                anchorX = port.anchorX,
                anchorY = port.anchorY,
            };
        }
    }

    /// <summary>
    /// Restores missing SubgraphInput → SubgraphOutput passthrough edges for interface-only scopes.
    /// </summary>
    public static class PcgSubgraphInterfaceRepair
    {
        public static bool EnsurePassthroughEdges(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return false;
            definition.inputs ??= new List<PcgSubgraphPort>();
            definition.outputs ??= new List<PcgSubgraphPort>();
            definition.nodes ??= new List<PcgGraphNodeRecord>();
            definition.edges ??= new List<PcgGraphEdgeRecord>();
            return EnsurePassthroughEdges(
                definition.inputs,
                definition.outputs,
                definition.nodes,
                definition.edges);
        }

        public static bool EnsurePassthroughEdges(
            List<PcgSubgraphPort> inputs,
            List<PcgSubgraphPort> outputs,
            List<PcgGraphNodeRecord> nodes,
            List<PcgGraphEdgeRecord> edges)
        {
            inputs ??= new List<PcgSubgraphPort>();
            outputs ??= new List<PcgSubgraphPort>();
            nodes ??= new List<PcgGraphNodeRecord>();
            edges ??= new List<PcgGraphEdgeRecord>();

            if (inputs.Count == 0 || outputs.Count == 0)
                return false;

            if (!IsInterfaceOnlyScope(nodes))
                return false;

            const string fallbackInputNodeId = "subgraph_input";
            const string fallbackOutputNodeId = "subgraph_output";

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

            var repaired = false;
            var pairCount = Math.Min(inputs.Count, outputs.Count);
            for (var index = 0; index < pairCount; index++)
            {
                var inputPort = inputs[index];
                var outputPort = outputs[index];
                if (inputPort == null || outputPort == null ||
                    string.IsNullOrEmpty(inputPort.id) ||
                    string.IsNullOrEmpty(outputPort.id))
                {
                    continue;
                }

                var hasPassthrough = edges.Any(edge =>
                    edge != null &&
                    edge.source == inputNodeId &&
                    edge.target == outputNodeId &&
                    edge.sourceHandle == inputPort.id &&
                    edge.targetHandle == outputPort.id);
                if (hasPassthrough)
                    continue;

                edges.Add(new PcgGraphEdgeRecord
                {
                    id = pairCount == 1 ? "iface_passthrough" : $"iface_passthrough_{index + 1}",
                    source = inputNodeId,
                    target = outputNodeId,
                    sourceHandle = inputPort.id,
                    targetHandle = outputPort.id,
                });
                repaired = true;
            }

            return repaired;
        }

        public static void RepairDefinitionsForExecution(IEnumerable<PcgSubgraphDefinition> definitions)
        {
            foreach (var definition in definitions ?? Enumerable.Empty<PcgSubgraphDefinition>())
                EnsurePassthroughEdges(definition);
        }

        private static bool IsInterfaceOnlyScope(List<PcgGraphNodeRecord> nodes)
        {
            if (nodes == null || nodes.Count == 0)
                return true;

            foreach (var node in nodes)
            {
                if (node == null)
                    continue;
                if (node.type != PcgStructuralNodeTypes.SubgraphInput &&
                    node.type != PcgStructuralNodeTypes.SubgraphOutput &&
                    node.type != PcgStructuralNodeTypes.SubgraphParentRef)
                {
                    return false;
                }
            }

            return true;
        }
    }

    /// <summary>
    /// Summary of safe, deterministic authoring repairs applied immediately before save.
    /// </summary>
    public sealed class PcgGraphRepairReport
    {
        public int RemovedOrphanEdges { get; internal set; }
        public List<string> RemovedEdgePaths { get; } = new();
        public bool Changed => RemovedOrphanEdges > 0;
    }

    /// <summary>
    /// Repairs references that cannot be represented by the current graph model.
    /// It never guesses replacement nodes or ports: only provably orphaned edges are removed.
    /// </summary>
    public static class PcgGraphIntegrityRepair
    {
        public static PcgGraphRepairReport RepairForSave(PcgGraphDocument document)
        {
            var report = new PcgGraphRepairReport();
            if (document == null)
                return report;

            document.nodes ??= new List<PcgGraphNodeRecord>();
            document.edges ??= new List<PcgGraphEdgeRecord>();
            document.subgraphs ??= new List<PcgSubgraphDefinition>();
            var definitions = BuildDefinitionMap(document.subgraphs);
            RepairScope(
                document.nodes,
                document.edges,
                null,
                null,
                definitions,
                "<root>",
                report);
            RepairDefinitions(document.subgraphs, definitions, report);
            return report;
        }

        public static PcgGraphRepairReport RepairForSave(PcgSubgraphAssetDocument document)
        {
            var report = new PcgGraphRepairReport();
            if (document == null)
                return report;

            document.inputs ??= new List<PcgSubgraphPort>();
            document.outputs ??= new List<PcgSubgraphPort>();
            document.nodes ??= new List<PcgGraphNodeRecord>();
            document.edges ??= new List<PcgGraphEdgeRecord>();
            document.subgraphs ??= new List<PcgSubgraphDefinition>();
            var definitions = BuildDefinitionMap(document.subgraphs);
            RepairScope(
                document.nodes,
                document.edges,
                document.inputs,
                document.outputs,
                definitions,
                "<asset>",
                report);
            RepairDefinitions(document.subgraphs, definitions, report);
            return report;
        }

        private static Dictionary<string, PcgSubgraphDefinition> BuildDefinitionMap(
            IEnumerable<PcgSubgraphDefinition> definitions)
        {
            var result = new Dictionary<string, PcgSubgraphDefinition>(StringComparer.Ordinal);
            foreach (var definition in definitions ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null || string.IsNullOrEmpty(definition.id))
                    continue;
                result[definition.id] = definition;
            }
            return result;
        }

        private static void RepairDefinitions(
            IEnumerable<PcgSubgraphDefinition> definitions,
            Dictionary<string, PcgSubgraphDefinition> definitionMap,
            PcgGraphRepairReport report)
        {
            foreach (var definition in definitions ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null)
                    continue;
                definition.inputs ??= new List<PcgSubgraphPort>();
                definition.outputs ??= new List<PcgSubgraphPort>();
                definition.nodes ??= new List<PcgGraphNodeRecord>();
                definition.edges ??= new List<PcgGraphEdgeRecord>();
                RepairScope(
                    definition.nodes,
                    definition.edges,
                    definition.inputs,
                    definition.outputs,
                    definitionMap,
                    string.IsNullOrEmpty(definition.id) ? "<subgraph>" : definition.id,
                    report);
            }
        }

        private static void RepairScope(
            List<PcgGraphNodeRecord> nodes,
            List<PcgGraphEdgeRecord> edges,
            List<PcgSubgraphPort> scopeInputs,
            List<PcgSubgraphPort> scopeOutputs,
            Dictionary<string, PcgSubgraphDefinition> definitions,
            string scopePath,
            PcgGraphRepairReport report)
        {
            var nodeById = new Dictionary<string, PcgGraphNodeRecord>(StringComparer.Ordinal);
            foreach (var node in nodes)
            {
                if (node == null || string.IsNullOrEmpty(node.id))
                    continue;
                nodeById[node.id] = node;
            }

            var inputHandles = PortIds(scopeInputs);
            var outputHandles = PortIds(scopeOutputs);
            for (var index = edges.Count - 1; index >= 0; index--)
            {
                var edge = edges[index];
                if (!IsOrphanEdge(
                        edge,
                        nodeById,
                        inputHandles,
                        outputHandles,
                        definitions))
                {
                    continue;
                }

                edges.RemoveAt(index);
                report.RemovedOrphanEdges++;
                report.RemovedEdgePaths.Add(
                    scopePath + "/" + (string.IsNullOrEmpty(edge?.id) ? "<unnamed-edge>" : edge.id));
            }
        }

        private static bool IsOrphanEdge(
            PcgGraphEdgeRecord edge,
            Dictionary<string, PcgGraphNodeRecord> nodeById,
            HashSet<string> scopeInputs,
            HashSet<string> scopeOutputs,
            Dictionary<string, PcgSubgraphDefinition> definitions)
        {
            if (edge == null ||
                string.IsNullOrEmpty(edge.source) ||
                string.IsNullOrEmpty(edge.target) ||
                !nodeById.TryGetValue(edge.source, out var source) ||
                !nodeById.TryGetValue(edge.target, out var target))
            {
                return true;
            }

            if (source.type == PcgStructuralNodeTypes.SubgraphInput &&
                !scopeInputs.Contains(edge.sourceHandle ?? ""))
            {
                return true;
            }

            if (target.type == PcgStructuralNodeTypes.SubgraphOutput &&
                !scopeOutputs.Contains(edge.targetHandle ?? ""))
            {
                return true;
            }

            var sourceOutputs = InstancePorts(source, definitions, input: false);
            if (sourceOutputs != null && !sourceOutputs.Contains(edge.sourceHandle ?? ""))
                return true;

            var targetInputs = InstancePorts(target, definitions, input: true);
            return targetInputs != null && !targetInputs.Contains(edge.targetHandle ?? "");
        }

        private static HashSet<string> InstancePorts(
            PcgGraphNodeRecord node,
            Dictionary<string, PcgSubgraphDefinition> definitions,
            bool input)
        {
            if (node.type == PcgStructuralNodeTypes.SubgraphAsset)
            {
                return PortIds(input
                    ? node.subgraphInterface?.inputs
                    : node.subgraphInterface?.outputs);
            }

            if (node.type != PcgStructuralNodeTypes.Subgraph)
                return null;

            var definitionId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
            if (!definitions.TryGetValue(definitionId, out var definition))
                return null;
            return PortIds(input ? definition.inputs : definition.outputs);
        }

        private static HashSet<string> PortIds(IEnumerable<PcgSubgraphPort> ports)
        {
            return new HashSet<string>(
                (ports ?? Enumerable.Empty<PcgSubgraphPort>())
                    .Where(port => port != null && !string.IsNullOrEmpty(port.id))
                    .Select(port => port.id),
                StringComparer.Ordinal);
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
