using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// ScriptableObject wrapper for a PCG graph JSON document.
    /// Created at import time by PcgAssetImporter (Editor) for .pcg files.
    /// </summary>
    public sealed class PcgGraphAsset : ScriptableObject
    {
        [TextArea(8, 24)]
        [SerializeField]
        private string graphJson = "";

        public string GraphJson => graphJson;

        public void SetGraphJson(string json)
        {
            graphJson = json ?? "";
        }
    }
}
