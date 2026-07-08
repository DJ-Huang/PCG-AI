#if UNITY_EDITOR
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// When PcgGraphComponent runs in the Editor, prefer the live Graph Editor document
    /// (includes unsaved ImageTexture assignments) over the on-disk .pcg file.
    /// </summary>
    [InitializeOnLoad]
    public static class PcgGraphExecutionBridge
    {
        static PcgGraphExecutionBridge()
        {
            PcgGraphComponent.EditorBuildExecutionJson = BuildExecutionJson;
        }

        private static string BuildExecutionJson(PcgGraphComponent component)
        {
            if (component == null || component.GraphAsset == null)
                return null;

            var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
            if (string.IsNullOrEmpty(assetPath))
                return null;

            var assetGuid = AssetDatabase.AssetPathToGUID(assetPath);
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.HasLoadedGraph)
                    continue;

                if (!window.MatchesGraphAsset(assetPath, assetGuid))
                    continue;

                var liveDoc = window.ExportLiveDocument();
                if (liveDoc == null)
                    continue;

                component.ApplyOverridesToDocument(liveDoc);
                var json = PcgGraphSerializer.ToJson(liveDoc, pretty: false);
                Debug.Log($"[PCG] Run uses live Graph Editor state for '{assetPath}'.");
                return json;
            }

            return null;
        }
    }
}
#endif
