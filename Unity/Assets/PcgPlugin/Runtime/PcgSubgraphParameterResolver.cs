using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Applies subgraph-local parameter definitions and per-instance overrides to internal node data
    /// before execution flattening.
    /// </summary>
    public static class PcgSubgraphParameterResolver
    {
        public static void ApplyInstanceOverrides(PcgGraphDocument document)
        {
            if (document?.subgraphs == null || document.subgraphs.Count == 0)
                return;

            var definitions = BuildDefinitionMap(document.subgraphs);
            ApplyScope(document.nodes, definitions);
            foreach (var definition in document.subgraphs)
            {
                if (definition?.nodes == null)
                    continue;
                ApplyScope(definition.nodes, definitions);
            }
        }

        private static Dictionary<string, PcgSubgraphDefinition> BuildDefinitionMap(
            IEnumerable<PcgSubgraphDefinition> definitions)
        {
            var map = new Dictionary<string, PcgSubgraphDefinition>(StringComparer.Ordinal);
            foreach (var definition in definitions ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null || string.IsNullOrEmpty(definition.id))
                    continue;
                map[definition.id] = definition;
            }

            return map;
        }

        private static void ApplyScope(
            List<PcgGraphNodeRecord> nodes,
            Dictionary<string, PcgSubgraphDefinition> definitions)
        {
            if (nodes == null)
                return;

            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;

                var definitionId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (string.IsNullOrEmpty(definitionId) ||
                    !definitions.TryGetValue(definitionId, out var definition))
                {
                    continue;
                }

                ApplyDefinitionParameters(
                    definition,
                    PcgSubgraphInstanceParameterStorage.ReadOverrides(node.data));
            }
        }

        internal static void ApplyDefinitionParameters(
            PcgSubgraphDefinition definition,
            List<PcgParameterOverride> overrides)
        {
            if (definition?.parameters == null || definition.parameters.Count == 0)
                return;

            var overrideById = new Dictionary<string, PcgParameterOverride>(StringComparer.Ordinal);
            if (overrides != null)
            {
                foreach (var item in overrides)
                {
                    if (item != null && !string.IsNullOrEmpty(item.parameterId))
                        overrideById[item.parameterId] = item;
                }
            }

            foreach (var parameter in definition.parameters)
            {
                if (parameter == null ||
                    string.IsNullOrEmpty(parameter.targetNode) ||
                    string.IsNullOrEmpty(parameter.targetProperty))
                {
                    continue;
                }

                var target = definition.nodes?.FirstOrDefault(node =>
                    node != null && node.id == parameter.targetNode);
                if (target == null)
                    continue;

                var value = overrideById.TryGetValue(parameter.id, out var overrideValue)
                    ? overrideValue.GetValue()
                    : ParseDefault(parameter);
                target.data ??= new PcgNodeData();
                target.data.SetRaw(parameter.targetProperty, value);
            }
        }

        private static object ParseDefault(PcgGraphParameter parameter)
        {
            if (parameter == null)
                return null;

            return parameter.type switch
            {
                "integer" => int.TryParse(parameter.defaultValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var intValue)
                    ? intValue
                    : 0,
                "number" => float.TryParse(parameter.defaultValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var floatValue)
                    ? floatValue
                    : 0f,
                "boolean" => bool.TryParse(parameter.defaultValue, out var boolValue) && boolValue,
                _ => parameter.defaultValue ?? "",
            };
        }
    }
}
