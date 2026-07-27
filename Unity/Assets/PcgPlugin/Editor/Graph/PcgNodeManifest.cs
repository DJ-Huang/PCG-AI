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
        public bool variadic;
    }

    public class ManifestPropertyOption
    {
        public string value;
        public string label;
    }

    public class ManifestVisibleWhenClause
    {
        public string property;
        public string equals;
        public List<string> oneOf;
    }

    public class ManifestOutputGroupDef
    {
        public string name;
        public string domain;
        public string label;
        public string condition;
        public bool dynamic;
    }

    public class ManifestPropertyDef
    {
        public string type;
        public object defaultValue;
        public List<ManifestPropertyOption> options = new();
        public bool hasRange = false;
        public float minimum = 0f;
        public float maximum = 1f;
        public string groupDomain;
        public bool isGroupOutput;
        public string displayName;
        public string section;
        public int order;
        public bool hasOrder;
        public string visibleWhenProperty;
        public string visibleWhenEquals;
        public List<string> visibleWhenOneOf;
        public List<ManifestVisibleWhenClause> visibleWhenAny;
        /// <summary>Houdini-style: keep row visible but grayed unless driver matches.</summary>
        public string enabledWhenProperty;
        public string enabledWhenEquals;
        public List<string> enabledWhenOneOf;
        /// <summary>Optional extra AND clauses (same shape as a single enabledWhen).</summary>
        public List<ManifestVisibleWhenClause> enabledWhenAll;
        /// <summary>Render this string/number field on the same row as a boolean toggle.</summary>
        public string companionField;
        /// <summary>Group properties onto one compact Houdini-style row.</summary>
        public string rowGroup;
        public int rowOrder;
        public bool hasRowOrder;
        /// <summary>Small inline label before this control (e.g. "to", "Offset by").</summary>
        public string rowPrefix;
        public bool indent;
        public bool multiline;
        public int lines = 1;
    }

    public class ManifestSectionDef
    {
        public string id;
        public string label;
        public bool foldout = true;
        public bool defaultExpanded = true;
        /// <summary>When foldout is false, still draw a separator header with label.</summary>
        public bool header;
    }

    public class ManifestNodeDef
    {
        public string type;
        public string displayName;
        public string category;
        public List<ManifestPinDef> inputs = new();
        public List<ManifestPinDef> outputs = new();
        public Dictionary<string, ManifestPropertyDef> properties = new();
        public List<ManifestOutputGroupDef> outputGroups = new();
        public List<ManifestSectionDef> inspectorSections = new();
    }

    /// <summary>Loads schema/node-manifest.json for manifest-driven GraphView nodes.</summary>
    [InitializeOnLoad]
    public static class PcgNodeManifest
    {
        private static Dictionary<string, ManifestNodeDef> _byType = new();
        private static List<ManifestNodeDef> _all = new();
        private static bool _loaded;
        private static string _loadedSourcePath;
        private static long _loadedSourceMtimeUtc;

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

        /// <summary>Force re-read node-manifest from disk (e.g. after schema sync).</summary>
        public static void Reload()
        {
            _loaded = false;
            _loadedSourcePath = null;
            _loadedSourceMtimeUtc = 0;
            EnsureLoaded();
        }

        public static string GetOutputPinType(string sourceType, string sourceHandle = "out")
        {
            if (!TryGet(sourceType, out var def))
                return "SpatialPoint";

            var pin = def.outputs.FirstOrDefault(p => p.id == (sourceHandle ?? "out"));
            return pin?.pinType ?? "SpatialPoint";
        }

        public static string GetInputPinType(string targetType, string targetHandle = "in")
        {
            if (!TryGet(targetType, out var def))
                return "SpatialPoint";

            var pin = def.inputs.FirstOrDefault(p => p.id == (targetHandle ?? "in"));
            return pin?.pinType ?? "SpatialPoint";
        }

        public static bool CanConnect(string sourceType, string targetType, string sourceHandle, string targetHandle)
        {
            var sourcePin = NormalizePinType(GetOutputPinType(sourceType, sourceHandle));
            var targetPin = NormalizePinType(GetInputPinType(targetType, targetHandle));
            return PinTypesCompatible(sourcePin, targetPin);
        }

        public static bool PinTypesCompatible(string sourcePin, string targetPin)
        {
            if (sourcePin == targetPin || sourcePin == "Any" || targetPin == "Any")
                return true;
            if (sourcePin == "SpatialGeometry" && IsSpatialGeometryFamily(targetPin))
                return true;
            if (targetPin == "SpatialGeometry" && IsSpatialGeometryFamily(sourcePin))
                return true;
            return false;
        }

        static bool IsSpatialGeometryFamily(string pinType)
        {
            return pinType == "SpatialGeometry" || pinType == "SpatialMesh" || pinType == "SpatialSpline";
        }

        static string NormalizePinType(string pinType)
        {
            return pinType == "Spline" ? "SpatialSpline" : pinType;
        }

        /// <summary>True if nodeType has an input pin matching the given pinType.</summary>
        public static bool HasCompatibleInputPin(string nodeType, string pinType)
        {
            pinType = NormalizePinType(pinType);
            if (!TryGet(nodeType, out var def))
                return NormalizePinType(GetInputPinType(nodeType)) == pinType;
            foreach (var input in def.inputs)
                if (PinTypesCompatible(pinType, NormalizePinType(input.pinType)) || input.pinType == "Any") return true;
            return false;
        }

        /// <summary>True if nodeType has an output pin matching the given pinType.</summary>
        public static bool HasCompatibleOutputPin(string nodeType, string pinType)
        {
            pinType = NormalizePinType(pinType);
            if (!TryGet(nodeType, out var def))
                return NormalizePinType(GetOutputPinType(nodeType)) == pinType;
            foreach (var output in def.outputs)
                if (PinTypesCompatible(pinType, NormalizePinType(output.pinType)) || output.pinType == "Any") return true;
            return false;
        }

        public static PcgNodeData DefaultDataFor(string type)
        {
            if (!TryGet(type, out var def))
                return new PcgNodeData();

            var data = new PcgNodeData();
            foreach (var (key, prop) in def.properties)
                data.SetRaw(key, prop.defaultValue);
            return data;
        }

        private static void EnsureLoaded()
        {
            if (!TryResolveManifestPath(out var manifestPath))
            {
                Debug.LogWarning("[PCG] node-manifest not found in dev path or Editor assets");
                return;
            }

            var mtimeUtc = File.GetLastWriteTimeUtc(manifestPath).Ticks;
            if (_loaded && manifestPath == _loadedSourcePath && mtimeUtc == _loadedSourceMtimeUtc)
                return;

            _byType = new Dictionary<string, ManifestNodeDef>();
            _all = new List<ManifestNodeDef>();
            _loadedSourcePath = manifestPath;
            _loadedSourceMtimeUtc = mtimeUtc;

            LoadFromString(File.ReadAllText(manifestPath));
        }

        private static bool TryResolveManifestPath(out string fullPath)
        {
            fullPath = null;

            // 1. Dev path — hot-reload during development (schema/ is repo sibling of Unity/)
            var devPath = Path.GetFullPath(Path.Combine(Application.dataPath, "../../schema/node-manifest.json"));
            if (File.Exists(devPath))
            {
                fullPath = devPath;
                return true;
            }

            // 2. Distribution — prefer Editor/Graph copy over Resources (stable order)
            var guids = AssetDatabase.FindAssets("node-manifest t:TextAsset");
            string editorAssetPath = null;
            string fallbackAssetPath = null;
            foreach (var guid in guids)
            {
                var assetPath = AssetDatabase.GUIDToAssetPath(guid);
                if (assetPath.EndsWith("Editor/Graph/node-manifest.json", StringComparison.Ordinal))
                    editorAssetPath = assetPath;
                else if (fallbackAssetPath == null)
                    fallbackAssetPath = assetPath;
            }

            var chosenAssetPath = editorAssetPath ?? fallbackAssetPath;
            if (string.IsNullOrEmpty(chosenAssetPath))
                return false;

            fullPath = Path.GetFullPath(Path.Combine(Application.dataPath, "..", chosenAssetPath));
            return File.Exists(fullPath);
        }

        private static void LoadFromString(string json)
        {
            try
            {
                var root = PcgMiniJson.Deserialize(json) as Dictionary<string, object>;
                if (root == null ||
                    !root.TryGetValue("nodes", out var nodesObj) ||
                    nodesObj is not List<object> nodesList)
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

            if (_byType.Count == 0)
                Debug.LogWarning($"[PCG] node-manifest loaded 0 nodes from {_loadedSourcePath}");

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

                        // Parse min/max for slider display (both required for ranged UI)
                        var hasMin = propObj.TryGetValue("minimum", out var minVal) && minVal != null;
                        var hasMax = propObj.TryGetValue("maximum", out var maxVal) && maxVal != null;
                        if (hasMin)
                            propDef.minimum = Convert.ToSingle(minVal, CultureInfo.InvariantCulture);
                        if (hasMax)
                            propDef.maximum = Convert.ToSingle(maxVal, CultureInfo.InvariantCulture);
                        propDef.hasRange = hasMin && hasMax;

                        // Parse group-related metadata
                        propDef.groupDomain = GetString(propObj, "groupDomain");
                        propDef.isGroupOutput = propObj.TryGetValue("isGroupOutput", out var groupOutVal)
                            && Convert.ToBoolean(groupOutVal, CultureInfo.InvariantCulture);

                        // Optional Inspector layout metadata (opt-in; missing → legacy UI path)
                        propDef.displayName = GetString(propObj, "displayName");
                        propDef.section = GetString(propObj, "section");
                        propDef.multiline = propObj.TryGetValue("multiline", out var multilineVal)
                            && Convert.ToBoolean(multilineVal, CultureInfo.InvariantCulture);
                        if (propObj.TryGetValue("lines", out var linesVal) && linesVal != null)
                            propDef.lines = Math.Max(1, Convert.ToInt32(linesVal, CultureInfo.InvariantCulture));
                        if (propObj.TryGetValue("order", out var orderVal) && orderVal != null)
                        {
                            propDef.hasOrder = true;
                            propDef.order = Convert.ToInt32(orderVal, CultureInfo.InvariantCulture);
                        }
                        if (propObj.TryGetValue("visibleWhen", out var visibleObj) &&
                            visibleObj is Dictionary<string, object> visibleDict)
                        {
                            propDef.visibleWhenProperty = GetString(visibleDict, "property");
                            propDef.visibleWhenEquals = GetString(visibleDict, "equals");
                            if (visibleDict.TryGetValue("oneOf", out var oneOfObj) &&
                                oneOfObj is List<object> oneOfList)
                            {
                                propDef.visibleWhenOneOf = new List<string>();
                                foreach (var item in oneOfList)
                                {
                                    if (item == null)
                                        continue;
                                    propDef.visibleWhenOneOf.Add(item.ToString());
                                }
                            }
                            if (visibleDict.TryGetValue("any", out var anyObj) &&
                                anyObj is List<object> anyList)
                            {
                                propDef.visibleWhenAny = new List<ManifestVisibleWhenClause>();
                                foreach (var item in anyList)
                                {
                                    if (item is not Dictionary<string, object> clauseDict)
                                        continue;
                                    var clause = new ManifestVisibleWhenClause
                                    {
                                        property = GetString(clauseDict, "property"),
                                        equals = GetString(clauseDict, "equals"),
                                    };
                                    if (clauseDict.TryGetValue("oneOf", out var clauseOneOfObj) &&
                                        clauseOneOfObj is List<object> clauseOneOfList)
                                    {
                                        clause.oneOf = new List<string>();
                                        foreach (var one in clauseOneOfList)
                                        {
                                            if (one == null)
                                                continue;
                                            clause.oneOf.Add(one.ToString());
                                        }
                                    }
                                    if (!string.IsNullOrEmpty(clause.property))
                                        propDef.visibleWhenAny.Add(clause);
                                }
                            }
                        }

                        if (propObj.TryGetValue("enabledWhen", out var enabledObj) &&
                            enabledObj is Dictionary<string, object> enabledDict)
                        {
                            propDef.enabledWhenProperty = GetString(enabledDict, "property");
                            propDef.enabledWhenEquals = GetString(enabledDict, "equals", "true");
                            if (enabledDict.TryGetValue("oneOf", out var enabledOneOfObj) &&
                                enabledOneOfObj is List<object> enabledOneOfList)
                            {
                                propDef.enabledWhenOneOf = new List<string>();
                                foreach (var one in enabledOneOfList)
                                {
                                    if (one == null)
                                        continue;
                                    propDef.enabledWhenOneOf.Add(one.ToString());
                                }
                            }
                            if (enabledDict.TryGetValue("all", out var enabledAllObj) &&
                                enabledAllObj is List<object> enabledAllList)
                            {
                                propDef.enabledWhenAll = new List<ManifestVisibleWhenClause>();
                                foreach (var clauseObj in enabledAllList)
                                {
                                    if (clauseObj is not Dictionary<string, object> clauseDict)
                                        continue;
                                    var clause = new ManifestVisibleWhenClause
                                    {
                                        property = GetString(clauseDict, "property"),
                                        equals = GetString(clauseDict, "equals", "true"),
                                    };
                                    if (clauseDict.TryGetValue("oneOf", out var clauseOneOfObj) &&
                                        clauseOneOfObj is List<object> clauseOneOfList)
                                    {
                                        clause.oneOf = new List<string>();
                                        foreach (var one in clauseOneOfList)
                                        {
                                            if (one == null)
                                                continue;
                                            clause.oneOf.Add(one.ToString());
                                        }
                                    }
                                    if (!string.IsNullOrEmpty(clause.property))
                                        propDef.enabledWhenAll.Add(clause);
                                }
                            }
                        }
                        propDef.companionField = GetString(propObj, "companionField");
                        propDef.indent = propObj.TryGetValue("indent", out var indentVal)
                            && Convert.ToBoolean(indentVal, CultureInfo.InvariantCulture);
                        propDef.rowGroup = GetString(propObj, "rowGroup");
                        propDef.rowPrefix = GetString(propObj, "rowPrefix");
                        if (propObj.TryGetValue("rowOrder", out var rowOrderVal) && rowOrderVal != null)
                        {
                            propDef.hasRowOrder = true;
                            propDef.rowOrder = Convert.ToInt32(rowOrderVal, CultureInfo.InvariantCulture);
                        }

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

            if (nodeDict.TryGetValue("inspectorSections", out var sectionsObj) &&
                sectionsObj is List<object> sectionsList)
            {
                foreach (var sectionObj in sectionsList)
                {
                    if (sectionObj is not Dictionary<string, object> sectionDict)
                        continue;
                    var section = new ManifestSectionDef
                    {
                        id = GetString(sectionDict, "id"),
                        label = GetString(sectionDict, "label", GetString(sectionDict, "id")),
                        foldout = !sectionDict.TryGetValue("foldout", out var foldoutVal)
                                  || Convert.ToBoolean(foldoutVal, CultureInfo.InvariantCulture),
                        defaultExpanded = !sectionDict.TryGetValue("defaultExpanded", out var expandedVal)
                                          || Convert.ToBoolean(expandedVal, CultureInfo.InvariantCulture),
                        header = sectionDict.TryGetValue("header", out var headerVal)
                                 && Convert.ToBoolean(headerVal, CultureInfo.InvariantCulture),
                    };
                    if (!string.IsNullOrEmpty(section.id))
                        def.inspectorSections.Add(section);
                }
            }

            // Parse outputGroups (groups this node produces on its output)
            if (nodeDict.TryGetValue("outputGroups", out var outputGroupsObj) && outputGroupsObj is List<object> outputGroupsList)
            {
                foreach (var ogObj in outputGroupsList)
                {
                    if (ogObj is not Dictionary<string, object> ogDict)
                        continue;
                    def.outputGroups.Add(new ManifestOutputGroupDef
                    {
                        name = GetString(ogDict, "name"),
                        domain = GetString(ogDict, "domain", "edge"),
                        label = GetString(ogDict, "label"),
                        condition = GetString(ogDict, "condition"),
                        dynamic = ogDict.TryGetValue("dynamic", out var dynVal) && Convert.ToBoolean(dynVal, CultureInfo.InvariantCulture),
                    });
                }
            }

            return def;
        }

        private static ManifestPinDef ParsePin(Dictionary<string, object> pin) => new()
        {
            id = GetString(pin, "id"),
            label = GetString(pin, "label", GetString(pin, "id")),
            pinType = GetString(pin, "pinType", "SpatialPoint"),
            variadic = pin.TryGetValue("variadic", out var v) && Convert.ToBoolean(v, CultureInfo.InvariantCulture),
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
                "vector3" => ParseVectorDefault(prop),
                _ => "",
            };
            }

            return type switch
            {
                "integer" => Convert.ToInt32(value, CultureInfo.InvariantCulture),
                "number" => Convert.ToSingle(value, CultureInfo.InvariantCulture),
                "boolean" => Convert.ToBoolean(value, CultureInfo.InvariantCulture),
                "enum" => value.ToString(),
                "vector3" => ParseVectorDefault(prop, value),
                _ => value.ToString(),
            };
        }

        private static object ParseVectorDefault(Dictionary<string, object> prop, object value = null)
        {
            if (value == null && prop.TryGetValue("default", out var defaultValue))
                value = defaultValue;
            return PcgVector3Property.NormalizeStored(value);
        }

        private static string GetString(Dictionary<string, object> dict, string key, string fallback = "") =>
            dict.TryGetValue(key, out var value) ? value?.ToString() ?? fallback : fallback;

        private static readonly Dictionary<string, Color> s_CategoryColors = new()
        {
            { "Generation", new Color(0.2f, 0.6f, 0.3f) },
            { "Filter", new Color(0.6f, 0.4f, 0.2f) },
            { "Transform", new Color(0.3f, 0.5f, 0.7f) },
            { "Sampler", new Color(0.5f, 0.3f, 0.6f) },
            { "Metadata", new Color(0.4f, 0.4f, 0.4f) },
            { "Spawner", new Color(0.6f, 0.5f, 0.2f) },
            { "Structural", new Color(0.3f, 0.3f, 0.5f) },
            { "Mesh", new Color(0.2f, 0.4f, 0.6f) },
            { "Geometry", new Color(0.2f, 0.5f, 0.4f) },
            { "Spline", new Color(0.3f, 0.5f, 0.4f) },
            { "Input", new Color(0.4f, 0.4f, 0.5f) },
            { "Output", new Color(0.35f, 0.35f, 0.35f) },
        };

        public static Color GetCategoryColor(string category) =>
            s_CategoryColors.TryGetValue(category ?? "", out var c) ? c : new Color(0.35f, 0.35f, 0.45f);
    }
}
