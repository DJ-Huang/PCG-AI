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

            if (!PcgResultParser.TryParse(result, out var parsed, out var parseError))
            {
                Debug.LogError($"[PCG] Failed to parse result JSON: {parseError}");
                return false;
            }

            var preview = GetComponent<PcgPreview>();
            if (preview != null)
                preview.SetPoints(PcgResultParser.ToVector3List(parsed));

            Debug.Log($"[PCG] Runtime OK — {parsed.pointCount} points generated.");
            return true;
        }
    }
}
