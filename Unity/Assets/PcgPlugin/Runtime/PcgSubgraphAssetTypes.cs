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
            var definition = new PcgSubgraphDefinition
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
            PcgSubgraphContractUtility.Synchronize(definition);
            return definition;
        }

        public static PcgSubgraphAssetDocument FromDefinition(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                throw new ArgumentNullException(nameof(definition));

            var normalized = definition.Clone();
            return new PcgSubgraphAssetDocument
            {
                version = "1.0",
                name = normalized.name ?? "",
                inputs = normalized.inputs.Select(ClonePort).ToList(),
                outputs = normalized.outputs.Select(ClonePort).ToList(),
                parameters = normalized.parameters?.Select(PcgGraphParameterUtility.CloneParameter).ToList()
                    ?? new List<PcgGraphParameter>(),
                nodes = normalized.nodes.Select(node => node?.Clone()).ToList(),
                edges = normalized.edges.Select(edge => edge?.Clone()).ToList(),
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
            var definition = ToRootDefinition();
            var repaired = PcgSubgraphContractUtility.Synchronize(definition);
            repaired |= PcgSubgraphInterfaceRepair.EnsurePassthroughEdges(definition);
            inputs = definition.inputs;
            outputs = definition.outputs;
            nodes = definition.nodes;
            edges = definition.edges;
            return repaired;
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
    /// Restores a missing SubgraphInput → Output passthrough edge for interface-only scopes.
    /// </summary>
    public static class PcgSubgraphInterfaceRepair
    {
        public static bool EnsurePassthroughEdges(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return false;
            var changed = PcgSubgraphContractUtility.Synchronize(definition);
            return EnsurePassthroughEdges(
                definition.inputs,
                definition.outputs,
                definition.nodes,
                definition.edges) || changed;
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
            var definition = new PcgSubgraphDefinition
            {
                id = "__interface__",
                inputs = inputs,
                outputs = outputs,
                nodes = nodes,
                edges = edges,
            };
            var contractChanged = PcgSubgraphContractUtility.Synchronize(definition);

            if (inputs.Count == 0 || outputs.Count == 0)
                return contractChanged;

            if (!IsInterfaceOnlyScope(nodes))
                return contractChanged;

            const string fallbackInputNodeId = "subgraph_input";
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
                .FirstOrDefault(node => node?.type == "Output")?.id;
            if (string.IsNullOrEmpty(outputNodeId))
                return contractChanged;

            var repaired = false;
            var inputPort = inputs[0];
            if (inputPort == null || string.IsNullOrEmpty(inputPort.id))
                return contractChanged;

            var hasPassthrough = edges.Any(edge =>
                edge != null &&
                edge.source == inputNodeId &&
                edge.target == outputNodeId &&
                edge.sourceHandle == inputPort.id &&
                edge.targetHandle == "in");
            if (!hasPassthrough)
            {
                edges.Add(new PcgGraphEdgeRecord
                {
                    id = "iface_passthrough",
                    source = inputNodeId,
                    target = outputNodeId,
                    sourceHandle = inputPort.id,
                    targetHandle = "in",
                });
                repaired = true;
            }

            return repaired || contractChanged;
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
                    node.type != PcgStructuralNodeTypes.SubgraphParentRef &&
                    node.type != "Output")
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
            var rootDefinition = document.ToRootDefinition();
            document.inputs = rootDefinition.inputs;
            document.outputs = rootDefinition.outputs;
            document.nodes = rootDefinition.nodes;
            document.edges = rootDefinition.edges;
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
                PcgSubgraphContractUtility.Synchronize(definition);
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
