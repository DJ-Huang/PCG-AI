using UnityEngine;
using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// ScriptableObject wrapper for a PCG graph JSON document.
    /// Created at import time by PcgAssetImporter (Editor) for .pcg files.
    /// Player builds consume <see cref="GraphJson"/> which must be a baked flat v2 document.
    /// </summary>
    public sealed class PcgGraphAsset : ScriptableObject
    {
        [TextArea(8, 24)]
        [SerializeField]
        private string graphJson = "";

        [SerializeField] private string importError = "";
        [SerializeField] private bool importSucceeded = true;
        [SerializeField] private List<string> dependencyGuids = new();

        public string GraphJson => graphJson;
        public string ImportError => importError;
        public bool ImportSucceeded => importSucceeded;
        public IReadOnlyList<string> DependencyGuids => dependencyGuids;

        public void SetGraphJson(string json)
        {
            graphJson = json ?? "";
        }

        public void SetImportResult(
            string bakedJson,
            bool succeeded,
            string error,
            IEnumerable<string> dependencies)
        {
            graphJson = bakedJson ?? "";
            importSucceeded = succeeded;
            importError = error ?? "";
            dependencyGuids = dependencies != null
                ? new List<string>(dependencies)
                : new List<string>();
        }
    }
}
