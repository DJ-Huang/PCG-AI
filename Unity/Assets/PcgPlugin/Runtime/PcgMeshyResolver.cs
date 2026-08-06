using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Before cook: for each Meshy3DGenerator node, resolve a local GLB (user-saved
    /// Assets path preferred, else MeshyCache) and inject its absolute path into
    /// execution JSON. Never calls the Meshy API — Generate (+ Save) is explicit.
    /// </summary>
    public static class PcgMeshyResolver
    {
        public const string NodeType = "Meshy3DGenerator";

        public static bool TryPrepareForCook(ref string json, out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(json))
                return true;
            if (json.IndexOf(NodeType, StringComparison.Ordinal) < 0)
                return true;

#if !UNITY_EDITOR
            error = "Meshy3DGenerator requires the Unity Editor.";
            return false;
#else
            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out error))
                return false;

            var changed = false;
            if (!PrepareNodes(doc.nodes, ref changed, out error))
                return false;
            if (doc.subgraphs != null)
            {
                foreach (var subgraph in doc.subgraphs)
                {
                    if (subgraph == null)
                        continue;
                    if (!PrepareNodes(subgraph.nodes, ref changed, out error))
                        return false;
                }
            }

            if (changed)
                json = PcgGraphSerializer.ToJson(doc, pretty: false);
            return true;
#endif
        }

#if UNITY_EDITOR
        private static bool PrepareNodes(
            List<PcgGraphNodeRecord> nodes, ref bool changed, out string error)
        {
            error = null;
            if (nodes == null)
                return true;

            foreach (var node in nodes)
            {
                if (node == null || node.type != NodeType || string.IsNullOrEmpty(node.id))
                    continue;

                node.data ??= new PcgNodeData();
                if (!TryResolveModelForCook(node.id, node.data, out var absolutePath, out error))
                    return false;

                // Execution JSON always gets an absolute path (ImportMesh-compatible).
                // Authoring may keep a project-relative Assets/… path from Generate→Save.
                var previous = node.data.GetRaw("path")?.ToString() ?? "";
                if (!string.Equals(previous, absolutePath, StringComparison.Ordinal))
                {
                    node.data.SetRaw("path", absolutePath);
                    node.data.SetRaw("projectRoot", "");
                    changed = true;
                }
            }

            return true;
        }

        /// <summary>
        /// Prefer the user-saved model path, then the deterministic MeshyCache file.
        /// Never calls the API — Generate is the only cloud entry point.
        /// </summary>
        public static bool TryResolveModelForCook(
            string nodeId, PcgNodeData data, out string absolutePath, out string error)
        {
            error = null;
            data ??= new PcgNodeData();

            if (TryGetSavedModelPath(data, out absolutePath))
                return true;

            absolutePath = GetCachePath(
                BuildCacheKey(nodeId ?? "", data),
                PcgMeshySaveFormats.Primary(PcgMeshySaveFormats.ReadSelected(data)));

            if (File.Exists(absolutePath) && new FileInfo(absolutePath).Length > 0)
                return true;

            // Fall back across known formats for this cache key (e.g. FBX-only generate).
            foreach (var format in PcgMeshySaveFormats.Supported)
            {
                var candidate = GetCachePath(BuildCacheKey(nodeId ?? "", data), format);
                if (File.Exists(candidate) && new FileInfo(candidate).Length > 0)
                {
                    absolutePath = candidate;
                    return true;
                }
            }

            error =
                $"Meshy3DGenerator '{nodeId}': no saved/cached model. " +
                "Click Generate, save the model into the project, then cook/preview " +
                "(preview / auto cook never generate).";
            absolutePath = null;
            return false;
        }

        /// <summary>
        /// True when <c>path</c> resolves to an existing non-empty model file
        /// (project-relative Assets/… or absolute, including a prior MeshyCache path).
        /// </summary>
        public static bool TryGetSavedModelPath(PcgNodeData data, out string absolutePath)
        {
            absolutePath = null;
            data ??= new PcgNodeData();
            var stored = data.GetRaw("path")?.ToString()?.Trim();
            if (string.IsNullOrWhiteSpace(stored))
                return false;

            absolutePath = ResolveStoredAbsolutePath(stored, data.GetRaw("projectRoot")?.ToString());
            return !string.IsNullOrEmpty(absolutePath) &&
                   File.Exists(absolutePath) &&
                   new FileInfo(absolutePath).Length > 0;
        }

        public static string GetUnityProjectRoot()
        {
            var parent = Directory.GetParent(Application.dataPath);
            return parent != null ? Path.GetFullPath(parent.FullName) : Path.GetFullPath(Application.dataPath);
        }

        public static string ResolveProjectAbsolutePath(string projectRelativeOrAbsolute)
        {
            return ResolveStoredAbsolutePath(projectRelativeOrAbsolute, projectRoot: null);
        }

        private static string ResolveStoredAbsolutePath(string stored, string projectRoot)
        {
            if (string.IsNullOrWhiteSpace(stored))
                return null;

            stored = stored.Trim().Replace('\\', '/');
            if (Path.IsPathRooted(stored))
                return Path.GetFullPath(stored);

            if (string.IsNullOrWhiteSpace(projectRoot))
                projectRoot = GetUnityProjectRoot();
            return Path.GetFullPath(Path.Combine(projectRoot, stored.Replace('/', Path.DirectorySeparatorChar)));
        }

        /// <summary>
        /// Main-thread prep for an explicit Meshy generation (inspector Generate button):
        /// resolves the cache path, validates the API key and builds the request.
        /// Does NOT consult the local cache — callers decide whether an existing
        /// file is acceptable; an explicit Generate always calls the API again.
        /// </summary>
        public static bool TryBuildGenerateRequest(
            string nodeId,
            PcgNodeData data,
            out PcgMeshyClient.GenerateRequest request,
            out string absolutePath,
            out string error)
        {
            request = default;
            error = null;
            data ??= new PcgNodeData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            absolutePath = GetCachePath(
                BuildCacheKey(nodeId ?? "", data),
                PcgMeshySaveFormats.Primary(formats));

            if (!PcgMeshySettings.HasApiKey)
            {
                error =
                    $"Meshy3DGenerator '{nodeId}': API key missing. " +
                    "Open PCG → Settings (or Project Settings → PCG AI) and enter your Meshy API key.";
                return false;
            }

            if (!TryBuildImageDataUri(nodeId, data, out var dataUri, out error))
                return false;

            request = new PcgMeshyClient.GenerateRequest
            {
                ImageDataUri = dataUri,
                AiModel = data.GetRaw("aiModel")?.ToString() ?? "latest",
                EnablePbr = ReadBool(data, "enablePbr", false),
                ShouldTexture = ReadBool(data, "shouldTexture", true),
                ShouldRemesh = ReadBool(data, "shouldRemesh", true),
                TargetPolycount = ReadInt(data, "targetPolycount", 30000),
                TargetFormats = formats,
            };
            return true;
        }

        /// <summary>Resolves the deterministic cache path and reports whether a valid file exists.</summary>
        public static bool TryGetCachedModelPath(string nodeId, PcgNodeData data, out string absolutePath)
        {
            data ??= new PcgNodeData();
            var key = BuildCacheKey(nodeId ?? "", data);
            absolutePath = GetCachePath(key, PcgMeshySaveFormats.Primary(PcgMeshySaveFormats.ReadSelected(data)));
            if (File.Exists(absolutePath) && new FileInfo(absolutePath).Length > 0)
                return true;
            foreach (var format in PcgMeshySaveFormats.Supported)
            {
                var candidate = GetCachePath(key, format);
                if (File.Exists(candidate) && new FileInfo(candidate).Length > 0)
                {
                    absolutePath = candidate;
                    return true;
                }
            }
            return false;
        }

        private static bool TryBuildImageDataUri(
            string nodeId, PcgNodeData data, out string dataUri, out string error)
        {
            dataUri = null;
            error = null;

            var imageUrl = data?.GetRaw("imageUrl")?.ToString()?.Trim();
            if (!string.IsNullOrWhiteSpace(imageUrl) &&
                (imageUrl.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
                 imageUrl.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ||
                 imageUrl.StartsWith("data:", StringComparison.OrdinalIgnoreCase)))
            {
                dataUri = imageUrl;
                return true;
            }

            var stored = data?.GetRaw("texture")?.ToString();
            if (string.IsNullOrWhiteSpace(stored))
            {
                error =
                    $"Meshy3DGenerator '{nodeId}': assign a Source Image (texture) " +
                    "or set imageUrl to a public / data URI.";
                return false;
            }

            var source = PcgTextureAssetUtil.LoadTextureFromStorage(stored);
            if (source == null)
            {
                error = $"Meshy3DGenerator '{nodeId}': failed to load texture '{stored}'.";
                return false;
            }

            var readable = EnsureReadable(source);
            try
            {
                var png = readable.EncodeToPNG();
                if (png == null || png.Length == 0)
                {
                    error = $"Meshy3DGenerator '{nodeId}': EncodeToPNG failed.";
                    return false;
                }

                dataUri = "data:image/png;base64," + Convert.ToBase64String(png);
                return true;
            }
            finally
            {
                if (readable != source)
                    UnityEngine.Object.DestroyImmediate(readable);
            }
        }

        /// <summary>
        /// EncodeToPNG requires uncompressed CPU pixels. Project textures are often
        /// GPU-compressed (DXT/ASTC/…) even when isReadable is true — blit those too.
        /// </summary>
        private static Texture2D EnsureReadable(Texture2D source)
        {
            if (source.isReadable && CanEncodeToPng(source.format))
                return source;

            var rt = RenderTexture.GetTemporary(
                source.width, source.height, 0, RenderTextureFormat.ARGB32);
            var prev = RenderTexture.active;
            try
            {
                Graphics.Blit(source, rt);
                RenderTexture.active = rt;
                var copy = new Texture2D(source.width, source.height, TextureFormat.RGBA32, false);
                copy.ReadPixels(new Rect(0, 0, source.width, source.height), 0, 0);
                copy.Apply();
                return copy;
            }
            finally
            {
                RenderTexture.active = prev;
                RenderTexture.ReleaseTemporary(rt);
            }
        }

        private static bool CanEncodeToPng(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat.ARGB32:
                case TextureFormat.RGBA32:
                case TextureFormat.RGB24:
                case TextureFormat.Alpha8:
                    return true;
                default:
                    return false;
            }
        }

        private static string BuildCacheKey(string nodeId, PcgNodeData data)
        {
            var sb = new StringBuilder(256);
            sb.Append(nodeId).Append('|');
            sb.Append(data?.GetRaw("texture")?.ToString() ?? "").Append('|');
            sb.Append(data?.GetRaw("imageUrl")?.ToString() ?? "").Append('|');
            sb.Append(data?.GetRaw("aiModel")?.ToString() ?? "latest").Append('|');
            sb.Append(ReadBool(data, "enablePbr", false)).Append('|');
            sb.Append(ReadBool(data, "shouldTexture", true)).Append('|');
            sb.Append(ReadBool(data, "shouldRemesh", true)).Append('|');
            sb.Append(ReadInt(data, "targetPolycount", 30000));

            // Include source image bytes so texture edits invalidate the cache.
            var stored = data?.GetRaw("texture")?.ToString();
            if (!string.IsNullOrWhiteSpace(stored))
            {
                var tex = PcgTextureAssetUtil.LoadTextureFromStorage(stored);
                if (tex != null)
                {
                    var readable = EnsureReadable(tex);
                    try
                    {
                        var png = readable.EncodeToPNG();
                        if (png != null)
                            sb.Append('|').Append(Convert.ToBase64String(ComputeSha256(png)));
                    }
                    finally
                    {
                        if (readable != tex)
                            UnityEngine.Object.DestroyImmediate(readable);
                    }
                }
            }

            var hash = ComputeSha256(Encoding.UTF8.GetBytes(sb.ToString()));
            var hex = new StringBuilder(hash.Length * 2);
            foreach (var b in hash)
                hex.Append(b.ToString("x2"));
            return hex.ToString();
        }

        private static byte[] ComputeSha256(byte[] bytes)
        {
            using var sha = SHA256.Create();
            return sha.ComputeHash(bytes);
        }

        private static string GetCachePath(string cacheKey, string format = PcgMeshySaveFormats.Glb)
        {
            if (string.IsNullOrWhiteSpace(format))
                format = PcgMeshySaveFormats.Glb;
            format = format.Trim().TrimStart('.').ToLowerInvariant();
            var root = Path.Combine(Application.dataPath, "..", "Library", "PCG", "MeshyCache");
            return Path.GetFullPath(Path.Combine(root, cacheKey + "." + format));
        }

        private static bool ReadBool(PcgNodeData data, string key, bool fallback)
        {
            var raw = data?.GetRaw(key);
            if (raw == null)
                return fallback;
            if (raw is bool b)
                return b;
            return bool.TryParse(raw.ToString(), out var parsed) ? parsed : fallback;
        }

        private static int ReadInt(PcgNodeData data, string key, int fallback)
        {
            var raw = data?.GetRaw(key);
            if (raw == null)
                return fallback;
            if (raw is int i)
                return i;
            if (raw is long l)
                return (int)l;
            if (raw is double d)
                return (int)d;
            if (raw is float f)
                return (int)f;
            return int.TryParse(raw.ToString(), out var parsed) ? parsed : fallback;
        }
#endif
    }
}
