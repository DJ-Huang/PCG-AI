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

            if (!ArePinTypesCompatible(sourceNode, targetNode, sourceHandle, targetHandle))
                return false;

            return IsValidConnection(
                sourceNode.NodeId,
                targetNode.NodeId,
                sourceNode.NodeType,
                targetNode.NodeType,
                sourceHandle,
                targetHandle,
                existingEdges,
                skipTypeCheck: true);
        }

        public static bool ArePinTypesCompatible(
            PcgGraphNodeBase source,
            PcgGraphNodeBase target,
            string sourceHandle,
            string targetHandle)
        {
            var sourcePin = source is PcgSubgraphNodeView sourceSubgraph
                ? sourceSubgraph.GetOutputPinType(sourceHandle)
                : source is PcgExternalSubgraphNodeView sourceExternal
                    ? sourceExternal.GetOutputPinType(sourceHandle)
                    : PcgNodeManifest.GetOutputPinType(source.NodeType, sourceHandle);
            var targetPin = target is PcgSubgraphNodeView targetSubgraph
                ? targetSubgraph.GetInputPinType(targetHandle)
                : target is PcgExternalSubgraphNodeView targetExternal
                    ? targetExternal.GetInputPinType(targetHandle)
                    : PcgNodeManifest.GetInputPinType(target.NodeType, targetHandle);
            return sourcePin == "Any" || targetPin == "Any" || sourcePin == targetPin;
        }

        public static bool IsValidConnection(
            string sourceId,
            string targetId,
            string sourceType,
            string targetType,
            string sourceHandle,
            string targetHandle,
            IEnumerable<Edge> existingEdges,
            bool skipTypeCheck = false)
        {
            if (string.IsNullOrEmpty(sourceId) || string.IsNullOrEmpty(targetId))
                return false;
            if (sourceId == targetId)
                return false;

            if (!skipTypeCheck && !PcgNodeManifest.CanConnect(sourceType, targetType, sourceHandle, targetHandle))
                return false;

            var variadic = PcgNodeManifest.TryGet(targetType, out var targetDef) &&
                targetDef.inputs.FirstOrDefault(pin => pin.id == targetHandle)?.variadic == true;
            var duplicateIn = existingEdges.Any(e =>
            {
                if (e.input?.node is not PcgGraphNodeBase target || target.NodeId != targetId ||
                    (e.input.userData as string ?? e.input.portName) != targetHandle)
                    return false;
                if (!variadic)
                    return true;
                return e.output?.node is PcgGraphNodeBase source &&
                    source.NodeId == sourceId &&
                    (e.output.userData as string ?? e.output.portName) == sourceHandle;
            });

            return !duplicateIn;
        }
    }
}
