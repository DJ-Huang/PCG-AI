using System.IO;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Shared graph execution + Scene preview update logic.
    /// </summary>
    public static class PcgGraphRunner
    {
        private const string PreviewObjectName = "PCG Preview";

        public static string DefaultSchemaDir =>
            Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "schema"));

        public static string DefaultWatchedGraphPath =>
            Path.Combine(DefaultSchemaDir, "editor-export.pcg.json");

        public static bool RunFileAndUpdatePreview(string path, int seed = 42)
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                Debug.LogError($"[PCG] Graph file not found: {path}");
                return false;
            }

            var result = PcgGraphLoader.LoadAndExecute(path, seed);
            if (result == null)
                return false;

            if (!PcgResultParser.TryParse(result, out var parsed, out var parseError))
            {
                Debug.LogError($"[PCG] Failed to parse result JSON: {parseError}");
                return false;
            }

            var preview = FindOrCreatePreview();
            preview.SetPoints(PcgResultParser.ToVector3List(parsed));
            EditorUtility.SetDirty(preview);
            SceneView.RepaintAll();

            Debug.Log($"[PCG] Executed graph with {parsed.pointCount} points. Preview updated on '{preview.gameObject.name}'.");
            return true;
        }

        private static PcgPreview FindOrCreatePreview()
        {
            var existing = Object.FindObjectOfType<PcgPreview>();
            if (existing != null)
                return existing;

            var go = new GameObject(PreviewObjectName);
            Undo.RegisterCreatedObjectUndo(go, "Create PCG Preview");
            return go.AddComponent<PcgPreview>();
        }
    }
}
