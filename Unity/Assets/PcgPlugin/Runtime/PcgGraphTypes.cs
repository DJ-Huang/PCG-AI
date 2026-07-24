using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    public static class PcgNodeTypes
    {
        public const string SpawnPoints = "SpawnPoints";
        public const string PlaceInScene = "PlaceInScene";

        public static readonly string[] All =
        {
            SpawnPoints,
            PlaceInScene,
        };
    }

    [Serializable]
    public class PcgGraphParameter
    {
        public string id;
        public string name;
        public string type = "number";
        public string defaultValue = "";
        public bool exposed = true;
        public string targetNode = "";
        public string targetProperty = "";
        public bool hasRange = false;
        public float minValue = 0f;
        public float maxValue = 1f;
    }

    public static class PcgGraphParameterUtility
    {
        public static List<PcgGraphParameter> FindBindingsTargetingNodes(
            IEnumerable<PcgGraphParameter> parameters,
            ISet<string> nodeIds)
        {
            if (parameters == null || nodeIds == null || nodeIds.Count == 0)
                return new List<PcgGraphParameter>();

            return parameters.Where(parameter =>
                parameter != null &&
                !string.IsNullOrEmpty(parameter.targetNode) &&
                nodeIds.Contains(parameter.targetNode)).ToList();
        }
    }

    [Serializable]
    public class PcgParameterOverride
    {
        public string parameterId;
        public string name;
        public string type = "number";
        public float floatValue;
        public int intValue;
        public bool boolValue;
        public string stringValue = "";

        public static PcgParameterOverride FromParameter(PcgGraphParameter param)
        {
            var o = new PcgParameterOverride
            {
                parameterId = param.id,
                name = param.name,
                type = param.type,
            };

            switch (param.type)
            {
                case "integer":
                    int.TryParse(param.defaultValue, out o.intValue);
                    break;
                case "number":
                    float.TryParse(param.defaultValue, out o.floatValue);
                    break;
                case "boolean":
                    bool.TryParse(param.defaultValue, out o.boolValue);
                    break;
                default:
                    o.stringValue = param.defaultValue;
                    break;
            }

            return o;
        }

        public object GetValue()
        {
            return type switch
            {
                "integer" => intValue,
                "number" => floatValue,
                "boolean" => boolValue,
                _ => stringValue,
            };
        }
    }

    [Serializable]
    public struct PcgGraphPosition
    {
        public float x;
        public float y;

        public Vector2 ToVector2() => new(x, y);

        public static PcgGraphPosition FromVector2(Vector2 v) => new() { x = v.x, y = v.y };
    }

    [Serializable]
    public class PcgSubgraphInterfaceSnapshot
    {
        public string name = "";
        public List<PcgSubgraphPort> inputs = new();
        public List<PcgSubgraphPort> outputs = new();

        public PcgSubgraphInterfaceSnapshot Clone()
        {
            return new PcgSubgraphInterfaceSnapshot
            {
                name = name,
                inputs = inputs.Select(ClonePort).ToList(),
                outputs = outputs.Select(ClonePort).ToList(),
            };
        }

        public static PcgSubgraphInterfaceSnapshot FromDefinition(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return null;
            return new PcgSubgraphInterfaceSnapshot
            {
                name = definition.name ?? "",
                inputs = definition.inputs.Select(ClonePort).ToList(),
                outputs = definition.outputs.Select(ClonePort).ToList(),
            };
        }

        private static PcgSubgraphPort ClonePort(PcgSubgraphPort port)
        {
            if (port == null)
                return null;
            return new PcgSubgraphPort
            {
                id = port.id,
                name = port.name,
                pinType = port.pinType,
            };
        }
    }

    [Serializable]
    public class PcgGraphNodeRecord
    {
        public string id;
        public string type;
        public PcgGraphPosition position;
        public PcgNodeData data = new();
        /// <summary>
        /// Authoring-only interface snapshot for linked <c>SubgraphAsset</c> nodes (v3).
        /// Null for ordinary nodes and v1/v2 graphs.
        /// </summary>
        public PcgSubgraphInterfaceSnapshot subgraphInterface;

        public PcgGraphNodeRecord Clone()
        {
            return new PcgGraphNodeRecord
            {
                id = id,
                type = type,
                position = position,
                data = data?.Clone() ?? new PcgNodeData(),
                subgraphInterface = subgraphInterface?.Clone(),
            };
        }
    }

    [Serializable]
    public class PcgGraphEdgeRecord
    {
        public string id;
        public string source;
        public string target;
        public string sourceHandle = "out";
        public string targetHandle = "in";
        public string sourcePinType;
        public string targetPinType;

        public PcgGraphEdgeRecord Clone()
        {
            return new PcgGraphEdgeRecord
            {
                id = id,
                source = source,
                target = target,
                sourceHandle = sourceHandle,
                targetHandle = targetHandle,
                sourcePinType = sourcePinType,
                targetPinType = targetPinType,
            };
        }
    }

    [Serializable]
    public class PcgSubgraphPort
    {
        public string id;
        public string name;
        public string pinType = "Any";
    }

    [Serializable]
    public class PcgSubgraphDefinition
    {
        public string id;
        public string name;
        public List<PcgSubgraphPort> inputs = new();
        public List<PcgSubgraphPort> outputs = new();
        public List<PcgGraphNodeRecord> nodes = new();
        public List<PcgGraphEdgeRecord> edges = new();

        public PcgSubgraphDefinition Clone()
        {
            return new PcgSubgraphDefinition
            {
                id = id,
                name = name,
                inputs = inputs.Select(port => port == null ? null : new PcgSubgraphPort
                {
                    id = port.id,
                    name = port.name,
                    pinType = port.pinType,
                }).ToList(),
                outputs = outputs.Select(port => port == null ? null : new PcgSubgraphPort
                {
                    id = port.id,
                    name = port.name,
                    pinType = port.pinType,
                }).ToList(),
                nodes = nodes.Select(node => node?.Clone()).ToList(),
                edges = edges.Select(edge => edge?.Clone()).ToList(),
            };
        }
    }

    [Serializable]
    public class PcgGraphDocument
    {
        public string version = "1.0";
        public List<PcgGraphNodeRecord> nodes = new();
        public List<PcgGraphEdgeRecord> edges = new();
        public List<PcgGraphParameter> parameters = new();
        public List<PcgSubgraphDefinition> subgraphs = new();

        public bool HasExternalSubgraphAssets()
        {
            if (nodes != null && nodes.Any(IsExternalSubgraphAsset))
                return true;
            if (subgraphs == null)
                return false;
            return subgraphs.Any(definition =>
                definition?.nodes != null && definition.nodes.Any(IsExternalSubgraphAsset));
        }

        public PcgGraphDocument Clone()
        {
            return new PcgGraphDocument
            {
                version = version,
                nodes = nodes.Select(node => node?.Clone()).ToList(),
                edges = edges.Select(edge => edge?.Clone()).ToList(),
                parameters = parameters.Select(CloneParameter).ToList(),
                subgraphs = subgraphs.Select(definition => definition?.Clone()).ToList(),
            };
        }

        private static bool IsExternalSubgraphAsset(PcgGraphNodeRecord node) =>
            node != null && node.type == PcgStructuralNodeTypes.SubgraphAsset;

        private static PcgGraphParameter CloneParameter(PcgGraphParameter parameter)
        {
            if (parameter == null)
                return null;
            return new PcgGraphParameter
            {
                id = parameter.id,
                name = parameter.name,
                type = parameter.type,
                defaultValue = parameter.defaultValue,
                exposed = parameter.exposed,
                targetNode = parameter.targetNode,
                targetProperty = parameter.targetProperty,
                hasRange = parameter.hasRange,
                minValue = parameter.minValue,
                maxValue = parameter.maxValue,
            };
        }
    }

    public static class PcgStructuralNodeTypes
    {
        public const string Subgraph = "Subgraph";
        public const string SubgraphInput = "SubgraphInput";
        public const string SubgraphOutput = "SubgraphOutput";
        public const string SubgraphAsset = "SubgraphAsset";
    }

    [Serializable]
    public class PcgNodeData
    {
        [SerializeField] private List<string> rawKeys = new();
        [SerializeField] private List<string> rawValues = new();

        public void SetRaw(string key, object value)
        {
            var text = ValueToString(value);
            var index = rawKeys.IndexOf(key);
            if (index >= 0)
                rawValues[index] = text;
            else
            {
                rawKeys.Add(key);
                rawValues.Add(text);
            }
        }

        public object GetRaw(string key)
        {
            var index = rawKeys.IndexOf(key);
            return index < 0 ? null : rawValues[index];
        }

        public bool RemoveRaw(string key)
        {
            var index = rawKeys.IndexOf(key);
            if (index < 0)
                return false;
            rawKeys.RemoveAt(index);
            rawValues.RemoveAt(index);
            return true;
        }

        public IEnumerable<(string key, object value)> EnumerateRaw()
        {
            for (var i = 0; i < rawKeys.Count; i++)
                yield return (rawKeys[i], rawValues[i]);
        }

        public void ClearRaw()
        {
            rawKeys.Clear();
            rawValues.Clear();
        }

        private static string ValueToString(object value) => value switch
        {
            null => "",
            bool b => b ? "true" : "false",
            float f => f.ToString(CultureInfo.InvariantCulture),
            double d => d.ToString(CultureInfo.InvariantCulture),
            int i => i.ToString(CultureInfo.InvariantCulture),
            long l => l.ToString(CultureInfo.InvariantCulture),
            _ => value.ToString(),
        };

        public static PcgNodeData DefaultForType(string type)
        {
            var manifestProps = PcgGraphSerializer.ManifestLookup?.Invoke(type);
            if (manifestProps != null)
            {
                var data = new PcgNodeData();
                foreach (var (key, prop) in manifestProps)
                    data.SetRaw(key, prop.defaultValue ?? "");
                return data;
            }

            return new PcgNodeData();
        }

        public PcgNodeData Clone()
        {
            var clone = new PcgNodeData();
            clone.rawKeys.AddRange(rawKeys);
            clone.rawValues.AddRange(rawValues);
            return clone;
        }
    }

    public static class PcgGraphDefaults
    {
        public static PcgGraphDocument CreatePipeline()
        {
            return new PcgGraphDocument
            {
                version = "1.0",
                nodes = new List<PcgGraphNodeRecord>
                {
                    new()
                    {
                        id = "n1",
                        type = PcgNodeTypes.SpawnPoints,
                        position = PcgGraphPosition.FromVector2(new Vector2(50, 150)),
                        data = PcgNodeData.DefaultForType(PcgNodeTypes.SpawnPoints),
                    },
                    new()
                    {
                        id = "n2",
                        type = PcgNodeTypes.PlaceInScene,
                        position = PcgGraphPosition.FromVector2(new Vector2(400, 150)),
                        data = PcgNodeData.DefaultForType(PcgNodeTypes.PlaceInScene),
                    },
                },
                edges = new List<PcgGraphEdgeRecord>
                {
                    new() { id = "e1", source = "n1", target = "n2", sourceHandle = "out", targetHandle = "in" },
                },
            };
        }

        public static int ExtractSeed(PcgGraphDocument doc)
        {
            foreach (var node in doc.nodes)
            {
                if (node.type == "GetTerrainData")
                    return node.data.GetRaw("seed") as int? ?? 42;
            }

            return 42;
        }
    }
}
