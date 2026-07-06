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
        /// Returns the result JSON string on success.
        /// </summary>
        public static string LoadAndExecute(string jsonPath, int seed = 42)
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
        /// Executes a graph JSON string directly.
        /// </summary>
        public static string Execute(string json, int seed = 42)
        {
            var (validateCode, error) = PcgNative.ValidateGraph(json);
            if (validateCode != PcgResultCode.Ok)
            {
                Debug.LogError($"[PCG] Validation failed ({validateCode}): {error}");
                return null;
            }

            var (execCode, resultJson) = PcgNative.ExecuteGraph(json, seed);
            if (execCode != PcgResultCode.Ok)
            {
                var hint = execCode == PcgResultCode.Execution
                    ? " (mesh result may exceed output buffer — try lowering bevel segments or subdiv levels)"
                    : string.Empty;
                Debug.LogError($"[PCG] Execution failed ({execCode}){hint}");
                return null;
            }

            if (PcgProjectSettings.IsLogEnabled)
                Debug.Log($"[PCG] Graph executed successfully. Result: {resultJson}");
            return resultJson;
        }
    }
}
