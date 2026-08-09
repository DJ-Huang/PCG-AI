using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>Project Shader picker and manifest-driven property UI for Material nodes.</summary>
    internal static class PcgMaterialShaderInspector
    {
        public static VisualElement Create(
            PcgManifestNodeView node,
            Action<Dictionary<string, object>, bool> persist)
        {
            var root = new VisualElement
            {
                style =
                {
                    marginTop = 4,
                    marginBottom = 8,
                    width = Length.Percent(100),
                },
            };

            var data = node.CollectData();
            var shaderGuid = data.GetRaw("unityShaderGuid")?.ToString() ?? "";
            var shaderName = data.GetRaw("unityShaderName")?.ToString() ?? "";
            var propertiesJson = data.GetRaw("unityPropertiesJson")?.ToString() ?? "{}";
            var shader = ResolveShader(shaderGuid, shaderName);

            var shaderField = new ObjectField("Project Shader")
            {
                objectType = typeof(Shader),
                allowSceneObjects = false,
                value = shader,
            };
            shaderField.RegisterValueChangedCallback(evt =>
            {
                var selected = evt.newValue as Shader;
                var path = selected != null ? AssetDatabase.GetAssetPath(selected) : "";
                var guid = string.IsNullOrEmpty(path) ? "" : AssetDatabase.AssetPathToGUID(path);
                var nextProperties = selected != null
                    ? ExtractProperties(selected, propertiesJson)
                    : "{}";
                persist(new Dictionary<string, object>
                {
                    ["unityShaderGuid"] = guid,
                    ["unityShaderName"] = selected != null ? selected.name : "",
                    ["unityPropertiesJson"] = nextProperties,
                }, true);
            });
            root.Add(shaderField);

            root.Add(new Label(shader != null
                ? "Unity uses this Shader. Web keeps the Standard PBR fields above as its fallback."
                : "Optional. When empty, Unity uses URP/Lit or Standard from the portable PBR fields above.")
            {
                style =
                {
                    color = new Color(0.55f, 0.68f, 0.78f),
                    fontSize = 10,
                    whiteSpace = WhiteSpace.Normal,
                    marginBottom = 6,
                },
            });

            if (shader == null)
                return root;

            var properties = ParseProperties(propertiesJson);
            if (properties.Count == 0)
            {
                propertiesJson = ExtractProperties(shader, "{}");
                properties = ParseProperties(propertiesJson);
            }

            if (properties.Count == 0)
            {
                root.Add(new Label("This Shader has no editable properties.")
                {
                    style = { color = new Color(0.55f, 0.55f, 0.55f), fontSize = 10 },
                });
                return root;
            }

            void Commit()
            {
                persist(new Dictionary<string, object>
                {
                    ["unityPropertiesJson"] = PcgMiniJson.Serialize(properties),
                }, false);
            }

            foreach (var (propertyName, raw) in properties)
            {
                if (raw is not Dictionary<string, object> property)
                    continue;
                root.Add(CreatePropertyField(propertyName, property, Commit));
            }

            return root;
        }

        internal static string ExtractProperties(Shader shader, string previousJson)
        {
            if (shader == null)
                return "{}";

            var previous = ParseProperties(previousJson);
            var result = new Dictionary<string, object>();
            var defaults = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            try
            {
                var count = ShaderUtil.GetPropertyCount(shader);
                for (var index = 0; index < count; index++)
                {
                    if (IsHidden(shader, index))
                        continue;

                    var name = ShaderUtil.GetPropertyName(shader, index);
                    var type = TypeName(ShaderUtil.GetPropertyType(shader, index));
                    if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(type))
                        continue;

                    Dictionary<string, object> entry;
                    if (previous.TryGetValue(name, out var previousValue) &&
                        previousValue is Dictionary<string, object> previousEntry &&
                        StringValue(previousEntry, "type", "") == type)
                    {
                        entry = new Dictionary<string, object>(previousEntry);
                    }
                    else
                    {
                        entry = new Dictionary<string, object>
                        {
                            ["type"] = type,
                            ["value"] = DefaultValue(defaults, name, type),
                        };
                    }

                    entry["displayName"] = ShaderUtil.GetPropertyDescription(shader, index);
                    if (type == "Range")
                    {
                        entry["min"] = ShaderUtil.GetRangeLimits(shader, index, 1);
                        entry["max"] = ShaderUtil.GetRangeLimits(shader, index, 2);
                    }
                    result[name] = entry;
                }
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(defaults);
            }

            return PcgMiniJson.Serialize(result);
        }

        private static VisualElement CreatePropertyField(
            string propertyName,
            Dictionary<string, object> property,
            Action commit)
        {
            var label = StringValue(property, "displayName", propertyName);
            var type = StringValue(property, "type", "");
            property.TryGetValue("value", out var rawValue);

            VisualElement field;
            switch (type)
            {
                case "Color":
                {
                    var colorField = new ColorField(label) { value = ReadColor(rawValue, Color.white) };
                    colorField.RegisterValueChangedCallback(evt =>
                    {
                        property["value"] = VectorValue(evt.newValue);
                        commit();
                    });
                    field = colorField;
                    break;
                }
                case "Vector":
                {
                    var vectorField = new Vector4Field(label) { value = ReadVector(rawValue, Vector4.zero) };
                    vectorField.RegisterValueChangedCallback(evt =>
                    {
                        property["value"] = VectorValue(evt.newValue);
                        commit();
                    });
                    field = vectorField;
                    break;
                }
                case "Range":
                {
                    var min = ConvertFloat(property.TryGetValue("min", out var minValue) ? minValue : null, 0f);
                    var max = ConvertFloat(property.TryGetValue("max", out var maxValue) ? maxValue : null, 1f);
                    var slider = new Slider(label, min, max) { value = ConvertFloat(rawValue, min), showInputField = true };
                    slider.RegisterValueChangedCallback(evt =>
                    {
                        property["value"] = evt.newValue;
                        commit();
                    });
                    field = slider;
                    break;
                }
                case "Int":
                {
                    var intField = new IntegerField(label) { value = Mathf.RoundToInt(ConvertFloat(rawValue, 0f)) };
                    intField.RegisterValueChangedCallback(evt =>
                    {
                        property["value"] = evt.newValue;
                        commit();
                    });
                    field = intField;
                    break;
                }
                case "Texture":
                {
                    var stored = rawValue?.ToString() ?? "";
                    var textureField = new ObjectField(label)
                    {
                        objectType = typeof(Texture),
                        allowSceneObjects = false,
                        value = LoadTextureAsset(stored),
                    };
                    textureField.RegisterValueChangedCallback(evt =>
                    {
                        property["value"] = TextureToStorageValue(evt.newValue as Texture);
                        commit();
                    });
                    field = textureField;
                    break;
                }
                default:
                {
                    var floatField = new FloatField(label) { value = ConvertFloat(rawValue, 0f) };
                    floatField.RegisterValueChangedCallback(evt =>
                    {
                        property["value"] = evt.newValue;
                        commit();
                    });
                    field = floatField;
                    break;
                }
            }

            field.style.marginBottom = 3;
            return field;
        }

        private static Shader ResolveShader(string guid, string name)
        {
            if (!string.IsNullOrEmpty(guid))
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                var asset = AssetDatabase.LoadAssetAtPath<Shader>(path);
                if (asset != null)
                    return asset;
            }
            return string.IsNullOrEmpty(name) ? null : Shader.Find(name);
        }

        private static bool IsHidden(Shader shader, int index)
        {
            return ShaderUtil.IsShaderPropertyHidden(shader, index);
        }

        private static string TypeName(ShaderUtil.ShaderPropertyType type)
        {
            return type switch
            {
                ShaderUtil.ShaderPropertyType.Color => "Color",
                ShaderUtil.ShaderPropertyType.Vector => "Vector",
                ShaderUtil.ShaderPropertyType.Float => "Float",
                ShaderUtil.ShaderPropertyType.Range => "Range",
                ShaderUtil.ShaderPropertyType.TexEnv => "Texture",
                ShaderUtil.ShaderPropertyType.Int => "Int",
                _ => "",
            };
        }

        private static object DefaultValue(Material material, string name, string type)
        {
            return type switch
            {
                "Color" => VectorValue(material.GetColor(name)),
                "Vector" => VectorValue(material.GetVector(name)),
                "Int" => material.GetInt(name),
                "Texture" => TextureToStorageValue(material.GetTexture(name)),
                _ => material.GetFloat(name),
            };
        }

        private static Texture LoadTextureAsset(string stored)
        {
            if (string.IsNullOrWhiteSpace(stored))
                return null;
            var path = PcgTextureGuidUtil.IsValidAssetGuid(stored)
                ? AssetDatabase.GUIDToAssetPath(stored)
                : stored;
            var texture = AssetDatabase.LoadAssetAtPath<Texture>(path);
            return texture != null ? texture : PcgTextureAssetUtil.LoadTextureFromStorage(stored);
        }

        private static string TextureToStorageValue(Texture texture)
        {
            if (texture == null)
                return "";
            var path = AssetDatabase.GetAssetPath(texture);
            if (!string.IsNullOrEmpty(path))
                return path.Replace('\\', '/');
            return texture is Texture2D texture2D
                ? PcgTextureAssetUtil.TextureToStorageValue(texture2D)
                : "";
        }

        private static Dictionary<string, object> ParseProperties(string json)
        {
            if (string.IsNullOrWhiteSpace(json))
                return new Dictionary<string, object>();
            try
            {
                return PcgMiniJson.Deserialize(json) as Dictionary<string, object>
                       ?? new Dictionary<string, object>();
            }
            catch (Exception)
            {
                return new Dictionary<string, object>();
            }
        }

        private static List<object> VectorValue(Color value) =>
            new() { value.r, value.g, value.b, value.a };

        private static List<object> VectorValue(Vector4 value) =>
            new() { value.x, value.y, value.z, value.w };

        private static Color ReadColor(object value, Color fallback)
        {
            var vector = ReadVector(value, new Vector4(fallback.r, fallback.g, fallback.b, fallback.a));
            return new Color(vector.x, vector.y, vector.z, vector.w);
        }

        private static Vector4 ReadVector(object value, Vector4 fallback)
        {
            if (value is not List<object> list || list.Count < 4)
                return fallback;
            return new Vector4(
                ConvertFloat(list[0], fallback.x),
                ConvertFloat(list[1], fallback.y),
                ConvertFloat(list[2], fallback.z),
                ConvertFloat(list[3], fallback.w));
        }

        private static float ConvertFloat(object value, float fallback)
        {
            try
            {
                return Convert.ToSingle(value, System.Globalization.CultureInfo.InvariantCulture);
            }
            catch (Exception)
            {
                return fallback;
            }
        }

        private static string StringValue(Dictionary<string, object> dictionary, string key, string fallback) =>
            dictionary.TryGetValue(key, out var value) && value != null ? value.ToString() : fallback;
    }
}
