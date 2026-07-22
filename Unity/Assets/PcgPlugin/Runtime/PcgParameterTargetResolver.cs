using System;
using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Resolves Graph Parameter target nodes in root scope or nested subgraph definitions.
    /// Supports path form <c>instanceId/nestedInstanceId/nodeId</c>.
    /// </summary>
    public static class PcgParameterTargetResolver
    {
        public static PcgGraphNodeRecord FindNode(PcgGraphDocument doc, string targetNode)
        {
            if (doc == null || string.IsNullOrEmpty(targetNode))
                return null;

            if (targetNode.IndexOf('/') >= 0)
                return FindNodeByPath(doc, targetNode);

            var root = FindInNodes(doc.nodes, targetNode);
            if (root != null)
                return root;

            // Bare id fallback: unique match inside nested definitions (legacy graphs).
            PcgGraphNodeRecord found = null;
            foreach (var definition in doc.subgraphs ?? (IEnumerable<PcgSubgraphDefinition>)Array.Empty<PcgSubgraphDefinition>())
            {
                var nested = FindInNodes(definition?.nodes, targetNode);
                if (nested == null)
                    continue;
                if (found != null && !ReferenceEquals(found, nested))
                    return null; // ambiguous
                found = nested;
            }
            return found;
        }

        private static PcgGraphNodeRecord FindNodeByPath(PcgGraphDocument doc, string path)
        {
            var parts = path.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0)
                return null;

            var nodes = doc.nodes;
            var definitions = BuildDefinitionMap(doc);
            for (var i = 0; i < parts.Length; i++)
            {
                var part = parts[i];
                var node = FindInNodes(nodes, part);
                if (node == null)
                    return null;

                var isLast = i == parts.Length - 1;
                if (isLast)
                    return node;

                if (node.type != PcgStructuralNodeTypes.Subgraph)
                    return null;

                var subgraphId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (string.IsNullOrEmpty(subgraphId) || !definitions.TryGetValue(subgraphId, out var definition))
                    return null;
                nodes = definition.nodes;
            }

            return null;
        }

        private static Dictionary<string, PcgSubgraphDefinition> BuildDefinitionMap(PcgGraphDocument doc)
        {
            var map = new Dictionary<string, PcgSubgraphDefinition>(StringComparer.Ordinal);
            foreach (var definition in doc.subgraphs ?? (IEnumerable<PcgSubgraphDefinition>)Array.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null || string.IsNullOrEmpty(definition.id))
                    continue;
                map[definition.id] = definition;
            }
            return map;
        }

        private static PcgGraphNodeRecord FindInNodes(List<PcgGraphNodeRecord> nodes, string nodeId)
        {
            if (nodes == null || string.IsNullOrEmpty(nodeId))
                return null;
            for (var i = 0; i < nodes.Count; i++)
            {
                var node = nodes[i];
                if (node != null && node.id == nodeId)
                    return node;
            }
            return null;
        }
    }
}
