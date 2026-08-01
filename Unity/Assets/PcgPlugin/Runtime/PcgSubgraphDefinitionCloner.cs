using System;
using System.Collections.Generic;
using System.Linq;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Deep-clones an inline <see cref="PcgSubgraphDefinition"/> closure for duplicate/paste.
    /// Shared definitions remain the responsibility of linked <see cref="PcgStructuralNodeTypes.SubgraphAsset"/> nodes.
    /// </summary>
    public static class PcgSubgraphDefinitionCloner
    {
        public sealed class CloneResult
        {
            public string RootDefinitionId;
            public List<PcgSubgraphDefinition> Definitions = new();
        }

        public static bool TryCloneDefinitionClosure(
            string rootDefinitionId,
            IEnumerable<PcgSubgraphDefinition> allDefinitions,
            Func<string> allocateDefinitionId,
            out CloneResult result)
        {
            result = null;
            if (string.IsNullOrEmpty(rootDefinitionId) || allocateDefinitionId == null)
                return false;

            var lookup = BuildLookup(allDefinitions);
            if (!lookup.ContainsKey(rootDefinitionId))
                return false;

            var closure = new List<string>();
            CollectClosure(rootDefinitionId, lookup, new HashSet<string>(StringComparer.Ordinal), closure);

            var idMap = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var oldId in closure)
                idMap[oldId] = allocateDefinitionId();

            result = new CloneResult { RootDefinitionId = idMap[rootDefinitionId] };
            foreach (var oldId in closure)
            {
                var clone = lookup[oldId].Clone();
                clone.id = idMap[oldId];
                RemapSubgraphReferences(clone.nodes, idMap);
                result.Definitions.Add(clone);
            }

            return true;
        }

        private static Dictionary<string, PcgSubgraphDefinition> BuildLookup(
            IEnumerable<PcgSubgraphDefinition> allDefinitions)
        {
            var lookup = new Dictionary<string, PcgSubgraphDefinition>(StringComparer.Ordinal);
            foreach (var definition in allDefinitions ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null || string.IsNullOrEmpty(definition.id))
                    continue;
                lookup[definition.id] = definition;
            }

            return lookup;
        }

        private static void CollectClosure(
            string definitionId,
            Dictionary<string, PcgSubgraphDefinition> lookup,
            HashSet<string> visited,
            List<string> closure)
        {
            if (!lookup.TryGetValue(definitionId, out var definition) || !visited.Add(definitionId))
                return;

            closure.Add(definitionId);
            foreach (var node in definition.nodes ?? Enumerable.Empty<PcgGraphNodeRecord>())
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var nestedId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (!string.IsNullOrEmpty(nestedId))
                    CollectClosure(nestedId, lookup, visited, closure);
            }
        }

        private static void RemapSubgraphReferences(
            List<PcgGraphNodeRecord> nodes,
            Dictionary<string, string> idMap)
        {
            if (nodes == null)
                return;

            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var oldId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (string.IsNullOrEmpty(oldId) || !idMap.TryGetValue(oldId, out var newId))
                    continue;
                node.data ??= new PcgNodeData();
                node.data.SetRaw("subgraphId", newId);
            }
        }
    }
}
