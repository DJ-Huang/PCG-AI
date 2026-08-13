using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Texture storage accepts a portable pcg-resource locator, an AssetDatabase path, or a legacy GUID.
    /// Built-in textures (Default-Particle, etc.) use Resources/*_builtin_extra/TextureName because GetAssetPath only returns the container.
    /// </summary>
    public static class PcgTextureAssetUtil
    {
        public const string PortableResourcePrefix = "pcg-resource://";
        public const string WebAssetPrefix = "/assets/";

        private static readonly string[] BuiltinExtraContainers =
        {
            "Resources/tuanjie_builtin_extra",
            "Resources/unity_builtin_extra",
        };

        public static bool IsValidStorage(string stored)
        {
            if (string.IsNullOrWhiteSpace(stored))
                return false;

            if (PcgTextureGuidUtil.IsValidAssetGuid(stored))
                return true;

            if (stored.StartsWith(PortableResourcePrefix, StringComparison.OrdinalIgnoreCase) ||
                stored.StartsWith(WebAssetPrefix, StringComparison.OrdinalIgnoreCase))
                return TryGetPortableResourceKey(stored, out _);

            // Any path returned by AssetDatabase.GetAssetPath (incl. Resources/tuanjie_builtin_extra/Name).
            return stored.IndexOf('/') >= 0;
        }

#if UNITY_EDITOR
        public static string TextureToStorageValue(Texture2D texture)
        {
            if (texture == null)
                return "";

            var path = UnityEditor.AssetDatabase.GetAssetPath(texture);
            if (string.IsNullOrEmpty(path))
            {
                var builtinKey = TryFindBuiltinExtraStorageKey(texture);
                if (!string.IsNullOrEmpty(builtinKey))
                    return builtinKey;

                if (!UnityEditor.EditorUtility.IsPersistent(texture))
                {
                    Debug.LogWarning(
                        "[PCG] ImageTexture requires a project or built-in Texture2D (not a scene instance).");
                }
                return "";
            }

            var normalized = path.Replace('\\', '/');
            if (IsBuiltinExtraContainer(normalized))
                return normalized + "/" + texture.name;

            return normalized;
        }

        public static Texture2D LoadTextureFromStorage(string stored)
        {
            if (string.IsNullOrWhiteSpace(stored))
                return null;

            var normalized = stored.Replace('\\', '/');

            if (TryGetPortableResourceKey(normalized, out var resourceKey))
            {
                var fromPortableResource = Resources.Load<Texture2D>(resourceKey);
                if (fromPortableResource != null)
                    return fromPortableResource;
            }

            if (PcgTextureGuidUtil.IsValidAssetGuid(normalized))
            {
                var guidPath = UnityEditor.AssetDatabase.GUIDToAssetPath(normalized);
                var fromGuid = LoadAtPathCandidates(guidPath);
                if (fromGuid != null)
                    return fromGuid;
            }

            if (TryParseBuiltinExtraPath(normalized, out var container, out var textureName))
            {
                var fromBuiltin = LoadFromBuiltinExtraContainer(container, textureName);
                if (fromBuiltin != null)
                    return fromBuiltin;
            }

            var direct = LoadAtPathCandidates(normalized);
            if (direct != null)
                return direct;

            if (normalized.StartsWith("Resources/", StringComparison.Ordinal))
            {
                var resourcePath = normalized.Substring("Resources/".Length);
                var fromResources = Resources.Load<Texture2D>(resourcePath);
                if (fromResources != null)
                    return fromResources;
            }

            var fileName = Path.GetFileNameWithoutExtension(normalized);
            if (!string.IsNullOrEmpty(fileName))
            {
                foreach (var guid in UnityEditor.AssetDatabase.FindAssets(fileName + " t:Texture2D"))
                {
                    var candidatePath = UnityEditor.AssetDatabase.GUIDToAssetPath(guid);
                    if (string.IsNullOrEmpty(candidatePath))
                        continue;
                    if (!candidatePath.Replace('\\', '/').EndsWith(normalized, StringComparison.OrdinalIgnoreCase) &&
                        Path.GetFileNameWithoutExtension(candidatePath) != fileName)
                        continue;

                    var found = UnityEditor.AssetDatabase.LoadAssetAtPath<Texture2D>(candidatePath);
                    if (found != null)
                        return found;
                }
            }

            return null;
        }

        public static bool TryGetPortableResourceKey(string stored, out string resourceKey)
        {
            resourceKey = null;
            if (string.IsNullOrWhiteSpace(stored))
                return false;

            string normalized;
            if (stored.StartsWith(PortableResourcePrefix, StringComparison.OrdinalIgnoreCase))
                normalized = stored.Substring(PortableResourcePrefix.Length);
            else if (stored.StartsWith(WebAssetPrefix, StringComparison.OrdinalIgnoreCase))
                normalized = stored.Substring(WebAssetPrefix.Length);
            else
                return false;

            normalized = normalized
                .Replace('\\', '/')
                .TrimStart('/');
            var extension = normalized.LastIndexOf('.');
            if (extension > normalized.LastIndexOf('/'))
                normalized = normalized.Substring(0, extension);
            if (string.IsNullOrWhiteSpace(normalized) || HasParentSegment(normalized))
                return false;

            resourceKey = normalized;
            return true;
        }

        private static bool HasParentSegment(string path)
        {
            foreach (var segment in path.Split('/'))
                if (segment == "..")
                    return true;
            return false;
        }

        public static string StorageToProjectPath(string stored)
        {
            if (string.IsNullOrWhiteSpace(stored))
                return null;

            var normalized = stored.Replace('\\', '/');
            if (LoadTextureFromStorage(normalized) != null)
                return normalized;

            return null;
        }

        private static bool IsBuiltinExtraContainer(string path)
        {
            if (string.IsNullOrEmpty(path))
                return false;

            var normalized = path.Replace('\\', '/').TrimEnd('/');
            if (!normalized.StartsWith("Resources/", StringComparison.Ordinal))
                return false;

            return normalized.EndsWith("_builtin_extra", StringComparison.Ordinal);
        }

        private static bool TryParseBuiltinExtraPath(string normalized, out string container, out string textureName)
        {
            container = null;
            textureName = null;

            var slash = normalized.LastIndexOf('/');
            if (slash <= 0)
                return false;

            container = normalized.Substring(0, slash);
            textureName = normalized.Substring(slash + 1);
            if (!IsBuiltinExtraContainer(container) || string.IsNullOrEmpty(textureName))
                return false;

            return true;
        }

        private static string TryFindBuiltinExtraStorageKey(Texture2D texture)
        {
            if (texture == null)
                return null;

            foreach (var container in BuiltinExtraContainers)
            {
                foreach (var obj in UnityEditor.AssetDatabase.LoadAllAssetsAtPath(container))
                {
                    if (ReferenceEquals(obj, texture))
                        return container + "/" + texture.name;
                }
            }

            return null;
        }

        private static Texture2D LoadFromBuiltinExtraContainer(string containerPath, string textureName)
        {
            if (string.IsNullOrEmpty(containerPath) || string.IsNullOrEmpty(textureName))
                return null;

            var container = containerPath.Replace('\\', '/').TrimEnd('/');

            foreach (var obj in UnityEditor.AssetDatabase.LoadAllAssetsAtPath(container))
            {
                if (obj is Texture2D tex &&
                    string.Equals(tex.name, textureName, StringComparison.Ordinal))
                    return tex;
            }

            foreach (var ext in new[] { ".psd", ".png", ".tif", ".tga", ".jpg", "" })
            {
                var builtin = UnityEditor.AssetDatabase.GetBuiltinExtraResource<Texture2D>(textureName + ext);
                if (builtin != null &&
                    string.Equals(builtin.name, textureName, StringComparison.OrdinalIgnoreCase))
                    return builtin;
            }

            if (container.StartsWith("Resources/", StringComparison.Ordinal))
            {
                var resourceKey = container.Substring("Resources/".Length) + "/" + textureName;
                var fromResources = Resources.Load<Texture2D>(resourceKey);
                if (fromResources != null)
                    return fromResources;
            }

            return null;
        }

        private static Texture2D LoadAtPathCandidates(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return null;

            var normalized = path.Replace('\\', '/');
            var candidates = new List<string> { normalized };

            if (!normalized.StartsWith("Assets/", StringComparison.Ordinal) &&
                !normalized.StartsWith("Packages/", StringComparison.Ordinal))
            {
                candidates.Add("Assets/" + normalized);
            }

            foreach (var candidate in candidates)
            {
                var tex = UnityEditor.AssetDatabase.LoadAssetAtPath<Texture2D>(candidate);
                if (tex != null)
                    return tex;
            }

            return null;
        }
#else
        public static Texture2D LoadTextureFromStorage(string stored) => null;
        public static string StorageToProjectPath(string stored) =>
            string.IsNullOrWhiteSpace(stored) ? null : stored.Replace('\\', '/');
#endif
    }
}
