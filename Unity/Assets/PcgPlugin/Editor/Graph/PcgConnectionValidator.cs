using System.Collections.Generic;
using System.Linq;
using UnityEditor.Experimental.GraphView;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Connection rules driven by node-manifest pin types (Phase 4.1).
    /// </summary>
    public static class PcgConnectionValidator
    {
        public static bool IsValidEdge(Edge edge, IEnumerable<Edge> existingEdges)
        {
            if (edge?.output?.node is not PcgGraphNodeBase sourceNode)
                return false;
            if (edge.input?.node is not PcgGraphNodeBase targetNode)
                return false;

            var sourceHandle = edge.output.userData as string ?? edge.output.portName;
            var targetHandle = edge.input.userData as string ?? edge.input.portName;

            return IsValidConnection(
                sourceNode.NodeId,
                targetNode.NodeId,
                sourceNode.NodeType,
                targetNode.NodeType,
                sourceHandle,
                targetHandle,
                existingEdges);
        }

        public static bool IsValidConnection(
            string sourceId,
            string targetId,
            string sourceType,
            string targetType,
            string sourceHandle,
            string targetHandle,
            IEnumerable<Edge> existingEdges)
        {
            if (string.IsNullOrEmpty(sourceId) || string.IsNullOrEmpty(targetId))
                return false;
            if (sourceId == targetId)
                return false;

            if (!PcgNodeManifest.CanConnect(sourceType, targetType, sourceHandle, targetHandle))
                return false;

            var duplicateIn = existingEdges.Any(e =>
                e.input?.node is PcgGraphNodeBase target &&
                target.NodeId == targetId &&
                (e.input.userData as string ?? e.input.portName) == targetHandle);

            return !duplicateIn;
        }
    }
}
