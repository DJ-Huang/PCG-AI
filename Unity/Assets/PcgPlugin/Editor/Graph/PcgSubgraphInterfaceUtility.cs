using System;
using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Shared helpers for subgraph interface ports, SubgraphInput/Output nodes, and promote-to-input.
    /// </summary>
    public static class PcgSubgraphInterfaceUtility
    {
        public static void EnsureInterfaceNodes(
            PcgSubgraphDefinition definition,
            float centerX = 0f,
            float minY = 0f,
            float maxY = 0f)
        {
            if (definition == null)
                return;

            definition.nodes ??= new List<PcgGraphNodeRecord>();

            if (definition.inputs.Count > 0 &&
                !definition.nodes.Any(n => n.type == PcgStructuralNodeTypes.SubgraphInput))
            {
                definition.nodes.Add(new PcgGraphNodeRecord
                {
                    id = PcgGraphNodeFactory.NextNodeId(),
                    type = PcgStructuralNodeTypes.SubgraphInput,
                    position = PcgGraphPosition.FromVector2(new Vector2(centerX, minY - 160f)),
                    data = new PcgNodeData(),
                });
            }

            if (definition.outputs.Count > 0 &&
                !definition.nodes.Any(n => n.type == PcgStructuralNodeTypes.SubgraphOutput))
            {
                definition.nodes.Add(new PcgGraphNodeRecord
                {
                    id = PcgGraphNodeFactory.NextNodeId(),
                    type = PcgStructuralNodeTypes.SubgraphOutput,
                    position = PcgGraphPosition.FromVector2(new Vector2(centerX, maxY + 160f)),
                    data = new PcgNodeData(),
                });
            }
        }

        public static string NextInputPortId(PcgSubgraphDefinition definition)
        {
            var counter = definition.inputs.Count + 1;
            string id;
            do id = $"in_{counter++}";
            while (definition.inputs.Any(p => p.id == id));
            return id;
        }

        public static PcgSubgraphPort AddInputPort(
            PcgSubgraphDefinition definition,
            string portId,
            string pinType,
            string name = null)
        {
            if (definition.inputs.Any(p => p.id == portId))
                return definition.inputs.First(p => p.id == portId);

            var port = new PcgSubgraphPort
            {
                id = portId,
                name = string.IsNullOrEmpty(name) ? portId : name,
                pinType = string.IsNullOrEmpty(pinType) ? "Any" : pinType,
            };
            definition.inputs.Add(port);
            return port;
        }

        public static string GetSubgraphInputNodeId(PcgSubgraphDefinition definition)
        {
            return definition.nodes?
                .FirstOrDefault(n => n.type == PcgStructuralNodeTypes.SubgraphInput)?.id;
        }

        public static void EnsureInternalInputEdge(
            PcgSubgraphDefinition definition,
            string portId,
            string internalTargetId,
            string internalTargetHandle)
        {
            if (string.IsNullOrEmpty(portId) || string.IsNullOrEmpty(internalTargetId))
                return;

            var inputNodeId = GetSubgraphInputNodeId(definition);
            if (string.IsNullOrEmpty(inputNodeId))
                return;

            definition.edges ??= new List<PcgGraphEdgeRecord>();
            var exists = definition.edges.Any(e =>
                e.source == inputNodeId &&
                e.sourceHandle == portId &&
                e.target == internalTargetId &&
                e.targetHandle == internalTargetHandle);
            if (exists)
                return;

            definition.edges.Add(new PcgGraphEdgeRecord
            {
                id = $"e_iface_{portId}_{internalTargetId}",
                source = inputNodeId,
                target = internalTargetId,
                sourceHandle = portId,
                targetHandle = internalTargetHandle,
            });
        }

        public static bool TryPromoteWireToSubgraphInput(
            PcgSubgraphDefinition definition,
            string sourceNodeId,
            string sourceHandle,
            string instanceTargetHandle,
            string pinType,
            out string portId,
            out string error)
        {
            portId = null;
            error = null;
            if (definition == null)
            {
                error = "Subgraph definition is null.";
                return false;
            }

            portId = string.IsNullOrEmpty(instanceTargetHandle)
                ? NextInputPortId(definition)
                : instanceTargetHandle;

            AddInputPort(definition, portId, pinType);

            var minY = definition.nodes.Count > 0
                ? definition.nodes.Min(n => n.position.y)
                : 0f;
            var maxY = definition.nodes.Count > 0
                ? definition.nodes.Max(n => n.position.y)
                : 0f;
            var centerX = definition.nodes.Count > 0
                ? definition.nodes.Average(n => n.position.x)
                : 0f;
            EnsureInterfaceNodes(definition, centerX, minY, maxY);

            var inputNodeId = GetSubgraphInputNodeId(definition);
            if (string.IsNullOrEmpty(inputNodeId))
            {
                error = "Failed to create SubgraphInput node.";
                return false;
            }

            definition.edges ??= new List<PcgGraphEdgeRecord>();
            var resolvedPortId = portId;
            var internalTargets = definition.edges
                .Where(e => e.targetHandle == resolvedPortId && e.source != inputNodeId)
                .Select(e => new KeyValuePair<string, string>(e.target, e.targetHandle))
                .Distinct()
                .ToList();

            foreach (var target in internalTargets)
            {
                EnsureInternalInputEdge(definition, resolvedPortId, target.Key, target.Value);
            }

            return true;
        }

        public static bool TryPromoteWireToAssetDocument(
            PcgSubgraphAssetDocument assetDoc,
            string sourceNodeId,
            string sourceHandle,
            string instanceTargetHandle,
            string pinType,
            out string portId,
            out string error)
        {
            portId = null;
            error = null;
            if (assetDoc == null)
            {
                error = "Subgraph asset document is null.";
                return false;
            }

            var definition = assetDoc.ToRootDefinition("__promote__");
            if (!TryPromoteWireToSubgraphInput(
                    definition,
                    sourceNodeId,
                    sourceHandle,
                    instanceTargetHandle,
                    pinType,
                    out portId,
                    out error))
                return false;

            PcgSubgraphAssetDocument.ApplyDefinitionToAssetDocument(definition, assetDoc);
            return true;
        }
    }
}
