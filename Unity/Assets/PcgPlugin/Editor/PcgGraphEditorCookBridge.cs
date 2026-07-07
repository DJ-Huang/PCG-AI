#if UNITY_EDITOR
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    [InitializeOnLoad]
    public static class PcgGraphEditorCookBridge
    {
        static PcgGraphEditorCookBridge()
        {
            PcgGraphView.GraphDocumentChanged += OnGraphDocumentChanged;
            PcgGraphComponent.EditorAfterPreviewCookApplied = RepaintViews;
        }

        public static void RepaintViews()
        {
            SceneView.RepaintAll();
            EditorApplication.QueuePlayerLoopUpdate();
        }

        public static void NotifyGraphChanged(PcgGraphEditorWindow window)
        {
            if (window == null || !window.HasLoadedGraph)
                return;

            var assetPath = window.CurrentAssetPath;
            var assetGuid = window.selectedGuid;
            foreach (var component in Object.FindObjectsOfType<PcgGraphComponent>())
            {
                if (component == null || component.GraphAsset == null)
                    continue;

                var componentPath = AssetDatabase.GetAssetPath(component.GraphAsset);
                if (string.IsNullOrEmpty(componentPath))
                    continue;

                if (componentPath != assetPath && AssetDatabase.AssetPathToGUID(componentPath) != assetGuid)
                    continue;

                if (!component.SupportsEditModePreview())
                    continue;

                component.RequestPreviewCook();
            }
        }

        private static void OnGraphDocumentChanged(PcgGraphEditorWindow window)
        {
            NotifyGraphChanged(window);
        }
    }
}
#endif
