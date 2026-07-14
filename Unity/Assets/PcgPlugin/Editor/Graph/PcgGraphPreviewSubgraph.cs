using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Builds an upstream-only subgraph for Houdini-style per-node scene preview.
    /// The target node is wired into a synthetic Output so the executor always treats
    /// that node as the graph sink (stable geometry_binary / Mesh cook for wire overlay).
    /// </summary>
    public static class PcgGraphPreviewSubgraph
    {
        public const string PreviewSinkNodeId = "__pcg_preview_sink__";

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

                subgraph.nodes.Add(new PcgGraphNodeRecord
                {
                    id = node.id,
                    type = node.type,
                    position = node.position,
                    data = node.data?.Clone() ?? new PcgNodeData(),
                });
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
                        subgraph.parameters.Add(param);
                }
            }

            // Prefer an Output sink so C++ executor does not rely on document-order fallback.
            // When previewing Output itself, the node is already the preferred sink.
            if (targetNode.type != "Output")
            {
                var sourceHandle = "out";
                var downstream = source.edges.FirstOrDefault(e => e.source == targetNodeId);
                if (downstream != null && !string.IsNullOrEmpty(downstream.sourceHandle))
                    sourceHandle = downstream.sourceHandle;

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
    }
}
