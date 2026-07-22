using UnityEditor;
using UnityEditor.Callbacks;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Opens <c>.pcg</c> / <c>.pcgsubgraph</c> in the Graph Editor on double-click.
    /// </summary>
    public static class PcgJsonAssetHandler
    {
        [OnOpenAsset(1)]
        public static bool OnOpenAsset(int instanceId, int line)
        {
            var path = AssetDatabase.GetAssetPath(instanceId);
            if (string.IsNullOrEmpty(path))
                return false;
            if (!path.EndsWith(".pcg", System.StringComparison.OrdinalIgnoreCase) &&
                !path.EndsWith(".pcgsubgraph", System.StringComparison.OrdinalIgnoreCase))
                return false;
            return PcgGraphEditorWindow.ShowGraphEditWindow(path);
        }
    }
}
