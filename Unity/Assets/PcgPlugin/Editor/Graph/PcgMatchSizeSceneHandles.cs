#if UNITY_EDITOR
using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Houdini-style MatchSize target volume preview: blue wireframe bounding box in Scene View.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgMatchSizeSceneHandles
    {
        private static readonly Color s_TargetWire = new(0.2f, 0.55f, 1f, 0.95f);
        private static readonly Color s_SelectedWire = new(0.35f, 0.75f, 1f, 1f);

        static PcgMatchSizeSceneHandles()
        {
            SceneView.duringSceneGui -= OnSceneGui;
            SceneView.duringSceneGui += OnSceneGui;
        }

        private static void OnSceneGui(SceneView sceneView)
        {
            if (Application.isPlaying || sceneView == null)
                return;

            if (Event.current == null || Event.current.type != EventType.Repaint)
                return;

            var window = ResolveGraphWindow();
            if (window == null || !window.HasLoadedGraph)
                return;

            var component = ResolveComponent(window);
            if (component == null)
                return;

            if (!TryResolveDocument(window, out var doc) || doc?.nodes == null)
                return;

            var selectedIds = CollectSelectedMatchSizeIds(window);
            var previewNodeId = window.PreviewNodeId;
            var drawIds = new HashSet<string>(selectedIds);
            if (!string.IsNullOrEmpty(previewNodeId))
            {
                var previewNode = FindNode(doc, previewNodeId);
                if (previewNode != null && previewNode.type == "MatchSize")
                    drawIds.Add(previewNodeId);
            }

            if (drawIds.Count == 0)
                return;

            var anchor = FindPreviewAnchor(component, window);
            var previewMatrix = anchor != null ? anchor.localToWorldMatrix : Matrix4x4.identity;

            foreach (var nodeId in drawIds)
            {
                var node = FindNode(doc, nodeId);
                if (node == null || node.type != "MatchSize")
                    continue;

                if (!PcgMatchSizeBoundsUtility.TryGetTargetBounds(
                        doc, node, component, previewNodeId, out var bounds))
                    continue;

                if (bounds.IsDegenerate)
                    continue;

                var isSelected = selectedIds.Contains(nodeId);
                DrawTargetWireCube(previewMatrix, bounds, isSelected);
            }
        }

        private static void DrawTargetWireCube(
            Matrix4x4 localToWorld,
            PcgMatchSizeBoundsUtility.MinMaxBounds bounds,
            bool selected)
        {
            var center = bounds.Center;
            var size = bounds.Size;
            if (size.x < 0.0001f) size.x = 0.0001f;
            if (size.y < 0.0001f) size.y = 0.0001f;
            if (size.z < 0.0001f) size.z = 0.0001f;

            var prevColor = Handles.color;
            var prevMatrix = Handles.matrix;
            Handles.color = selected ? s_SelectedWire : s_TargetWire;
            Handles.matrix = localToWorld;
            Handles.DrawWireCube(center, size);
            Handles.matrix = prevMatrix;
            Handles.color = prevColor;
        }

        private static PcgGraphNodeRecord FindNode(PcgGraphDocument doc, string nodeId)
        {
            foreach (var node in doc.nodes)
            {
                if (node != null && node.id == nodeId)
                    return node;
            }

            return null;
        }

        private static HashSet<string> CollectSelectedMatchSizeIds(PcgGraphEditorWindow window)
        {
            var ids = new HashSet<string>();
            if (window?.GraphView == null)
                return ids;

            foreach (var node in window.GraphView.selection.OfType<PcgManifestNodeView>())
            {
                if (node.NodeType == "MatchSize" && !string.IsNullOrEmpty(node.NodeId))
                    ids.Add(node.NodeId);
            }

            return ids;
        }

        private static PcgGraphEditorWindow ResolveGraphWindow()
        {
            var focused = EditorWindow.focusedWindow as PcgGraphEditorWindow;
            if (focused != null && focused.HasLoadedGraph)
                return focused;

            var component = Selection.activeGameObject != null
                ? Selection.activeGameObject.GetComponent<PcgGraphComponent>()
                : null;
            if (component != null && component.GraphAsset != null)
                return FindWindowForComponent(component);

            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window != null && window.HasLoadedGraph)
                    return window;
            }

            return null;
        }

        private static PcgGraphComponent ResolveComponent(PcgGraphEditorWindow window)
        {
            var component = Selection.activeGameObject != null
                ? Selection.activeGameObject.GetComponent<PcgGraphComponent>()
                : null;
            if (component != null && component.GraphAsset != null)
                return component;

            return FindComponentForWindow(window);
        }

        private static Transform FindPreviewAnchor(PcgGraphComponent component, PcgGraphEditorWindow window)
        {
            if (component != null)
                return component.transform;

            if (window == null)
                return null;

            var assetPath = window.CurrentAssetPath;
            var assetGuid = window.selectedGuid;
            foreach (var candidate in Object.FindObjectsOfType<PcgGraphComponent>())
            {
                if (candidate == null || candidate.GraphAsset == null)
                    continue;

                var componentPath = AssetDatabase.GetAssetPath(candidate.GraphAsset);
                if (string.IsNullOrEmpty(componentPath))
                    continue;

                if (componentPath != assetPath && AssetDatabase.AssetPathToGUID(componentPath) != assetGuid)
                    continue;

                return candidate.transform;
            }

            return null;
        }

        private static bool TryResolveDocument(PcgGraphEditorWindow window, out PcgGraphDocument doc)
        {
            doc = null;
            if (window != null && window.HasLoadedGraph)
            {
                doc = window.ExportLiveDocument();
                if (doc != null)
                    return true;
            }

            return false;
        }

        private static PcgGraphEditorWindow FindWindowForComponent(PcgGraphComponent component)
        {
            var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
            if (string.IsNullOrEmpty(assetPath))
                return null;

            var assetGuid = AssetDatabase.AssetPathToGUID(assetPath);
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window != null &&
                    window.HasLoadedGraph &&
                    window.MatchesGraphAsset(assetPath, assetGuid))
                    return window;
            }

            return null;
        }

        private static PcgGraphComponent FindComponentForWindow(PcgGraphEditorWindow window)
        {
            var assetPath = window.CurrentAssetPath;
            var assetGuid = window.selectedGuid;
            foreach (var component in Object.FindObjectsOfType<PcgGraphComponent>())
            {
                if (component == null || component.GraphAsset == null)
                    continue;

                var path = AssetDatabase.GetAssetPath(component.GraphAsset);
                if (path == assetPath || AssetDatabase.AssetPathToGUID(path) == assetGuid)
                    return component;
            }

            return null;
        }
    }
}
#endif
