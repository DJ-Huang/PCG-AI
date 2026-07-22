using System.Collections.Generic;
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
        [SerializeField] private string graphFileName = "demo.pcg";
        [SerializeField] private int seed = 42;
        [SerializeField] private bool runOnStart = true;
        [SerializeField] private List<PcgParameterOverride> m_ParameterOverrides = new();

        public List<PcgParameterOverride> ParameterOverrides => m_ParameterOverrides;

        private void Start()
        {
            if (runOnStart)
                Run();
        }

        public bool Run()
        {
            var path = Path.Combine(Application.streamingAssetsPath, "pcg", graphFileName);
            Debug.Log($"[PCG] Runtime executing graph: {path} (core {PcgNative.GetVersion()})");

            string json = File.ReadAllText(path);

            if (PcgExecutionDocumentBuilder.ContainsExternalOrAuthoringV3(json, out var reason))
            {
                Debug.LogError(
                    $"[PCG] StreamingAssets graph is not Player-ready. Bake it in the Editor first. {reason}");
                return false;
            }

            if (m_ParameterOverrides != null && m_ParameterOverrides.Count > 0)
                json = PcgParameterApplicator.ApplyOverrides(json, m_ParameterOverrides);

            var result = PcgGraphLoader.Execute(json, seed);
            if (result == null)
                return false;

            var preview = GetComponent<PcgPreview>();
            var kind = PcgResultParser.DetectKind(result);

            switch (kind)
            {
                case PcgResultKind.Mesh:
                    if (!PcgResultParser.TryParseMeshBinary(result.MeshBinary, out var mesh, out var meshError))
                    {
                        Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                        return false;
                    }
                    if (preview != null)
                        preview.SetMesh(mesh);
                    Debug.Log($"[PCG] Runtime OK — mesh ({mesh.vertexCount} verts, {mesh.triangles.Length / 3} tris).");
                    break;

                case PcgResultKind.Splines:
                    if (!PcgResultParser.TryParseSplines(result.Json, out var splines, out var splineError))
                    {
                        Debug.LogError($"[PCG] Failed to parse spline result: {splineError}");
                        return false;
                    }
                    if (preview != null)
                        preview.SetSplines(splines);
                    Debug.Log($"[PCG] Runtime OK — {splines.Count} splines.");
                    break;

                case PcgResultKind.Points:
                    if (!PcgResultParser.TryParsePoints(result.Json, out var parsed, out var parseError))
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
