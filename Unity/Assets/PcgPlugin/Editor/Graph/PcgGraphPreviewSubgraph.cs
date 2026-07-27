using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Builds an upstream-only cook document for Houdini-style per-node scene preview.
    /// Supports root nodes, Subgraph instances (full definition result), and nodes
    /// inside a Subgraph definition (with parent-instance inputs grafted in).
    /// </summary>
    public static class PcgGraphPreviewSubgraph
    {
        /// Must stay in sync with pcg-core graph_executor kPreviewSinkNodeId.
        /// When present, unmatched ForEachBegin regions cook the first iteration only
        /// (Editor node preview truncates ForEachEnd from the upstream subgraph).
        public const string PreviewSinkNodeId = "__pcg_preview_sink__";
        private const string ExternalIdPrefix = "__pcg_ext__/";

        public static bool TryBuildPreviewCook(
            PcgGraphDocument liveDoc,
            string previewNodeId,
            string scopeSubgraphId,
            IReadOnlyList<string> instanceChainRootToLeaf,
            out PcgGraphDocument cookDoc,
            out string error)
        {
            cookDoc = null;
            error = null;

            if (liveDoc == null)
            {
                error = "Graph document is null.";
                return false;
            }

            if (string.IsNullOrEmpty(previewNodeId))
            {
                error = "Preview node id is empty.";
                return false;
            }

            if (string.IsNullOrEmpty(scopeSubgraphId))
            {
                if (!TryBuildUpstream(liveDoc, previewNodeId, out cookDoc, out error))
                    return false;
                return true;
            }

            var definition = liveDoc.subgraphs?.FirstOrDefault(sg => sg.id == scopeSubgraphId);
            if (definition == null)
            {
                error = $"Preview scope subgraph '{scopeSubgraphId}' was not found.";
                return false;
            }

            var scopeDoc = new PcgGraphDocument
            {
                version = liveDoc.version,
                nodes = CloneNodes(definition.nodes),
                edges = CloneEdges(definition.edges),
                subgraphs = CloneSubgraphs(liveDoc.subgraphs),
                parameters = CloneParametersForNodes(liveDoc.parameters, definition.nodes),
            };

            if (!TryBuildUpstream(scopeDoc, previewNodeId, out cookDoc, out error))
                return false;

            // Graft parent inputs from innermost opened instance out to root.
            if (instanceChainRootToLeaf != null && instanceChainRootToLeaf.Count > 0)
            {
                for (var i = instanceChainRootToLeaf.Count - 1; i >= 0; i--)
                {
                    var instanceId = instanceChainRootToLeaf[i];
                    if (string.IsNullOrEmpty(instanceId))
                        continue;
                    if (!TryGraftInstanceInputs(liveDoc, cookDoc, instanceId, i, out error))
                        return false;
                }
            }

            if (cookDoc.nodes.Any(n => n.type == "SubgraphInput"))
            {
                error =
                    "Preview still depends on SubgraphInput. Enter the subgraph by double-clicking an instance node, then preview again.";
                return false;
            }

            StripInterfaceNodes(cookDoc);
            return true;
        }

        public static bool TryBuildUpstream(
            PcgGraphDocument source,
            string targetNodeId,
            out PcgGraphDocument subgraph,
            out string error)
        {
            subgraph = null;
            error = null;

            if (source == null)
            {
                error = "Graph document is null.";
                return false;
            }

            if (string.IsNullOrEmpty(targetNodeId))
            {
                error = "Preview node id is empty.";
                return false;
            }

            var targetNode = source.nodes.FirstOrDefault(n => n.id == targetNodeId);
            if (targetNode == null)
            {
                error = $"Preview node '{targetNodeId}' was not found in the graph.";
                return false;
            }

            if (targetNode.type == "SubgraphInput")
            {
                error = "SubgraphInput has no geometry to preview.";
                return false;
            }

            // Previewing SubgraphOutput → show whatever feeds into that interface port.
            if (targetNode.type == "SubgraphOutput")
            {
                var incoming = source.edges.FirstOrDefault(e => e.target == targetNodeId);
                if (incoming == null)
                {
                    error = $"SubgraphOutput '{targetNodeId}' has no connected source.";
                    return false;
                }

                return TryBuildUpstream(source, incoming.source, out subgraph, out error);
            }

            var included = new HashSet<string> { targetNodeId };
            var queue = new Queue<string>();
            queue.Enqueue(targetNodeId);

            while (queue.Count > 0)
            {
                var nodeId = queue.Dequeue();
                foreach (var edge in source.edges)
                {
                    if (edge.target != nodeId || included.Contains(edge.source))
                        continue;

                    included.Add(edge.source);
                    queue.Enqueue(edge.source);
                }
            }

            subgraph = new PcgGraphDocument { version = source.version };
            foreach (var node in source.nodes)
            {
                if (!included.Contains(node.id))
                    continue;

                subgraph.nodes.Add(node.Clone());
            }

            foreach (var edge in source.edges)
            {
                if (!included.Contains(edge.source) || !included.Contains(edge.target))
                    continue;

                subgraph.edges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id,
                    source = edge.source,
                    target = edge.target,
                    sourceHandle = edge.sourceHandle,
                    targetHandle = edge.targetHandle,
                });
            }

            if (source.parameters != null)
            {
                foreach (var param in source.parameters)
                {
                    if (!string.IsNullOrEmpty(param.targetNode) && included.Contains(param.targetNode))
                        subgraph.parameters.Add(CloneParameter(param));
                }
            }

            // Keep definitions so Subgraph instances in the upstream set can flatten.
            subgraph.subgraphs = CloneSubgraphs(source.subgraphs);

            if (targetNode.type != "Output")
            {
                var sourceHandle = ResolvePreviewSourceHandle(source, targetNode);
                subgraph.nodes.Add(new PcgGraphNodeRecord
                {
                    id = PreviewSinkNodeId,
                    type = "Output",
                    position = targetNode.position,
                    data = new PcgNodeData(),
                });
                subgraph.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"{PreviewSinkNodeId}_edge",
                    source = targetNodeId,
                    target = PreviewSinkNodeId,
                    sourceHandle = sourceHandle,
                    targetHandle = "in",
                });
            }

            return true;
        }

        private static string ResolvePreviewSourceHandle(PcgGraphDocument source, PcgGraphNodeRecord targetNode)
        {
            var downstream = source.edges.FirstOrDefault(e => e.source == targetNode.id);
            if (downstream != null && !string.IsNullOrEmpty(downstream.sourceHandle))
                return downstream.sourceHandle;

            if (targetNode.type == "Subgraph")
            {
                var subgraphId = targetNode.data?.GetRaw("subgraphId")?.ToString() ?? "";
                var definition = source.subgraphs?.FirstOrDefault(sg => sg.id == subgraphId);
                if (definition?.outputs != null && definition.outputs.Count > 0 &&
                    !string.IsNullOrEmpty(definition.outputs[0].id))
                {
                    return definition.outputs[0].id;
                }
            }

            if (targetNode.type == PcgStructuralNodeTypes.SubgraphAsset)
            {
                var outputs = targetNode.subgraphInterface?.outputs;
                if (outputs != null && outputs.Count > 0 && !string.IsNullOrEmpty(outputs[0].id))
                    return outputs[0].id;
            }

            return "out";
        }

        private static bool TryGraftInstanceInputs(
            PcgGraphDocument liveDoc,
            PcgGraphDocument cookDoc,
            string instanceId,
            int chainIndex,
            out string error)
        {
            error = null;
            if (!TryGetParentScope(liveDoc, instanceId, out var parentNodes, out var parentEdges))
            {
                error = $"Could not find parent scope for subgraph instance '{instanceId}'.";
                return false;
            }

            var inputNodes = cookDoc.nodes
                .Where(n => n.type == "SubgraphInput")
                .Select(n => n.id)
                .ToHashSet();
            if (inputNodes.Count == 0)
                return true;

            var incoming = parentEdges.Where(e => e.target == instanceId).ToList();
            if (incoming.Count == 0)
                return true;

            var included = new HashSet<string>();
            var queue = new Queue<string>();
            foreach (var edge in incoming)
            {
                if (included.Add(edge.source))
                    queue.Enqueue(edge.source);
            }

            while (queue.Count > 0)
            {
                var nodeId = queue.Dequeue();
                foreach (var edge in parentEdges)
                {
                    if (edge.target != nodeId || included.Contains(edge.source))
                        continue;
                    included.Add(edge.source);
                    queue.Enqueue(edge.source);
                }
            }

            var prefix = $"{ExternalIdPrefix}{chainIndex}/";
            string MapId(string id) => prefix + id;

            foreach (var node in parentNodes)
            {
                if (!included.Contains(node.id))
                    continue;
                if (cookDoc.nodes.Any(n => n.id == MapId(node.id)))
                    continue;

                cookDoc.nodes.Add(new PcgGraphNodeRecord
                {
                    id = MapId(node.id),
                    type = node.type,
                    position = node.position,
                    data = node.data?.Clone() ?? new PcgNodeData(),
                    subgraphInterface = node.subgraphInterface?.Clone(),
                });
            }

            foreach (var edge in parentEdges)
            {
                if (!included.Contains(edge.source) || !included.Contains(edge.target))
                    continue;

                cookDoc.edges.Add(new PcgGraphEdgeRecord
                {
                    id = MapId(edge.id ?? $"{edge.source}_{edge.target}"),
                    source = MapId(edge.source),
                    target = MapId(edge.target),
                    sourceHandle = edge.sourceHandle,
                    targetHandle = edge.targetHandle,
                });
            }

            foreach (var edge in incoming)
            {
                var portId = string.IsNullOrEmpty(edge.targetHandle) ? "in" : edge.targetHandle;
                var internalTargets = cookDoc.edges
                    .Where(e => inputNodes.Contains(e.source) &&
                                (e.sourceHandle == portId || string.IsNullOrEmpty(e.sourceHandle)))
                    .ToList();

                foreach (var targetEdge in internalTargets)
                {
                    cookDoc.edges.Add(new PcgGraphEdgeRecord
                    {
                        id = $"{PreviewSinkNodeId}_graft_{chainIndex}_{edge.id}_{targetEdge.id}",
                        source = MapId(edge.source),
                        target = targetEdge.target,
                        sourceHandle = edge.sourceHandle,
                        targetHandle = targetEdge.targetHandle,
                    });
                }
            }

            cookDoc.edges.RemoveAll(e => inputNodes.Contains(e.source) || inputNodes.Contains(e.target));
            cookDoc.nodes.RemoveAll(n => inputNodes.Contains(n.id));

            if (liveDoc.parameters != null)
            {
                foreach (var param in liveDoc.parameters)
                {
                    if (string.IsNullOrEmpty(param.targetNode) || !included.Contains(param.targetNode))
                        continue;
                    var mapped = CloneParameter(param);
                    mapped.targetNode = MapId(param.targetNode);
                    cookDoc.parameters.Add(mapped);
                }
            }

            if (cookDoc.subgraphs == null || cookDoc.subgraphs.Count == 0)
                cookDoc.subgraphs = CloneSubgraphs(liveDoc.subgraphs);

            return true;
        }

        private static bool TryGetParentScope(
            PcgGraphDocument liveDoc,
            string instanceId,
            out List<PcgGraphNodeRecord> nodes,
            out List<PcgGraphEdgeRecord> edges)
        {
            nodes = null;
            edges = null;
            if (liveDoc == null || string.IsNullOrEmpty(instanceId))
                return false;

            if (liveDoc.nodes != null && liveDoc.nodes.Any(n => n.id == instanceId))
            {
                nodes = liveDoc.nodes;
                edges = liveDoc.edges;
                return true;
            }

            if (liveDoc.subgraphs == null)
                return false;

            foreach (var definition in liveDoc.subgraphs)
            {
                if (definition.nodes != null && definition.nodes.Any(n => n.id == instanceId))
                {
                    nodes = definition.nodes;
                    edges = definition.edges;
                    return true;
                }
            }

            return false;
        }

        private static void StripInterfaceNodes(PcgGraphDocument cookDoc)
        {
            if (cookDoc?.nodes == null)
                return;
            var interfaceIds = cookDoc.nodes
                .Where(n => n.type == "SubgraphInput" || n.type == "SubgraphOutput")
                .Select(n => n.id)
                .ToHashSet();
            if (interfaceIds.Count == 0)
                return;
            cookDoc.edges?.RemoveAll(e => interfaceIds.Contains(e.source) || interfaceIds.Contains(e.target));
            cookDoc.nodes.RemoveAll(n => interfaceIds.Contains(n.id));
        }

        private static List<PcgSubgraphDefinition> CloneSubgraphs(List<PcgSubgraphDefinition> source)
        {
            var list = new List<PcgSubgraphDefinition>();
            if (source == null)
                return list;

            foreach (var sg in source)
            {
                list.Add(new PcgSubgraphDefinition
                {
                    id = sg.id,
                    name = sg.name,
                    inputs = ClonePorts(sg.inputs),
                    outputs = ClonePorts(sg.outputs),
                    nodes = CloneNodes(sg.nodes),
                    edges = CloneEdges(sg.edges),
                });
            }

            return list;
        }

        private static List<PcgSubgraphPort> ClonePorts(List<PcgSubgraphPort> source)
        {
            var list = new List<PcgSubgraphPort>();
            if (source == null)
                return list;
            foreach (var port in source)
            {
                list.Add(new PcgSubgraphPort
                {
                    id = port.id,
                    name = port.name,
                    pinType = port.pinType,
                });
            }

            return list;
        }

        private static List<PcgGraphNodeRecord> CloneNodes(List<PcgGraphNodeRecord> source)
        {
            var list = new List<PcgGraphNodeRecord>();
            if (source == null)
                return list;
            foreach (var node in source)
            {
                if (node == null)
                    continue;
                list.Add(node.Clone());
            }

            return list;
        }

        private static List<PcgGraphEdgeRecord> CloneEdges(List<PcgGraphEdgeRecord> source)
        {
            var list = new List<PcgGraphEdgeRecord>();
            if (source == null)
                return list;
            foreach (var edge in source)
            {
                list.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id,
                    source = edge.source,
                    target = edge.target,
                    sourceHandle = edge.sourceHandle,
                    targetHandle = edge.targetHandle,
                });
            }

            return list;
        }

        private static List<PcgGraphParameter> CloneParametersForNodes(
            List<PcgGraphParameter> source,
            List<PcgGraphNodeRecord> nodes)
        {
            var list = new List<PcgGraphParameter>();
            if (source == null || nodes == null)
                return list;
            var ids = new HashSet<string>(nodes.Select(n => n.id));
            foreach (var param in source)
            {
                if (!string.IsNullOrEmpty(param.targetNode) && ids.Contains(param.targetNode))
                    list.Add(CloneParameter(param));
            }

            return list;
        }

        private static PcgGraphParameter CloneParameter(PcgGraphParameter param)
        {
            return new PcgGraphParameter
            {
                id = param.id,
                name = param.name,
                type = param.type,
                defaultValue = param.defaultValue,
                exposed = param.exposed,
                targetNode = param.targetNode,
                targetProperty = param.targetProperty,
                hasRange = param.hasRange,
                minValue = param.minValue,
                maxValue = param.maxValue,
            };
        }
    }
}
