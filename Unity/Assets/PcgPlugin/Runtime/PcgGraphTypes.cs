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
    public class PcgGraphNodeRecord
    {
        public string id;
        public string type;
        public PcgGraphPosition position;
        public PcgNodeData data = new();
    }

    [Serializable]
    public class PcgGraphEdgeRecord
    {
        public string id;
        public string source;
        public string target;
        public string sourceHandle = "out";
        public string targetHandle = "in";
    }

    [Serializable]
    public class PcgGraphDocument
    {
        public string version = "1.0";
        public List<PcgGraphNodeRecord> nodes = new();
        public List<PcgGraphEdgeRecord> edges = new();
        public List<PcgGraphParameter> parameters = new();
    }

    [Serializable]
    public class PcgNodeData
    {
        public int seed = 42;
        public float density = 0.5f;
        public int count = 100;
        public float radius = 10f;
        public string prefab = "";
        public float scale = 1f;

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

            SyncLegacyFields(key, value);
        }

        public object GetRaw(string key)
        {
            var index = rawKeys.IndexOf(key);
            if (index < 0)
                return GetLegacyField(key);

            return rawValues[index];
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

        private void SyncLegacyFields(string key, object value)
        {
            switch (key)
            {
                case "seed": seed = Convert.ToInt32(value); break;
                case "density": density = Convert.ToSingle(value, CultureInfo.InvariantCulture); break;
                case "count": count = Convert.ToInt32(value); break;
                case "radius": radius = Convert.ToSingle(value, CultureInfo.InvariantCulture); break;
                case "prefab": prefab = value?.ToString() ?? ""; break;
                case "scale": scale = Convert.ToSingle(value, CultureInfo.InvariantCulture); break;
            }
        }

        private object GetLegacyField(string key) => key switch
        {
            "seed" => seed,
            "density" => density,
            "count" => count,
            "radius" => radius,
            "prefab" => prefab,
            "scale" => scale,
            _ => null,
        };

        public static PcgNodeData DefaultForType(string type)
        {
            var manifestProps = PcgGraphSerializer.ManifestLookup?.Invoke(type);
            if (manifestProps != null)
            {
                var manifestData = new PcgNodeData();
                foreach (var (key, prop) in manifestProps)
                    manifestData.SetRaw(key, prop.defaultValue ?? "");
                return manifestData;
            }

            var data = new PcgNodeData();
            switch (type)
            {
                case PcgNodeTypes.SpawnPoints:
                    data.count = 100;
                    data.radius = 10f;
                    data.SetRaw("count", 100);
                    data.SetRaw("radius", 10f);
                    break;
                case PcgNodeTypes.PlaceInScene:
                    data.prefab = "";
                    data.scale = 1f;
                    data.SetRaw("prefab", "");
                    data.SetRaw("scale", 1f);
                    break;
            }

            return data;
        }

        public PcgNodeData Clone()
        {
            var clone = new PcgNodeData
            {
                seed = seed,
                density = density,
                count = count,
                radius = radius,
                prefab = prefab,
                scale = scale,
            };
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
