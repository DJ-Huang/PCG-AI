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
        [SerializeField] private string schemaVersion = PcgSubgraphAssetMigration.Version10;
        [SerializeField] private string contentHash = "";

        public string SourceJson => sourceJson;
        public string AssetName => assetName;
        public string ImportError => importError;
        public bool ImportSucceeded => importSucceeded;
        public IReadOnlyList<string> DependencyGuids => dependencyGuids;
        public string SchemaVersion => schemaVersion;
        public string ContentHash => contentHash;

        public void SetImportResult(
            string json,
            string name,
            bool succeeded,
            string error,
            IEnumerable<string> dependencies,
            string version = null,
            string hash = null)
        {
            sourceJson = json ?? "";
            assetName = name ?? "";
            importSucceeded = succeeded;
            importError = error ?? "";
            dependencyGuids = dependencies != null
                ? new List<string>(dependencies)
                : new List<string>();
            schemaVersion = string.IsNullOrEmpty(version) ? PcgSubgraphAssetMigration.Version10 : version;
            contentHash = hash ?? "";
        }
    }
}
