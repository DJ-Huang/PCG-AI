using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Resolves ImageTexture node GUIDs to RGBA float buffers for pcg_execute_graph_v3.
    /// Editor-only: uses AssetDatabase; returns empty list in Player builds.
    /// </summary>
    public static class PcgTextureResolver
    {
        public static List<PcgTextureUpload> CollectFromGraphJson(string json)
        {
#if UNITY_EDITOR
            return CollectFromGraphJsonEditor(json);
#else
            return new List<PcgTextureUpload>();
#endif
        }

#if UNITY_EDITOR
        private static List<PcgTextureUpload> CollectFromGraphJsonEditor(string json)
        {
            var uploads = new List<PcgTextureUpload>();
            if (string.IsNullOrWhiteSpace(json))
                return uploads;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                return uploads;

            foreach (var node in doc.nodes)
            {
                if (node.type == PcgMeshyImageGenResolver.NodeType && !string.IsNullOrEmpty(node.id))
                {
                    CollectMeshyImageGen(node, uploads);
                    continue;
                }

                if (node.type != "ImageTexture" || string.IsNullOrEmpty(node.id))
                    continue;

                var stored = node.data?.GetRaw("texture")?.ToString();
                if (string.IsNullOrWhiteSpace(stored))
                {
                    Debug.LogWarning($"[PCG] ImageTexture node '{node.id}' has no texture assigned.");
                    continue;
                }

                var source = PcgTextureAssetUtil.LoadTextureFromStorage(stored);
                if (source == null)
                {
                    Debug.LogWarning(
                        $"[PCG] ImageTexture node '{node.id}': failed to load '{stored}'.");
                    continue;
                }

                UploadTexture(node.id, source, uploads, destroySource: false);
            }

            return uploads;
        }

        /// <summary>MeshyImageGen nodes stream their saved/cached PNG file (Generate-baked).</summary>
        private static void CollectMeshyImageGen(PcgGraphNodeRecord node, List<PcgTextureUpload> uploads)
        {
            string imagePath;
            if (!PcgMeshyResolver.TryGetSavedModelPath(node.data, out imagePath) &&
                !PcgMeshyImageGenResolver.TryGetCachedImagePath(node.id, node.data, out imagePath))
            {
                Debug.LogWarning(
                    $"[PCG] MeshyImageGen node '{node.id}': no saved/cached image. Click Generate first.");
                return;
            }

            try
            {
                var bytes = File.ReadAllBytes(imagePath);
                var source = new Texture2D(2, 2, TextureFormat.RGBA32, false);
                if (!source.LoadImage(bytes))
                {
                    Object.DestroyImmediate(source);
                    Debug.LogWarning(
                        $"[PCG] MeshyImageGen node '{node.id}': failed to decode image '{imagePath}'.");
                    return;
                }

                UploadTexture(node.id, source, uploads, destroySource: true);
            }
            catch (System.Exception ex)
            {
                Debug.LogWarning(
                    $"[PCG] MeshyImageGen node '{node.id}': failed to read '{imagePath}': {ex.Message}");
            }
        }

        private static void UploadTexture(
            string slotId, Texture2D source, List<PcgTextureUpload> uploads, bool destroySource)
        {
            var readable = EnsureReadable(source);
            if (readable == null)
            {
                Debug.LogWarning($"[PCG] Texture slot '{slotId}': could not read texture pixels.");
                if (destroySource && source != null)
                    Object.DestroyImmediate(source);
                return;
            }

            try
            {
                var pixels = readable.GetPixels();
                var rgba = new float[pixels.Length * 4];
                for (var i = 0; i < pixels.Length; i++)
                {
                    rgba[i * 4] = pixels[i].r;
                    rgba[i * 4 + 1] = pixels[i].g;
                    rgba[i * 4 + 2] = pixels[i].b;
                    rgba[i * 4 + 3] = pixels[i].a;
                }

                uploads.Add(new PcgTextureUpload
                {
                    SlotId = slotId,
                    Width = readable.width,
                    Height = readable.height,
                    Rgba = rgba,
                });
            }
            finally
            {
                if (readable != source)
                    Object.DestroyImmediate(readable);
                if (destroySource && source != null)
                    Object.DestroyImmediate(source);
            }
        }

        private static Texture2D EnsureReadable(Texture2D source)
        {
            if (source.isReadable)
                return source;

            var rt = RenderTexture.GetTemporary(source.width, source.height, 0, RenderTextureFormat.ARGB32);
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
#endif
    }
}
