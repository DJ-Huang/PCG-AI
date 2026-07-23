using System.Collections.Generic;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Resolves manifest-declared output groups for Inspector / Info / PCG Mode.
    /// Contract: static entries use outputGroups[].name as the group name;
    /// dynamic entries use outputGroups[].name as the property key whose value is the group name
    /// (same as GroupCreate). Legacy manifests that put the default group string in
    /// outputGroups[].name are recovered via isGroupOutput default matching.
    /// Domain: when the node has a <c>domain</c> property (GroupCreate / GroupCombine / …),
    /// use its runtime value — do not trust a static outputGroups[].domain default of "edge".
    /// </summary>
    public static class PcgGroupResolution
    {
        public struct DeclaredOutputGroup
        {
            public string name;
            public string domain;
            public string label;
            public bool isDynamic;
            public string propertyKey;
        }

        public static List<DeclaredOutputGroup> ResolveOutputGroups(
            ManifestNodeDef def, PcgNodeData data)
        {
            var result = new List<DeclaredOutputGroup>();
            if (def == null)
                return result;

            var seen = new HashSet<string>();

            foreach (var og in def.outputGroups)
            {
                if (!PassesCondition(og, data))
                    continue;

                string groupName = og.name;
                string propertyKey = null;
                var isDynamic = og.dynamic;

                if (isDynamic)
                {
                    propertyKey = og.name;
                    var propValue = data?.GetRaw(og.name)?.ToString();
                    if (string.IsNullOrWhiteSpace(propValue))
                    {
                        if (!TryResolveLegacyDynamic(def, data, og.name, out propertyKey, out groupName))
                            continue;
                    }
                    else
                    {
                        groupName = propValue;
                    }
                }

                var domain = ResolveDomain(def, data, og.domain);
                if (!seen.Add($"{groupName}:{domain}"))
                    continue;

                result.Add(new DeclaredOutputGroup
                {
                    name = groupName,
                    domain = domain,
                    label = string.IsNullOrEmpty(og.label) ? groupName : og.label,
                    isDynamic = isDynamic,
                    propertyKey = propertyKey,
                });
            }

            foreach (var (key, prop) in def.properties)
            {
                if (!prop.isGroupOutput)
                    continue;

                var groupName = data?.GetRaw(key)?.ToString();
                if (string.IsNullOrWhiteSpace(groupName))
                    continue;

                var domain = ResolveDomain(def, data, prop.groupDomain);
                if (!seen.Add($"{groupName}:{domain}"))
                    continue;

                result.Add(new DeclaredOutputGroup
                {
                    name = groupName,
                    domain = domain,
                    label = string.IsNullOrEmpty(prop.displayName) ? key : prop.displayName,
                    isDynamic = true,
                    propertyKey = key,
                });
            }

            return result;
        }

        /// <summary>
        /// Prefer the node's runtime <c>domain</c> property (Points/Edges/Primitives)
        /// over a static manifest default — GroupCreate writes groupType into that field.
        /// </summary>
        internal static string ResolveDomain(
            ManifestNodeDef def, PcgNodeData data, string declaredOrPropDomain)
        {
            if (def?.properties != null && def.properties.ContainsKey("domain"))
            {
                var fromData = data?.GetRaw("domain")?.ToString();
                if (IsValidGroupDomain(fromData))
                    return fromData;

                if (def.properties.TryGetValue("domain", out var domainProp))
                {
                    var fromDefault = domainProp.defaultValue?.ToString();
                    if (IsValidGroupDomain(fromDefault))
                        return fromDefault;
                }
            }

            if (IsValidGroupDomain(declaredOrPropDomain))
                return declaredOrPropDomain;

            return "edge";
        }

        private static bool IsValidGroupDomain(string domain) =>
            domain == "point" || domain == "edge" || domain == "face";

        public static string PropertyDisplayLabel(string key, ManifestPropertyDef prop)
        {
            if (!string.IsNullOrEmpty(prop.displayName))
                return prop.displayName;

            if (prop.isGroupOutput)
                return $"{key} (Output)";

            if (prop.type is "groupSelect" or "groupMultiSelect")
                return $"{key} (Input)";

            return key;
        }

        private static bool PassesCondition(ManifestOutputGroupDef og, PcgNodeData data)
        {
            if (string.IsNullOrEmpty(og.condition))
                return true;

            var condVal = data?.GetRaw(og.condition);
            if (condVal is bool b)
                return b;
            if (condVal is string s)
                return s == "true";
            return true;
        }

        /// <summary>
        /// Legacy: outputGroups[].name held the default group string (e.g. "extrude_top")
        /// while the editable property key differed (e.g. "topGroup").
        /// </summary>
        private static bool TryResolveLegacyDynamic(
            ManifestNodeDef def,
            PcgNodeData data,
            string declaredName,
            out string propertyKey,
            out string groupName)
        {
            propertyKey = null;
            groupName = null;

            foreach (var (key, prop) in def.properties)
            {
                if (!prop.isGroupOutput)
                    continue;

                var defaultName = prop.defaultValue?.ToString();
                if (defaultName != declaredName && key != declaredName)
                    continue;

                var current = data?.GetRaw(key)?.ToString();
                if (string.IsNullOrWhiteSpace(current))
                    current = string.IsNullOrEmpty(defaultName) ? declaredName : defaultName;

                propertyKey = key;
                groupName = current;
                return true;
            }

            return false;
        }
    }
}
