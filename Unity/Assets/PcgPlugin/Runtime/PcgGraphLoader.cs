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
            return ExecuteWithResolvedAssets(json, seed, null, null, null, null, null);
        }

        public static PcgGraphExecuteResult ExecuteWithResolvedAssets(
            string json,
            int seed,
            GameObject componentHost,
            IReadOnlyList<PcgMeshBinding> meshBindings,
            IReadOnlyList<PcgPreviewMeshBinding> previewBindings)
        {
            return ExecuteWithResolvedAssets(
                json, seed, componentHost, meshBindings, previewBindings, null, null);
        }

        public static PcgGraphExecuteResult ExecuteWithResolvedAssets(
            string json,
            int seed,
            GameObject componentHost,
            IReadOnlyList<PcgMeshBinding> meshBindings,
            IReadOnlyList<PcgPreviewMeshBinding> previewBindings,
            IReadOnlyList<PcgSplineBinding> splineBindings,
            IReadOnlyList<PcgPreviewSplineBinding> previewSplineBindings)
        {
            var assetSw = System.Diagnostics.Stopwatch.StartNew();
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

            var splines = PcgSplineResolver.CollectFromGraphJson(
                json, componentHost, splineBindings, previewSplineBindings);
            assetSw.Stop();

            var result = Execute(json, seed, textures, meshes, splines);
            if (result?.Perf != null)
                result.Perf.AssetResolveMs = assetSw.Elapsed.TotalMilliseconds;
            return result;
        }

        public static PcgGraphExecuteResult Execute(string json, int seed = 42)
        {
            return Execute(json, seed, null, null, null);
        }

        public static PcgGraphExecuteResult Execute(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes)
        {
            return Execute(json, seed, textures, meshes, null);
        }

        public static PcgGraphExecuteResult Execute(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines)
        {
            return Execute(json, seed, textures, meshes, splines, null);
        }

        public static PcgGraphExecuteResult Execute(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines,
            IReadOnlyList<PcgHeightFieldUpload> heightfields)
        {
            if (!PcgGraphExecutionPolicy.TryPrepareJson(json, out json, out var prepareError))
            {
                Debug.LogError($"[PCG] Failed to prepare graph for execution: {prepareError}");
                return null;
            }

            // ExecuteGraph parses and validates the same document. A separate native
            // validation call doubled JSON parsing on every cook without adding safety.
            var (execCode, result) = PcgCookBackend.ExecuteGraph(
                json, seed, textures, meshes, splines, heightfields);
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

            PcgCookPerfLog.Log(result.Perf);

            if (PcgProjectSettings.IsLogEnabled)
            {
                if (result.Kind == PcgExecuteKind.Mesh)
                {
                    Debug.Log(
                        $"[PCG] Graph executed. Mesh binary: {result.VertexCount} verts, {result.IndexCount} indices.");
                }
                else if (result.Kind == PcgExecuteKind.Points)
                {
                    Debug.Log(
                        $"[PCG] Graph executed. Point binary: {result.PointCount} points, flags=0x{result.PointAttrFlags:X}.");
                }
                else
                {
                    Debug.Log($"[PCG] Graph executed. Result: {result.Json}");
                }
            }

            return result;
        }
    }
}
