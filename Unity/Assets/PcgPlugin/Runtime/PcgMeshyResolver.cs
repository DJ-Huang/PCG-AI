using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Before cook: for each Meshy3DGenerator node, ensure a cached GLB exists and
    /// inject its absolute path into execution JSON (ImportMesh-compatible).
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
                if (!TryEnsureCachedModel(node, out var absolutePath, out error))
                    return false;

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

        private static bool TryEnsureCachedModel(
            PcgGraphNodeRecord node, out string absolutePath, out string error)
        {
            absolutePath = null;
            error = null;

            var force = ReadBool(node.data, "forceRegenerate", false);
            var cacheKey = BuildCacheKey(node);
            absolutePath = GetCachePath(cacheKey);

            if (!force && File.Exists(absolutePath) && new FileInfo(absolutePath).Length > 0)
                return true;

            if (!PcgMeshySettings.HasApiKey)
            {
                error =
                    $"Meshy3DGenerator '{node.id}': API key missing. " +
                    "Open PCG → Settings (or Project Settings → PCG AI) and enter your Meshy API key.";
                return false;
            }

            if (!TryBuildImageDataUri(node, out var dataUri, out error))
                return false;

            var request = new PcgMeshyClient.GenerateRequest
            {
                ImageDataUri = dataUri,
                AiModel = node.data.GetRaw("aiModel")?.ToString() ?? "latest",
                EnablePbr = ReadBool(node.data, "enablePbr", false),
                ShouldTexture = ReadBool(node.data, "shouldTexture", true),
                ShouldRemesh = ReadBool(node.data, "shouldRemesh", true),
                TargetPolycount = ReadInt(node.data, "targetPolycount", 30000),
            };

            try
            {
                UnityEditor.EditorUtility.DisplayProgressBar(
                    "Meshy 3D Generator", $"Node {node.id}", 0.05f);
                var result = PcgMeshyClient.GenerateAndDownload(
                    request,
                    absolutePath,
                    default,
                    (msg, t) => UnityEditor.EditorUtility.DisplayProgressBar(
                        "Meshy 3D Generator", msg, t));
                if (!result.Ok)
                {
                    error = $"Meshy3DGenerator '{node.id}': {result.Error}";
                    return false;
                }

                Debug.Log(
                    $"[PCG] Meshy3DGenerator '{node.id}' cached → {absolutePath}" +
                    (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : ""));
                return true;
            }
            finally
            {
                UnityEditor.EditorUtility.ClearProgressBar();
            }
        }

        private static bool TryBuildImageDataUri(
            PcgGraphNodeRecord node, out string dataUri, out string error)
        {
            dataUri = null;
            error = null;

            var imageUrl = node.data.GetRaw("imageUrl")?.ToString();
            if (!string.IsNullOrWhiteSpace(imageUrl) &&
                (imageUrl.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
                 imageUrl.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ||
                 imageUrl.StartsWith("data:", StringComparison.OrdinalIgnoreCase)))
            {
                dataUri = imageUrl.Trim();
                return true;
            }

            var stored = node.data.GetRaw("texture")?.ToString();
            if (string.IsNullOrWhiteSpace(stored))
            {
                error =
                    $"Meshy3DGenerator '{node.id}': assign a Source Image (texture) " +
                    "or set imageUrl to a public / data URI.";
                return false;
            }

            var source = PcgTextureAssetUtil.LoadTextureFromStorage(stored);
            if (source == null)
            {
                error = $"Meshy3DGenerator '{node.id}': failed to load texture '{stored}'.";
                return false;
            }

            var readable = EnsureReadable(source);
            try
            {
                var png = readable.EncodeToPNG();
                if (png == null || png.Length == 0)
                {
                    error = $"Meshy3DGenerator '{node.id}': EncodeToPNG failed.";
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

        private static string BuildCacheKey(PcgGraphNodeRecord node)
        {
            var sb = new StringBuilder(256);
            sb.Append(node.id).Append('|');
            sb.Append(node.data.GetRaw("texture")?.ToString() ?? "").Append('|');
            sb.Append(node.data.GetRaw("imageUrl")?.ToString() ?? "").Append('|');
            sb.Append(node.data.GetRaw("aiModel")?.ToString() ?? "latest").Append('|');
            sb.Append(ReadBool(node.data, "enablePbr", false)).Append('|');
            sb.Append(ReadBool(node.data, "shouldTexture", true)).Append('|');
            sb.Append(ReadBool(node.data, "shouldRemesh", true)).Append('|');
            sb.Append(ReadInt(node.data, "targetPolycount", 30000));

            // Include source image bytes so texture edits invalidate the cache.
            var stored = node.data.GetRaw("texture")?.ToString();
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

        private static string GetCachePath(string cacheKey)
        {
            var root = Path.Combine(Application.dataPath, "..", "Library", "PCG", "MeshyCache");
            return Path.GetFullPath(Path.Combine(root, cacheKey + ".glb"));
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
