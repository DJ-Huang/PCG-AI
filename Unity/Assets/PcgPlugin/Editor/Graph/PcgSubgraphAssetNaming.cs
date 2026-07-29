using System.IO;
using DJTechRuntime.PCG;
using UnityEditor;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Resolves the user-facing name of a linked <c>.pcgsubgraph</c> asset.
    /// The Project asset file name is preferred over the JSON <c>name</c> field,
    /// which often stays at the create-time default (e.g. NewSubgraph).
    /// </summary>
    public static class PcgSubgraphAssetNaming
    {
        public static string ResolveDisplayName(PcgSubgraphAsset asset, PcgSubgraphAssetDocument doc = null)
        {
            if (asset == null)
                return "Subgraph Asset";
            return ResolveDisplayName(AssetDatabase.GetAssetPath(asset), doc);
        }

        public static string ResolveDisplayName(string assetPath, PcgSubgraphAssetDocument doc)
        {
            var fileName = GetFileNameWithoutExtension(assetPath);
            if (!string.IsNullOrEmpty(fileName))
                return fileName;
            return ResolveDisplayNameFromDocument(doc);
        }

        public static string ResolveDisplayName(string assetPath, string documentName)
        {
            var fileName = GetFileNameWithoutExtension(assetPath);
            if (!string.IsNullOrEmpty(fileName))
                return fileName;
            return string.IsNullOrEmpty(documentName) ? "Subgraph Asset" : documentName;
        }

        private static string ResolveDisplayNameFromDocument(PcgSubgraphAssetDocument doc)
        {
            return string.IsNullOrEmpty(doc?.name) ? "Subgraph Asset" : doc.name;
        }

        private static string GetFileNameWithoutExtension(string assetPath)
        {
            if (string.IsNullOrEmpty(assetPath))
                return "";
            return Path.GetFileNameWithoutExtension(assetPath.Replace('\\', '/'));
        }
    }
}
