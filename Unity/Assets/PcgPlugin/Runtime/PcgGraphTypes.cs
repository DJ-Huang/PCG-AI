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

        public static PcgGraphParameter CloneParameter(PcgGraphParameter parameter)
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

        public static void PromoteBindings(
            IList<PcgGraphParameter> source,
            PcgSubgraphDefinition definition,
            ISet<string> nodeIds)
        {
            if (definition == null)
                return;
            definition.parameters ??= new List<PcgGraphParameter>();
            PromoteBindingsToList(source, definition.parameters, nodeIds);
        }

        public static void PromoteBindingsToList(
            IList<PcgGraphParameter> source,
            List<PcgGraphParameter> destination,
            ISet<string> nodeIds)
        {
            if (source == null || destination == null || nodeIds == null || nodeIds.Count == 0)
                return;

            for (var index = source.Count - 1; index >= 0; index--)
            {
                var parameter = source[index];
                if (parameter == null ||
                    string.IsNullOrEmpty(parameter.targetNode) ||
                    !nodeIds.Contains(parameter.targetNode))
                {
                    continue;
                }

                destination.Add(CloneParameter(parameter));
                source.RemoveAt(index);
            }
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
            var clone = new PcgSubgraphInterfaceSnapshot
            {
                name = name,
                inputs = inputs.Select(ClonePort).ToList(),
                outputs = outputs.Select(ClonePort).ToList(),
            };
            PcgSubgraphInputUtility.NormalizePorts(clone.inputs);
            PcgSubgraphOutputUtility.NormalizePorts(clone.outputs);
            return clone;
        }

        public static PcgSubgraphInterfaceSnapshot FromDefinition(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return null;
            var snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = definition.name ?? "",
                inputs = definition.inputs.Select(ClonePort).ToList(),
                outputs = definition.outputs.Select(ClonePort).ToList(),
            };
            PcgSubgraphInputUtility.NormalizePorts(snapshot.inputs, definition.nodes);
            PcgSubgraphOutputUtility.NormalizePorts(snapshot.outputs);
            return snapshot;
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
                anchorPlaced = port.anchorPlaced,
                anchorX = port.anchorX,
                anchorY = port.anchorY,
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
        /// <summary>When true, a visible interface anchor is shown on the subgraph canvas.</summary>
        public bool anchorPlaced;
        public float anchorX;
        public float anchorY;
    }

    /// <summary>
    /// Houdini-style subgraph input contract: inputs are untyped, always visible inside the
    /// subgraph, and every definition exposes at least one stable input handle.
    /// </summary>
    public static class PcgSubgraphInputUtility
    {
        public const string AnyPinType = "Any";

        public static bool Synchronize(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return false;

            definition.inputs ??= new List<PcgSubgraphPort>();
            definition.nodes ??= new List<PcgGraphNodeRecord>();
            var changed = NormalizePorts(definition.inputs, definition.nodes);

            if (!definition.nodes.Any(node =>
                    node?.type == PcgStructuralNodeTypes.SubgraphInput))
            {
                var ids = new HashSet<string>(
                    definition.nodes.Where(node => node != null).Select(node => node.id),
                    StringComparer.Ordinal);
                var nodeId = "subgraph_input";
                var suffix = 1;
                while (!ids.Add(nodeId))
                    nodeId = $"subgraph_input_{++suffix}";

                definition.nodes.Add(new PcgGraphNodeRecord
                {
                    id = nodeId,
                    type = PcgStructuralNodeTypes.SubgraphInput,
                    position = new PcgGraphPosition { x = DefaultAnchorX(definition.nodes), y = 0f },
                    data = new PcgNodeData(),
                });
                changed = true;
            }

            return changed;
        }

        public static bool NormalizePorts(
            List<PcgSubgraphPort> inputs,
            IEnumerable<PcgGraphNodeRecord> nodes = null)
        {
            if (inputs == null)
                return false;

            var changed = false;
            if (inputs.Count == 0)
            {
                inputs.Add(new PcgSubgraphPort
                {
                    id = "in_1",
                    name = "Input 1",
                    pinType = AnyPinType,
                });
                changed = true;
            }

            var ids = new HashSet<string>(StringComparer.Ordinal);
            var defaultX = DefaultAnchorX(nodes);
            for (var index = 0; index < inputs.Count; index++)
            {
                var port = inputs[index];
                if (port == null)
                {
                    port = new PcgSubgraphPort();
                    inputs[index] = port;
                    changed = true;
                }

                if (string.IsNullOrEmpty(port.id) || !ids.Add(port.id))
                {
                    var ordinal = index + 1;
                    var id = $"in_{ordinal}";
                    while (ids.Contains(id))
                        id = $"in_{++ordinal}";
                    port.id = id;
                    ids.Add(id);
                    changed = true;
                }

                if (string.IsNullOrEmpty(port.name))
                {
                    port.name = $"Input {index + 1}";
                    changed = true;
                }

                if (!string.Equals(port.pinType, AnyPinType, StringComparison.Ordinal))
                {
                    port.pinType = AnyPinType;
                    changed = true;
                }

                if (!port.anchorPlaced)
                {
                    port.anchorPlaced = true;
                    port.anchorX = defaultX;
                    port.anchorY = index * 90f;
                    changed = true;
                }
            }

            return changed;
        }

        private static float DefaultAnchorX(IEnumerable<PcgGraphNodeRecord> nodes)
        {
            var executable = (nodes ?? Enumerable.Empty<PcgGraphNodeRecord>())
                .Where(node => node != null &&
                               node.type != PcgStructuralNodeTypes.SubgraphInput &&
                               node.type != PcgStructuralNodeTypes.SubgraphOutput)
                .ToList();
            return executable.Count == 0 ? -200f : executable.Min(node => node.position.x) - 220f;
        }
    }

    /// <summary>
    /// Houdini-style subgraph output contract: one ordinary Output node represents the
    /// single result exposed by a Subgraph instance. SubgraphOutput is accepted only as
    /// a legacy serialization form and is migrated in memory.
    /// </summary>
    public static class PcgSubgraphOutputUtility
    {
        public const string DefaultPortId = "out_1";

        public static bool Synchronize(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return false;

            definition.outputs ??= new List<PcgSubgraphPort>();
            definition.nodes ??= new List<PcgGraphNodeRecord>();
            definition.edges ??= new List<PcgGraphEdgeRecord>();

            var changed = NormalizePorts(definition.outputs);
            var regularOutputs = definition.nodes
                .Where(node => node?.type == "Output")
                .ToList();
            var legacyOutputs = definition.nodes
                .Where(node => node?.type == PcgStructuralNodeTypes.SubgraphOutput)
                .ToList();

            PcgGraphNodeRecord outputNode;
            if (regularOutputs.Count > 0)
            {
                outputNode = regularOutputs[0];
            }
            else if (legacyOutputs.Count > 0)
            {
                outputNode = legacyOutputs[0];
                outputNode.type = "Output";
                outputNode.data ??= new PcgNodeData();
                changed = true;
            }
            else
            {
                outputNode = CreateOutputNode(definition.nodes);
                definition.nodes.Add(outputNode);
                changed = true;
            }

            if (outputNode.data?.GetRaw("label") == null)
            {
                outputNode.data ??= new PcgNodeData();
                outputNode.data.SetRaw("label", "Output");
                changed = true;
            }

            var obsoleteIds = new HashSet<string>(
                regularOutputs
                    .Concat(legacyOutputs)
                    .Where(node => !ReferenceEquals(node, outputNode))
                    .Select(node => node.id),
                StringComparer.Ordinal);
            if (obsoleteIds.Count > 0)
            {
                foreach (var edge in definition.edges)
                {
                    if (edge == null)
                        continue;
                    if (obsoleteIds.Contains(edge.target))
                    {
                        edge.target = outputNode.id;
                        edge.targetHandle = "in";
                    }
                }

                definition.edges.RemoveAll(edge =>
                    edge != null && obsoleteIds.Contains(edge.source));
                definition.nodes.RemoveAll(node =>
                    node != null && obsoleteIds.Contains(node.id));
                changed = true;
            }

            foreach (var edge in definition.edges)
            {
                if (edge == null || edge.target != outputNode.id ||
                    string.Equals(edge.targetHandle, "in", StringComparison.Ordinal))
                {
                    continue;
                }

                edge.targetHandle = "in";
                changed = true;
            }

            var seenInputs = new HashSet<string>(StringComparer.Ordinal);
            for (var index = definition.edges.Count - 1; index >= 0; index--)
            {
                var edge = definition.edges[index];
                if (edge == null || edge.target != outputNode.id)
                    continue;
                var key = (edge.source ?? "") + "\u001f" + (edge.sourceHandle ?? "out");
                if (seenInputs.Add(key))
                    continue;
                definition.edges.RemoveAt(index);
                changed = true;
            }

            return changed;
        }

        public static bool NormalizePorts(List<PcgSubgraphPort> outputs)
        {
            if (outputs == null)
                return false;

            var changed = false;
            if (outputs.Count == 0)
            {
                outputs.Add(new PcgSubgraphPort
                {
                    id = DefaultPortId,
                    name = "Output",
                    pinType = "Any",
                });
                return true;
            }

            if (outputs.Count > 1)
            {
                outputs.RemoveRange(1, outputs.Count - 1);
                changed = true;
            }

            var port = outputs[0];
            if (port == null)
            {
                port = new PcgSubgraphPort();
                outputs[0] = port;
                changed = true;
            }
            if (string.IsNullOrEmpty(port.id))
            {
                port.id = DefaultPortId;
                changed = true;
            }
            if (string.IsNullOrEmpty(port.name))
            {
                port.name = "Output";
                changed = true;
            }
            if (string.IsNullOrEmpty(port.pinType))
            {
                port.pinType = "Any";
                changed = true;
            }
            if (port.anchorPlaced)
            {
                port.anchorPlaced = false;
                changed = true;
            }
            return changed;
        }

        private static PcgGraphNodeRecord CreateOutputNode(
            IEnumerable<PcgGraphNodeRecord> nodes)
        {
            var nodeList = (nodes ?? Enumerable.Empty<PcgGraphNodeRecord>())
                .Where(node => node != null)
                .ToList();
            var ids = new HashSet<string>(nodeList.Select(node => node.id), StringComparer.Ordinal);
            var id = "output";
            var suffix = 1;
            while (!ids.Add(id))
                id = $"output_{++suffix}";

            var executable = nodeList
                .Where(node => node.type != PcgStructuralNodeTypes.SubgraphInput &&
                               node.type != PcgStructuralNodeTypes.SubgraphOutput)
                .ToList();
            var x = executable.Count == 0 ? 0f : executable.Average(node => node.position.x);
            var y = executable.Count == 0 ? 160f : executable.Max(node => node.position.y) + 160f;
            var data = new PcgNodeData();
            data.SetRaw("label", "Output");
            return new PcgGraphNodeRecord
            {
                id = id,
                type = "Output",
                position = new PcgGraphPosition { x = x, y = y },
                data = data,
            };
        }
    }

    public static class PcgSubgraphContractUtility
    {
        public static bool Synchronize(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return false;
            var changed = PcgSubgraphInputUtility.Synchronize(definition);
            changed |= PcgSubgraphOutputUtility.Synchronize(definition);
            return changed;
        }
    }

    [Serializable]
    public class PcgSubgraphDefinition
    {
        public string id;
        public string name;
        public List<PcgSubgraphPort> inputs = new();
        public List<PcgSubgraphPort> outputs = new();
        public List<PcgGraphParameter> parameters = new();
        public List<PcgGraphNodeRecord> nodes = new();
        public List<PcgGraphEdgeRecord> edges = new();

        public PcgSubgraphDefinition Clone()
        {
            var clone = new PcgSubgraphDefinition
            {
                id = id,
                name = name,
                inputs = inputs.Select(port => port == null ? null : new PcgSubgraphPort
                {
                    id = port.id,
                    name = port.name,
                    pinType = port.pinType,
                    anchorPlaced = port.anchorPlaced,
                    anchorX = port.anchorX,
                    anchorY = port.anchorY,
                }).ToList(),
                outputs = outputs.Select(port => port == null ? null : new PcgSubgraphPort
                {
                    id = port.id,
                    name = port.name,
                    pinType = port.pinType,
                    anchorPlaced = port.anchorPlaced,
                    anchorX = port.anchorX,
                    anchorY = port.anchorY,
                }).ToList(),
                parameters = parameters.Select(PcgGraphParameterUtility.CloneParameter).ToList(),
                nodes = nodes.Select(node => node?.Clone()).ToList(),
                edges = edges.Select(edge => edge?.Clone()).ToList(),
            };
            PcgSubgraphContractUtility.Synchronize(clone);
            return clone;
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

        private static PcgGraphParameter CloneParameter(PcgGraphParameter parameter) =>
            PcgGraphParameterUtility.CloneParameter(parameter);
    }

    public static class PcgStructuralNodeTypes
    {
        public const string Subgraph = "Subgraph";
        public const string SubgraphInput = "SubgraphInput";
        public const string SubgraphOutput = "SubgraphOutput";
        public const string SubgraphAsset = "SubgraphAsset";
        public const string SubgraphParentRef = "SubgraphParentRef";
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
