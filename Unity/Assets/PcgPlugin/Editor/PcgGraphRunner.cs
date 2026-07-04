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

            var json = File.ReadAllText(path);
            return RunJsonAndUpdatePreview(json, seed);
        }

        public static bool RunJsonAndUpdatePreview(string json, int seed = 42)
        {
            if (string.IsNullOrWhiteSpace(json))
            {
                Debug.LogError("[PCG] Graph JSON is empty.");
                return false;
            }

            var result = PcgGraphLoader.Execute(json, seed);
            if (result == null)
                return false;

            var preview = FindOrCreatePreview();
            var kind = PcgResultParser.DetectKind(result);

            switch (kind)
            {
                case PcgResultKind.Mesh:
                    if (!PcgResultParser.TryParseMesh(result, out var mesh, out var meshError))
                    {
                        Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                        return false;
                    }
                    preview.SetMesh(mesh);
                    Debug.Log($"[PCG] Mesh preview updated ({mesh.vertexCount} verts, {mesh.triangles.Length / 3} tris).");
                    break;

                case PcgResultKind.Splines:
                    if (!PcgResultParser.TryParseSplines(result, out var splines, out var splineError))
                    {
                        Debug.LogError($"[PCG] Failed to parse spline result: {splineError}");
                        return false;
                    }
                    preview.SetSplines(splines);
                    Debug.Log($"[PCG] Spline preview updated ({splines.Count} splines).");
                    break;

                case PcgResultKind.Points:
                    if (!PcgResultParser.TryParsePoints(result, out var parsed, out var parseError))
                    {
                        Debug.LogError($"[PCG] Failed to parse point result: {parseError}");
                        return false;
                    }
                    preview.SetPoints(PcgResultParser.ToVector3List(parsed));
                    Debug.Log($"[PCG] Point preview updated ({parsed.pointCount} points).");
                    break;

                default:
                    Debug.LogError("[PCG] Unknown result JSON shape (expected points, splines, or mesh).");
                    return false;
            }

            EditorUtility.SetDirty(preview);
            SceneView.RepaintAll();
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
