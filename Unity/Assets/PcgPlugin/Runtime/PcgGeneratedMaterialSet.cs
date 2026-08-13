using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Transient Unity materials generated from Material-node definitions in cook metadata.
    /// Explicit PcgMaterialBinding entries are resolved separately and always take precedence.
    /// </summary>
    internal sealed class PcgGeneratedMaterialSet : IDisposable
    {
        private readonly Dictionary<string, Material> m_Materials = new();
        private readonly Dictionary<string, Texture> m_TextureCache = new();
        private readonly List<UnityEngine.Object> m_OwnedObjects = new();

        public IReadOnlyDictionary<string, Material> Materials => m_Materials;

        public void Rebuild(string resultJson)
        {
            Dispose();
            foreach (var (name, definition) in ParseDefinitions(resultJson))
            {
                var material = CreateMaterial(name, definition);
                if (material != null)
                    m_Materials[name] = material;
            }
        }

        public bool TryGet(string name, out Material material) =>
            m_Materials.TryGetValue(name ?? string.Empty, out material) && material != null;

        public void Dispose()
        {
            foreach (var material in m_Materials.Values)
                DestroyObject(material);
            m_Materials.Clear();

            foreach (var owned in m_OwnedObjects)
                DestroyObject(owned);
            m_OwnedObjects.Clear();
            m_TextureCache.Clear();
        }

        internal static Dictionary<string, Dictionary<string, object>> ParseDefinitions(string resultJson)
        {
            var definitions = new Dictionary<string, Dictionary<string, object>>();
            if (string.IsNullOrWhiteSpace(resultJson))
                return definitions;

            try
            {
                if (PcgMiniJson.Deserialize(resultJson) is not Dictionary<string, object> root)
                    return definitions;

                Dictionary<string, object> rawLibrary = null;
                if (root.TryGetValue("materials", out var topLevel) &&
                    topLevel is Dictionary<string, object> topLevelLibrary)
                {
                    rawLibrary = topLevelLibrary;
                }
                else if (root.TryGetValue("mesh_metadata", out var metadataValue) &&
                         metadataValue is Dictionary<string, object> metadata &&
                         metadata.TryGetValue("pbrMaterials", out var libraryValue) &&
                         libraryValue is Dictionary<string, object> metadataLibrary)
                {
                    rawLibrary = metadataLibrary;
                }

                if (rawLibrary == null)
                    return definitions;

                foreach (var (name, rawDefinition) in rawLibrary)
                {
                    if (!string.IsNullOrEmpty(name) && rawDefinition is Dictionary<string, object> definition)
                        definitions[name] = definition;
                }
            }
            catch (Exception)
            {
                // Cook JSON may describe a non-mesh result. Treat malformed/absent metadata as empty.
            }

            return definitions;
        }

        private Material CreateMaterial(string slotName, Dictionary<string, object> definition)
        {
            var shader = ResolveShader(definition);
            if (shader == null)
                return null;

            var material = new Material(shader)
            {
                name = $"PCG Generated - {StringValue(definition, "name", slotName)}",
                hideFlags = HideFlags.HideAndDontSave,
            };

            ApplyPortablePbr(material, definition);
            ApplyUnityProperties(material, StringValue(definition, "unityPropertiesJson", "{}"));
            return material;
        }

        private static Shader ResolveShader(Dictionary<string, object> definition)
        {
            var guid = StringValue(definition, "unityShaderGuid", "");
#if UNITY_EDITOR
            if (!string.IsNullOrEmpty(guid))
            {
                var path = UnityEditor.AssetDatabase.GUIDToAssetPath(guid);
                var fromGuid = UnityEditor.AssetDatabase.LoadAssetAtPath<Shader>(path);
                if (fromGuid != null)
                    return fromGuid;
            }
#endif
            var shaderName = StringValue(definition, "unityShaderName", "");
            if (!string.IsNullOrEmpty(shaderName))
            {
                var custom = Shader.Find(shaderName);
                if (custom != null)
                    return custom;
            }

            return Shader.Find("Universal Render Pipeline/Lit")
                   ?? Shader.Find("Standard")
                   ?? Shader.Find("Hidden/InternalErrorShader");
        }

        private void ApplyPortablePbr(Material material, Dictionary<string, object> definition)
        {
            var baseColor = ParseColor(StringValue(definition, "baseColor", "#b8c2cc"),
                new Color(0.72f, 0.76f, 0.8f, 1f));
            var opacity = FloatValue(definition, "opacity", 1f, 0f, 1f);
            baseColor.a *= opacity;
            SetColor(material, baseColor, "_BaseColor", "_Color");

            var baseMap = LoadTexture(StringValue(definition, "baseColorMap", ""));
            SetTexture(material, baseMap, "_BaseMap", "_MainTex");

            var metallic = FloatValue(definition, "metallic", 0f, 0f, 1f);
            SetFloat(material, metallic, "_Metallic");
            var metallicMap = LoadTexture(StringValue(definition, "metallicMap", ""));
            SetTexture(material, metallicMap, "_MetallicGlossMap", "_MetallicMap");
            SetKeyword(material, "_METALLICSPECGLOSSMAP", metallicMap != null);

            var roughness = FloatValue(definition, "roughness", 0.5f, 0f, 1f);
            SetFloat(material, roughness, "_Roughness");
            SetFloat(material, 1f - roughness, "_Smoothness", "_Glossiness");
            var roughnessMap = LoadTexture(StringValue(definition, "roughnessMap", ""));
            SetTexture(material, roughnessMap, "_RoughnessMap");

            var normalMap = LoadTexture(StringValue(definition, "normalMap", ""));
            SetTexture(material, normalMap, "_BumpMap", "_NormalMap");
            SetFloat(material, FloatValue(definition, "normalScale", 1f, 0f, 4f), "_BumpScale", "_NormalScale");
            SetKeyword(material, "_NORMALMAP", normalMap != null);

            var aoMap = LoadTexture(StringValue(definition, "aoMap", ""));
            SetTexture(material, aoMap, "_OcclusionMap", "_AOMap");
            SetFloat(material, FloatValue(definition, "aoIntensity", 1f, 0f, 4f), "_OcclusionStrength", "_AOIntensity");
            SetKeyword(material, "_OCCLUSIONMAP", aoMap != null);

            var emissiveColor = ParseColor(StringValue(definition, "emissiveColor", "#000000"), Color.black);
            emissiveColor *= FloatValue(definition, "emissiveIntensity", 0f, 0f, 16f);
            var emissiveMap = LoadTexture(StringValue(definition, "emissiveMap", ""));
            SetColor(material, emissiveColor, "_EmissionColor", "_EmissiveColor");
            SetTexture(material, emissiveMap, "_EmissionMap", "_EmissiveMap");
            SetKeyword(material, "_EMISSION", emissiveMap != null || emissiveColor.maxColorComponent > 0f);

            var doubleSided = BoolValue(definition, "doubleSided", false);
            SetFloat(material, doubleSided ? (float)CullMode.Off : (float)CullMode.Back, "_Cull");
            material.doubleSidedGI = doubleSided;

            ApplyAlphaMode(material, StringValue(definition, "alphaMode", "opaque"),
                FloatValue(definition, "alphaCutoff", 0.5f, 0f, 1f));
        }

        private void ApplyUnityProperties(Material material, string propertiesJson)
        {
            if (string.IsNullOrWhiteSpace(propertiesJson))
                return;

            Dictionary<string, object> properties;
            try
            {
                properties = PcgMiniJson.Deserialize(propertiesJson) as Dictionary<string, object>;
            }
            catch (Exception)
            {
                return;
            }

            if (properties == null)
                return;

            foreach (var (propertyName, rawProperty) in properties)
            {
                if (!material.HasProperty(propertyName) ||
                    rawProperty is not Dictionary<string, object> property)
                    continue;

                var type = StringValue(property, "type", "");
                property.TryGetValue("value", out var value);
                switch (type)
                {
                    case "Color":
                        if (TryReadVector(value, out var color))
                            material.SetColor(propertyName, new Color(color.x, color.y, color.z, color.w));
                        break;
                    case "Vector":
                        if (TryReadVector(value, out var vector))
                            material.SetVector(propertyName, vector);
                        break;
                    case "Int":
                        material.SetInt(propertyName, Mathf.RoundToInt(ConvertFloat(value, 0f)));
                        break;
                    case "Float":
                    case "Range":
                        material.SetFloat(propertyName, ConvertFloat(value, 0f));
                        break;
                    case "Texture":
                    case "TexEnv":
                        var texture = LoadTexture(value?.ToString() ?? StringValue(property, "storage", ""));
                        if (texture != null)
                            material.SetTexture(propertyName, texture);
                        break;
                }
            }
        }

        private Texture LoadTexture(string storage)
        {
            if (string.IsNullOrWhiteSpace(storage))
                return null;
            if (m_TextureCache.TryGetValue(storage, out var cached))
                return cached;

            Texture texture = null;
#if UNITY_EDITOR
            texture = PcgTextureAssetUtil.LoadTextureFromStorage(storage);
            if (texture == null)
            {
                var path = PcgTextureGuidUtil.IsValidAssetGuid(storage)
                    ? UnityEditor.AssetDatabase.GUIDToAssetPath(storage)
                    : storage;
                texture = UnityEditor.AssetDatabase.LoadAssetAtPath<Texture>(path);
            }
#endif
            if (texture == null && storage.StartsWith("data:image/", StringComparison.OrdinalIgnoreCase))
                texture = LoadDataUriTexture(storage);
            if (texture == null)
                texture = LoadResourcesTexture(storage);

            m_TextureCache[storage] = texture;
            return texture;
        }

        private Texture2D LoadDataUriTexture(string uri)
        {
            var marker = uri.IndexOf(",", StringComparison.Ordinal);
            if (marker < 0 || uri.IndexOf(";base64", 0, marker, StringComparison.OrdinalIgnoreCase) < 0)
                return null;
            try
            {
                var bytes = Convert.FromBase64String(uri.Substring(marker + 1));
                var texture = new Texture2D(2, 2, TextureFormat.RGBA32, true)
                {
                    name = "PCG Data URI Texture",
                    hideFlags = HideFlags.HideAndDontSave,
                };
                if (!texture.LoadImage(bytes, false))
                {
                    DestroyObject(texture);
                    return null;
                }
                m_OwnedObjects.Add(texture);
                return texture;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static Texture2D LoadResourcesTexture(string storage)
        {
            if (PcgTextureAssetUtil.TryGetPortableResourceKey(storage, out var portableResourceKey))
                return Resources.Load<Texture2D>(portableResourceKey);

            var normalized = storage.Replace('\\', '/');
            var resourcesIndex = normalized.IndexOf("/Resources/", StringComparison.Ordinal);
            if (resourcesIndex >= 0)
                normalized = normalized.Substring(resourcesIndex + "/Resources/".Length);
            else if (normalized.StartsWith("Resources/", StringComparison.Ordinal))
                normalized = normalized.Substring("Resources/".Length);
            else
                return null;

            var extension = normalized.LastIndexOf('.');
            if (extension > normalized.LastIndexOf('/'))
                normalized = normalized.Substring(0, extension);
            return Resources.Load<Texture2D>(normalized);
        }

        private static void ApplyAlphaMode(Material material, string mode, float cutoff)
        {
            var blend = string.Equals(mode, "blend", StringComparison.OrdinalIgnoreCase);
            var mask = string.Equals(mode, "mask", StringComparison.OrdinalIgnoreCase);
            SetFloat(material, mask ? 1f : 0f, "_AlphaClip");
            SetFloat(material, cutoff, "_Cutoff");
            SetKeyword(material, "_ALPHATEST_ON", mask);

            if (material.HasProperty("_Surface"))
            {
                material.SetFloat("_Surface", blend ? 1f : 0f);
                SetKeyword(material, "_SURFACE_TYPE_TRANSPARENT", blend);
            }
            if (material.HasProperty("_Mode"))
                material.SetFloat("_Mode", blend ? 3f : mask ? 1f : 0f);

            if (blend)
            {
                SetFloat(material, (float)BlendMode.SrcAlpha, "_SrcBlend");
                SetFloat(material, (float)BlendMode.OneMinusSrcAlpha, "_DstBlend");
                SetFloat(material, 0f, "_ZWrite");
                SetKeyword(material, "_ALPHABLEND_ON", true);
                material.renderQueue = (int)RenderQueue.Transparent;
            }
            else
            {
                SetFloat(material, (float)BlendMode.One, "_SrcBlend");
                SetFloat(material, (float)BlendMode.Zero, "_DstBlend");
                SetFloat(material, 1f, "_ZWrite");
                SetKeyword(material, "_ALPHABLEND_ON", false);
                material.renderQueue = mask ? (int)RenderQueue.AlphaTest : -1;
            }
        }

        private static void SetColor(Material material, Color value, params string[] propertyNames)
        {
            foreach (var propertyName in propertyNames)
                if (material.HasProperty(propertyName))
                    material.SetColor(propertyName, value);
        }

        private static void SetFloat(Material material, float value, params string[] propertyNames)
        {
            foreach (var propertyName in propertyNames)
                if (material.HasProperty(propertyName))
                    material.SetFloat(propertyName, value);
        }

        private static void SetTexture(Material material, Texture value, params string[] propertyNames)
        {
            if (value == null)
                return;
            foreach (var propertyName in propertyNames)
                if (material.HasProperty(propertyName))
                    material.SetTexture(propertyName, value);
        }

        private static void SetKeyword(Material material, string keyword, bool enabled)
        {
            if (enabled)
                material.EnableKeyword(keyword);
            else
                material.DisableKeyword(keyword);
        }

        private static string StringValue(Dictionary<string, object> dictionary, string key, string fallback)
        {
            return dictionary.TryGetValue(key, out var value) && value != null
                ? value.ToString()
                : fallback;
        }

        private static float FloatValue(
            Dictionary<string, object> dictionary, string key, float fallback, float minimum, float maximum)
        {
            return dictionary.TryGetValue(key, out var value)
                ? Mathf.Clamp(ConvertFloat(value, fallback), minimum, maximum)
                : fallback;
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

        private static bool BoolValue(Dictionary<string, object> dictionary, string key, bool fallback)
        {
            if (!dictionary.TryGetValue(key, out var value) || value == null)
                return fallback;
            if (value is bool boolean)
                return boolean;
            return bool.TryParse(value.ToString(), out var parsed) ? parsed : fallback;
        }

        private static Color ParseColor(string html, Color fallback)
        {
            return ColorUtility.TryParseHtmlString(html, out var color) ? color : fallback;
        }

        private static bool TryReadVector(object value, out Vector4 vector)
        {
            vector = Vector4.zero;
            if (value is not List<object> entries || entries.Count < 4)
                return false;
            vector = new Vector4(
                ConvertFloat(entries[0], 0f),
                ConvertFloat(entries[1], 0f),
                ConvertFloat(entries[2], 0f),
                ConvertFloat(entries[3], 0f));
            return true;
        }

        private static void DestroyObject(UnityEngine.Object value)
        {
            if (value == null)
                return;
            if (Application.isPlaying)
                UnityEngine.Object.Destroy(value);
            else
                UnityEngine.Object.DestroyImmediate(value);
        }
    }
}
