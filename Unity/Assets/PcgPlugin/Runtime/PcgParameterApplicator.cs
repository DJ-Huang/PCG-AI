using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Applies parameter overrides to a graph JSON before execution.
    /// Modifies node data values based on parameter target bindings.
    /// </summary>
    public static class PcgParameterApplicator
    {
        /// <summary>
        /// Deserializes graph JSON, applies override values to bound node properties,
        /// then re-serializes. Returns the modified JSON string.
        /// </summary>
        public static string ApplyOverrides(string json, List<PcgParameterOverride> overrides)
        {
            if (string.IsNullOrEmpty(json) || overrides == null || overrides.Count == 0)
                return json;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                return json;

            // Build override lookup by parameterId
            var byId = new Dictionary<string, PcgParameterOverride>();
            foreach (var o in overrides)
            {
                if (!string.IsNullOrEmpty(o.parameterId))
                    byId[o.parameterId] = o;
            }

            // Apply parameter definitions first to get target bindings
            foreach (var param in doc.parameters)
            {
                if (!byId.TryGetValue(param.id, out var overrideVal))
                    continue;

                var targetNode = FindNode(doc, param.targetNode);
                if (targetNode == null)
                    continue;

                targetNode.data.SetRaw(param.targetProperty, overrideVal.GetValue());
            }

            return PcgGraphSerializer.ToJson(doc, pretty: false);
        }

        private static PcgGraphNodeRecord FindNode(PcgGraphDocument doc, string nodeId)
        {
            if (string.IsNullOrEmpty(nodeId))
                return null;

            foreach (var node in doc.nodes)
            {
                if (node.id == nodeId)
                    return node;
            }

            return null;
        }
    }
}
