using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    public static class PcgGraphLoader
    {
        public static string DefaultGraphPath =>
            System.IO.Path.Combine(Application.streamingAssetsPath, "pcg", "graph.json");

        public static PcgGraphExecuteResult LoadAndExecute(string jsonPath, int seed = 42)
        {
            if (!System.IO.File.Exists(jsonPath))
            {
                Debug.LogError($"[PCG] Graph file not found: {jsonPath}");
                return null;
            }

            return Execute(System.IO.File.ReadAllText(jsonPath), seed);
        }

        public static PcgGraphExecuteResult ExecuteWithResolvedTextures(string json, int seed = 42)
        {
            return ExecuteWithResolvedAssets(json, seed, null, null, null, PcgPreviewQuality.Full);
        }

        public static PcgGraphExecuteResult ExecuteWithResolvedAssets(
            string json,
            int seed,
            GameObject componentHost,
            IReadOnlyList<PcgMeshBinding> meshBindings,
            IReadOnlyList<PcgPreviewMeshBinding> previewBindings,
            PcgPreviewQuality quality)
        {
            var textures = PcgTextureResolver.CollectFromGraphJson(json);
            if (!PcgTextureGraphUtil.TryValidateTextureRequirements(json, textures, out var textureError))
            {
                Debug.LogError($"[PCG] {textureError}");
                return null;
            }

            var meshes = PcgMeshResolver.CollectFromGraphJson(
                json, componentHost, meshBindings, previewBindings);
            if (!PcgMeshGraphUtil.TryValidateMeshRequirements(json, meshes, out _))
                return null;

            return Execute(json, seed, textures, meshes, quality);
        }

        public static PcgGraphExecuteResult Execute(string json, int seed = 42)
        {
            return Execute(json, seed, null, null, PcgPreviewQuality.Full);
        }

        public static PcgGraphExecuteResult Execute(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            PcgPreviewQuality quality)
        {
            var (validateCode, error) = PcgNative.ValidateGraph(json);
            if (validateCode != PcgResultCode.Ok)
            {
                Debug.LogError($"[PCG] Validation failed ({validateCode}): {error}");
                return null;
            }

            var (execCode, result) = PcgNative.ExecuteGraph(json, seed, textures, meshes);
            if (execCode != PcgResultCode.Ok)
            {
                Debug.LogError($"[PCG] Execution failed ({execCode}): {result?.Error}");
                return null;
            }

            if (result.CookNodesSkipped > 0)
            {
                Debug.Log(
                    $"[PCG] Cook cache: skipped {result.CookNodesSkipped} node(s), executed {result.CookNodesExecuted}.");
            }
            else if (PcgProjectSettings.IsLogEnabled)
            {
                Debug.Log(
                    $"[PCG] Cook cache: skipped 0 node(s), executed {result.CookNodesExecuted} (cold).");
            }

            if (PcgProjectSettings.IsLogEnabled)
            {
                if (result.Kind == PcgExecuteKind.Mesh)
                {
                    Debug.Log(
                        $"[PCG] Graph executed ({quality}). Mesh binary: {result.VertexCount} verts, {result.IndexCount} indices.");
                }
                else if (result.Kind == PcgExecuteKind.Points)
                {
                    Debug.Log(
                        $"[PCG] Graph executed ({quality}). Point binary: {result.PointCount} points, flags=0x{result.PointAttrFlags:X}.");
                }
                else
                {
                    Debug.Log($"[PCG] Graph executed ({quality}). Result: {result.Json}");
                }
            }

            return result;
        }
    }
}
