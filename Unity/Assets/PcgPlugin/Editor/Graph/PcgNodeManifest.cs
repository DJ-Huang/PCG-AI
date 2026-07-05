using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEngine;
using DJTechRuntime.PCG;
using UnityEditor;

namespace DJTechEditor.PCG.Graph
{
    public class ManifestPinDef
    {
        public string id;
        public string label;
        public string pinType;
    }

    public class ManifestPropertyOption
    {
        public string value;
        public string label;
    }

    public class ManifestPropertyDef
    {
        public string type;
        public object defaultValue;
        public List<ManifestPropertyOption> options = new();
    }

    public class ManifestNodeDef
    {
        public string type;
        public string displayName;
        public string category;
        public List<ManifestPinDef> inputs = new();
        public List<ManifestPinDef> outputs = new();
        public Dictionary<string, ManifestPropertyDef> properties = new();
    }

    /// <summary>Loads schema/node-manifest.json for manifest-driven GraphView nodes.</summary>
    [InitializeOnLoad]
    public static class PcgNodeManifest
    {
        private static Dictionary<string, ManifestNodeDef> _byType = new();
        private static List<ManifestNodeDef> _all = new();
        private static bool _loaded;

        static PcgNodeManifest()
        {
            PcgGraphSerializer.ManifestLookup = type =>
            {
                if (!TryGet(type, out var def))
                    return null;

                var dict = new Dictionary<string, ManifestPropertyInfo>();
                foreach (var (key, prop) in def.properties)
                {
                    dict[key] = new ManifestPropertyInfo
                    {
                        type = prop.type,
                        defaultValue = prop.defaultValue,
                    };
                }
                return dict;
            };
        }

        public static IReadOnlyList<ManifestNodeDef> All
        {
            get { EnsureLoaded(); return _all; }
        }

        public static bool TryGet(string type, out ManifestNodeDef def)
        {
            EnsureLoaded();
            return _byType.TryGetValue(type, out def);
        }

        public static bool IsManifestOnlyType(string type) =>
            type is not (PcgNodeTypes.ParseConfig or PcgNodeTypes.SpawnPoints or PcgNodeTypes.PlaceInScene)
            && TryGet(type, out _);

        public static string GetOutputPinType(string sourceType, string sourceHandle = "out")
        {
            if (!TryGet(sourceType, out var def))
                return InferLegacyPinType(sourceType, isOutput: true);

            var pin = def.outputs.FirstOrDefault(p => p.id == (sourceHandle ?? "out"));
            return pin?.pinType ?? "SpatialPoint";
        }

        public static string GetInputPinType(string targetType, string targetHandle = "in")
        {
            if (!TryGet(targetType, out var def))
                return InferLegacyPinType(targetType, isOutput: false);

            var pin = def.inputs.FirstOrDefault(p => p.id == (targetHandle ?? "in"));
            return pin?.pinType ?? "SpatialPoint";
        }

        public static bool CanConnect(string sourceType, string targetType, string sourceHandle, string targetHandle)
        {
            return GetOutputPinType(sourceType, sourceHandle) == GetInputPinType(targetType, targetHandle);
        }

        public static PcgNodeData DefaultDataFor(string type)
        {
            if (!TryGet(type, out var def))
                return PcgNodeData.DefaultForType(type);

            var data = new PcgNodeData();
            foreach (var (key, prop) in def.properties)
                data.SetRaw(key, prop.defaultValue);
            return data;
        }

        private static string InferLegacyPinType(string type, bool isOutput) => type switch
        {
            PcgNodeTypes.ParseConfig => "Param",
            PcgNodeTypes.SpawnPoints => isOutput ? "SpatialPoint" : "Param",
            PcgNodeTypes.PlaceInScene => "SpatialPoint",
            _ => "SpatialPoint",
        };

        private static void EnsureLoaded()
        {
            if (_loaded)
                return;

            _byType = new Dictionary<string, ManifestNodeDef>();
            _all = new List<ManifestNodeDef>();

            var path = Path.GetFullPath(Path.Combine(Application.dataPath, "../../schema/node-manifest.json"));
            if (!File.Exists(path))
            {
                Debug.LogWarning($"[PCG] node-manifest not found at {path}");
                _loaded = true;
                return;
            }

            try
            {
                var root = PcgMiniJson.Deserialize(File.ReadAllText(path)) as Dictionary<string, object>;
                if (root?.TryGetValue("nodes", out var nodesObj) != true || nodesObj is not List<object> nodesList)
                {
                    _loaded = true;
                    return;
                }

                foreach (var nodeObj in nodesList)
                {
                    if (nodeObj is not Dictionary<string, object> nodeDict)
                        continue;
                    var def = ParseNode(nodeDict);
                    if (string.IsNullOrEmpty(def.type))
                        continue;
                    _all.Add(def);
                    _byType[def.type] = def;
                }
            }
            catch (Exception ex)
            {
                Debug.LogError($"[PCG] Failed to load node-manifest: {ex.Message}");
            }

            _loaded = true;
        }

        private static ManifestNodeDef ParseNode(Dictionary<string, object> nodeDict)
        {
            var def = new ManifestNodeDef
            {
                type = GetString(nodeDict, "type"),
                displayName = GetString(nodeDict, "displayName", GetString(nodeDict, "type")),
                category = GetString(nodeDict, "category", "Other"),
            };

            if (nodeDict.TryGetValue("inputs", out var inputs) && inputs is List<object> inputList)
            {
                foreach (var item in inputList)
                {
                    if (item is Dictionary<string, object> pin)
                        def.inputs.Add(ParsePin(pin));
                }
            }

            if (nodeDict.TryGetValue("outputs", out var outputs) && outputs is List<object> outputList)
            {
                foreach (var item in outputList)
                {
                    if (item is Dictionary<string, object> pin)
                        def.outputs.Add(ParsePin(pin));
                }
            }

            if (nodeDict.TryGetValue("properties", out var props) && props is Dictionary<string, object> propDict)
            {
                foreach (var (key, value) in propDict)
                {
                    if (value is Dictionary<string, object> propObj)
                    {
                        var propDef = new ManifestPropertyDef
                        {
                            type = GetString(propObj, "type"),
                            defaultValue = ParseDefault(propObj),
                        };
                        if (propObj.TryGetValue("options", out var optionsObj) &&
                            optionsObj is List<object> optionsList)
                        {
                            foreach (var optionObj in optionsList)
                            {
                                if (optionObj is not Dictionary<string, object> optionDict)
                                    continue;
                                propDef.options.Add(new ManifestPropertyOption
                                {
                                    value = GetString(optionDict, "value"),
                                    label = GetString(optionDict, "label", GetString(optionDict, "value")),
                                });
                            }
                        }
                        def.properties[key] = propDef;
                    }
                }
            }

            return def;
        }

        private static ManifestPinDef ParsePin(Dictionary<string, object> pin) => new()
        {
            id = GetString(pin, "id"),
            label = GetString(pin, "label", GetString(pin, "id")),
            pinType = GetString(pin, "pinType", "SpatialPoint"),
        };

        private static object ParseDefault(Dictionary<string, object> prop)
        {
            var type = GetString(prop, "type");
            if (!prop.TryGetValue("default", out var value) || value == null)
            {
            return type switch
            {
                "integer" => 0,
                "number" => 0f,
                "boolean" => false,
                "enum" => GetString(prop, "default", ""),
                _ => "",
            };
            }

            return type switch
            {
                "integer" => Convert.ToInt32(value, CultureInfo.InvariantCulture),
                "number" => Convert.ToSingle(value, CultureInfo.InvariantCulture),
                "boolean" => Convert.ToBoolean(value, CultureInfo.InvariantCulture),
                "enum" => value.ToString(),
                _ => value.ToString(),
            };
        }

        private static string GetString(Dictionary<string, object> dict, string key, string fallback = "") =>
            dict.TryGetValue(key, out var value) ? value?.ToString() ?? fallback : fallback;
    }
}
