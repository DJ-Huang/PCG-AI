#if UNITY_EDITOR
using System.Collections.Generic;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    [InitializeOnLoad]
    public static class PcgMeshExecutionBridge
    {
        static PcgMeshExecutionBridge()
        {
            PcgGraphComponent.EditorResolvePreviewMeshBindings = ResolvePreviewMeshBindings;
        }

        private static IReadOnlyList<PcgPreviewMeshBinding> ResolvePreviewMeshBindings(PcgGraphComponent component)
        {
            if (component == null || component.GraphAsset == null)
                return null;

            var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
            if (string.IsNullOrEmpty(assetPath))
                return null;

            var assetGuid = AssetDatabase.AssetPathToGUID(assetPath);
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.MatchesGraphAsset(assetPath, assetGuid))
                    continue;

                return window.PreviewMeshBindings;
            }

            return null;
        }
    }
}
#endif
