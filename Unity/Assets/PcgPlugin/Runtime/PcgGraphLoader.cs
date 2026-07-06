using System.Collections.Generic;
using System.IO;
using DJTechRuntime.PCG;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Loads a Graph JSON file and executes it via PcgCore.
    /// </summary>
    public static class PcgGraphLoader
    {
        public static string DefaultGraphPath =>
            Path.Combine(Application.streamingAssetsPath, "pcg", "graph.json");

        /// <summary>
        /// Loads and executes a graph JSON file.
        /// Returns the execution result on success.
        /// </summary>
        public static PcgGraphExecuteResult LoadAndExecute(string jsonPath, int seed = 42)
        {
            if (!File.Exists(jsonPath))
            {
                Debug.LogError($"[PCG] Graph file not found: {jsonPath}");
                return null;
            }

            string json = File.ReadAllText(jsonPath);
            return Execute(json, seed);
        }

        /// <summary>
        /// Executes a graph JSON with ImageTexture nodes resolved in the Editor.
        /// </summary>
        public static PcgGraphExecuteResult ExecuteWithResolvedTextures(string json, int seed = 42)
        {
            var textures = PcgTextureResolver.CollectFromGraphJson(json);
            if (!PcgTextureGraphUtil.TryValidateTextureRequirements(json, textures, out var textureError))
            {
                Debug.LogError($"[PCG] {textureError}");
                return null;
            }

            return textures.Count > 0 ? Execute(json, seed, textures) : Execute(json, seed);
        }

        /// <summary>
        /// Executes a graph JSON string directly.
        /// </summary>
        public static PcgGraphExecuteResult Execute(string json, int seed = 42)
        {
            return Execute(json, seed, null);
        }

        /// <summary>
        /// Executes a graph JSON with optional runtime texture uploads (ImageTexture nodes).
        /// </summary>
        public static PcgGraphExecuteResult Execute(string json, int seed, IReadOnlyList<PcgTextureUpload> textures)
        {
            var (validateCode, error) = PcgNative.ValidateGraph(json);
            if (validateCode != PcgResultCode.Ok)
            {
                Debug.LogError($"[PCG] Validation failed ({validateCode}): {error}");
                return null;
            }

            var (execCode, result) = PcgNative.ExecuteGraph(json, seed, textures);
            if (execCode != PcgResultCode.Ok)
            {
                Debug.LogError($"[PCG] Execution failed ({execCode}): {result?.Error}");
                return null;
            }

            if (PcgProjectSettings.IsLogEnabled)
            {
                if (result.Kind == PcgExecuteKind.Mesh)
                {
                    Debug.Log(
                        $"[PCG] Graph executed successfully. Mesh binary: {result.VertexCount} verts, {result.IndexCount} indices.");
                }
                else
                {
                    Debug.Log($"[PCG] Graph executed successfully. Result: {result.Json}");
                }
            }

            return result;
        }
    }
}
