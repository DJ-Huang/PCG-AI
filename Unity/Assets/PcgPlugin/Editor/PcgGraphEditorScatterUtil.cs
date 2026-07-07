#if UNITY_EDITOR
using System.Collections.Generic;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    internal static class PcgGraphEditorScatterUtil
    {
        public static IReadOnlyList<PcgGraphComponent> FindLinkedComponents(PcgGraphEditorWindow window)
        {
            var results = new List<PcgGraphComponent>();
            if (window == null || !window.HasLoadedGraph)
                return results;

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

                results.Add(component);
            }

            return results;
        }

        public static PcgScatterDisplayMode GetDisplayMode(PcgGraphEditorWindow window)
        {
            var components = FindLinkedComponents(window);
            if (components.Count == 0)
                return PcgProjectSettings.DefaultScatterDisplayMode;

            return components[0].ScatterDisplayMode;
        }

        public static void SetDisplayMode(PcgGraphEditorWindow window, PcgScatterDisplayMode mode)
        {
            var components = FindLinkedComponents(window);
            if (components.Count == 0)
            {
                Debug.LogWarning(
                    "[PCG] No PcgGraphComponent in the scene uses this graph. " +
                    "Add one to the hierarchy, or change Display Mode on the component Inspector.");
                return;
            }

            foreach (var component in components)
            {
                Undo.RecordObject(component, "Change Scatter Display Mode");
                component.SetScatterDisplayMode(mode);
            }

            PcgGraphEditorCookBridge.RepaintViews();
        }
    }
}
#endif
