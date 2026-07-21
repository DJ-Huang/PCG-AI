using System;
using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// ScriptableObject wrapper for a linked <c>.pcgsubgraph</c> asset.
    /// </summary>
    public sealed class PcgSubgraphAsset : ScriptableObject
    {
        [SerializeField] private string sourceJson = "";
        [SerializeField] private string assetName = "";
        [SerializeField] private string importError = "";
        [SerializeField] private bool importSucceeded;
        [SerializeField] private List<string> dependencyGuids = new();

        public string SourceJson => sourceJson;
        public string AssetName => assetName;
        public string ImportError => importError;
        public bool ImportSucceeded => importSucceeded;
        public IReadOnlyList<string> DependencyGuids => dependencyGuids;

        public void SetImportResult(
            string json,
            string name,
            bool succeeded,
            string error,
            IEnumerable<string> dependencies)
        {
            sourceJson = json ?? "";
            assetName = name ?? "";
            importSucceeded = succeeded;
            importError = error ?? "";
            dependencyGuids = dependencies != null
                ? new List<string>(dependencies)
                : new List<string>();
        }
    }
}
