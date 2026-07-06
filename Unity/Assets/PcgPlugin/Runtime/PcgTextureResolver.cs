using System.Collections.Generic;
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

                var readable = EnsureReadable(source);
                if (readable == null)
                {
                    Debug.LogWarning($"[PCG] ImageTexture node '{node.id}': could not read texture pixels.");
                    continue;
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
                        SlotId = node.id,
                        Width = readable.width,
                        Height = readable.height,
                        Rgba = rgba,
                    });
                }
                finally
                {
                    if (readable != source)
                        Object.DestroyImmediate(readable);
                }
            }

            return uploads;
        }

        private static Texture2D EnsureReadable(Texture2D source)
        {
            if (source.isReadable)
                return source;

            var rt = RenderTexture.GetTemporary(source.width, source.height, 0, RenderTextureFormat.ARGB32);
            Graphics.Blit(source, rt);
            var prev = RenderTexture.active;
            RenderTexture.active = rt;
            var copy = new Texture2D(source.width, source.height, TextureFormat.RGBA32, false);
            copy.ReadPixels(new Rect(0, 0, source.width, source.height), 0, 0);
            copy.Apply();
            RenderTexture.active = prev;
            RenderTexture.ReleaseTemporary(rt);
            return copy;
        }
#endif
    }
}
