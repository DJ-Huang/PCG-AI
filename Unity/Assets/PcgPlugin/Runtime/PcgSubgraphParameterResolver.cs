using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Resolves subgraph-local parameters onto a private definition copy for one instance.
    /// </summary>
    public static class PcgSubgraphParameterResolver
    {
        internal static PcgSubgraphDefinition CreateResolvedInstanceDefinition(
            PcgSubgraphDefinition definition,
            PcgGraphNodeRecord instance)
        {
            var resolved = definition?.Clone();
            if (resolved == null)
                return null;

            ApplyDefinitionParameters(
                resolved,
                PcgSubgraphInstanceParameterStorage.ReadOverrides(instance?.data));
            return resolved;
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
