using System.IO;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Reads the latest .pcg JSON from disk in the Editor (avoids stale importer cache).
    /// </summary>
    public static class PcgGraphAssetUtility
    {
        public static string ReadLatestJson(PcgGraphAsset asset)
        {
            if (asset == null)
                return null;

#if UNITY_EDITOR
            var path = UnityEditor.AssetDatabase.GetAssetPath(asset);
            if (!string.IsNullOrEmpty(path) && File.Exists(path))
                return File.ReadAllText(path);
#endif
            return asset.GraphJson;
        }
    }
}
