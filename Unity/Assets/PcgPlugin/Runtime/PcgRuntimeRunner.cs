using System.IO;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Executes a graph from StreamingAssets at runtime (IL2CPP Player path).
    /// Attach to a scene object with PcgPreview for point visualization in Play mode.
    /// </summary>
    public class PcgRuntimeRunner : MonoBehaviour
    {
        [SerializeField] private string graphFileName = "demo.pcg.json";
        [SerializeField] private int seed = 42;
        [SerializeField] private bool runOnStart = true;

        private void Start()
        {
            if (runOnStart)
                Run();
        }

        public bool Run()
        {
            var path = Path.Combine(Application.streamingAssetsPath, "pcg", graphFileName);
            Debug.Log($"[PCG] Runtime executing graph: {path} (core {PcgNative.GetVersion()})");

            var result = PcgGraphLoader.LoadAndExecute(path, seed);
            if (result == null)
                return false;

            var preview = GetComponent<PcgPreview>();
            var kind = PcgResultParser.DetectKind(result);

            switch (kind)
            {
                case PcgResultKind.Mesh:
                    if (!PcgResultParser.TryParseMesh(result, out var mesh, out var meshError))
                    {
                        Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                        return false;
                    }
                    if (preview != null)
                        preview.SetMesh(mesh);
                    Debug.Log($"[PCG] Runtime OK — mesh ({mesh.vertexCount} verts, {mesh.triangles.Length / 3} tris).");
                    break;

                case PcgResultKind.Splines:
                    if (!PcgResultParser.TryParseSplines(result, out var splines, out var splineError))
                    {
                        Debug.LogError($"[PCG] Failed to parse spline result: {splineError}");
                        return false;
                    }
                    if (preview != null)
                        preview.SetSplines(splines);
                    Debug.Log($"[PCG] Runtime OK — {splines.Count} splines.");
                    break;

                case PcgResultKind.Points:
                    if (!PcgResultParser.TryParsePoints(result, out var parsed, out var parseError))
                    {
                        Debug.LogError($"[PCG] Failed to parse point result: {parseError}");
                        return false;
                    }
                    if (preview != null)
                        preview.SetPoints(PcgResultParser.ToVector3List(parsed));
                    Debug.Log($"[PCG] Runtime OK — {parsed.pointCount} points generated.");
                    break;

                default:
                    Debug.LogError("[PCG] Unknown result JSON shape (expected points, splines, or mesh).");
                    return false;
            }

            return true;
        }
    }
}
