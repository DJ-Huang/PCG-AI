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

        public static void CancelPreviewCooksForWindow(PcgGraphEditorWindow window)
        {
            foreach (var component in ComponentsForWindow(window))
            {
                component.CancelAsyncCookForPreviewSwitch();
                // Node Preview of a mask/HF node keeps LastCookedHeightField for the red
                // Scene tint. Leaving preview must drop that surface immediately; the
                // following full-graph cook restores the final HeightField if needed.
                component.ClearHeightFieldOverlayForPreviewSwitch();
            }
        }

        public static void NotifyGraphChanged(PcgGraphEditorWindow window, bool immediate = false)
        {
            foreach (var component in ComponentsForWindow(window))
                component.RequestPreviewCook(immediate);
        }

        private static System.Collections.Generic.IEnumerable<PcgGraphComponent> ComponentsForWindow(
            PcgGraphEditorWindow window)
        {
            if (window == null || !window.HasLoadedGraph)
                yield break;

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

                yield return component;
            }
        }

        private static void OnGraphDocumentChanged(PcgGraphEditorWindow window)
        {
            NotifyGraphChanged(window);
        }
    }
}
#endif
