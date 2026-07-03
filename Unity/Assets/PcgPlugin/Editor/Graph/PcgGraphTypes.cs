using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    public static class PcgNodeTypes
    {
        public const string ParseConfig = "ParseConfig";
        public const string SpawnPoints = "SpawnPoints";
        public const string PlaceInScene = "PlaceInScene";

        public static readonly string[] All =
        {
            ParseConfig,
            SpawnPoints,
            PlaceInScene,
        };
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
            if (PcgNodeManifest.TryGet(type, out _))
                return PcgNodeManifest.DefaultDataFor(type);

            var data = new PcgNodeData();
            switch (type)
            {
                case PcgNodeTypes.ParseConfig:
                    data.seed = 42;
                    data.density = 0.5f;
                    data.SetRaw("seed", 42);
                    data.SetRaw("density", 0.5f);
                    break;
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
                        type = PcgNodeTypes.ParseConfig,
                        position = PcgGraphPosition.FromVector2(new Vector2(50, 150)),
                        data = PcgNodeData.DefaultForType(PcgNodeTypes.ParseConfig),
                    },
                    new()
                    {
                        id = "n2",
                        type = PcgNodeTypes.SpawnPoints,
                        position = PcgGraphPosition.FromVector2(new Vector2(400, 150)),
                        data = PcgNodeData.DefaultForType(PcgNodeTypes.SpawnPoints),
                    },
                    new()
                    {
                        id = "n3",
                        type = PcgNodeTypes.PlaceInScene,
                        position = PcgGraphPosition.FromVector2(new Vector2(750, 150)),
                        data = PcgNodeData.DefaultForType(PcgNodeTypes.PlaceInScene),
                    },
                },
                edges = new List<PcgGraphEdgeRecord>
                {
                    new() { id = "e1", source = "n1", target = "n2", sourceHandle = "out", targetHandle = "in" },
                    new() { id = "e2", source = "n2", target = "n3", sourceHandle = "out", targetHandle = "in" },
                },
            };
        }

        public static int ExtractSeed(PcgGraphDocument doc)
        {
            foreach (var node in doc.nodes)
            {
                if (node.type == PcgNodeTypes.ParseConfig)
                    return node.data.seed;
            }

            return 42;
        }
    }
}
