#if UNITY_EDITOR
using System.Linq;
using DJTechEditor.PCG;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Houdini-style Scene View editing for <see cref="PcgManifestNodeView"/> CreateSpline nodes
    /// selected in an open Graph Editor window.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgCreateSplineSceneHandles
    {
        private static readonly Color s_ControlLineColor = new(1f, 0.85f, 0.2f, 0.85f);
        private static readonly Color s_HandleColor = new(0.25f, 0.85f, 1f, 1f);

        private static bool s_DragActive;
        private static bool s_HandleHot;
        private static PcgGraphView s_DragGraphView;
        private static PcgGraphEditorWindow s_DragWindow;

        static PcgCreateSplineSceneHandles()
        {
            SceneView.duringSceneGui += OnSceneGui;
        }

        private static void OnSceneGui(SceneView sceneView)
        {
            if (Application.isPlaying)
                return;

            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.HasLoadedGraph || window.GraphView == null)
                    continue;

                foreach (var node in window.GraphView.selection.OfType<PcgManifestNodeView>())
                {
                    if (node.NodeType != "CreateSpline")
                        continue;

                    DrawNodeSpline(sceneView, window, window.GraphView, node);
                }
            }

            TryEndSplineDrag();
        }

        private static void DrawNodeSpline(
            SceneView sceneView,
            PcgGraphEditorWindow window,
            PcgGraphView graphView,
            PcgManifestNodeView node)
        {
            var anchor = FindPreviewAnchor(window);
            var nodeData = node.CollectData();
            var usesExplicit = PcgSplineControlPoints.HasExplicitControlPoints(nodeData);
            var points = PcgSplineControlPoints.GetEffectivePoints(nodeData);
            if (points.Count < 2)
                return;

            var worldPoints = new Vector3[points.Count];
            for (var i = 0; i < points.Count; i++)
                worldPoints[i] = LocalToWorld(points[i], anchor);

            Handles.color = s_ControlLineColor;
            Handles.DrawAAPolyLine(3f, worldPoints);

            var changed = false;
            for (var i = 0; i < worldPoints.Length; i++)
            {
                Handles.color = s_HandleColor;

                EditorGUI.BeginChangeCheck();
                var newWorld = Handles.PositionHandle(worldPoints[i], Quaternion.identity);
                if (!EditorGUI.EndChangeCheck())
                    continue;

                BeginSplineDrag(graphView, window);

                var newLocal = WorldToLocal(newWorld, anchor);
                points[i] = newLocal;

                if (usesExplicit || points.Count > 2)
                {
                    node.SetPropertyValue(
                        "controlPoints",
                        PcgSplineControlPoints.Serialize(points));
                }
                else
                {
                    node.SetPropertyValue("startX", points[0].x);
                    node.SetPropertyValue("startY", points[0].y);
                    node.SetPropertyValue("startZ", points[0].z);
                    node.SetPropertyValue("endX", points[1].x);
                    node.SetPropertyValue("endY", points[1].y);
                    node.SetPropertyValue("endZ", points[1].z);
                }

                changed = true;
            }

            if (!changed)
                return;

            graphView.CommitState();
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            sceneView.Repaint();
            HandleUtility.Repaint();
        }

        private static void BeginSplineDrag(PcgGraphView graphView, PcgGraphEditorWindow window)
        {
            if (s_DragActive)
                return;

            graphView.BeginDrag("Move Spline Control Point");
            s_DragActive = true;
            s_HandleHot = true;
            s_DragGraphView = graphView;
            s_DragWindow = window;
        }

        private static void TryEndSplineDrag()
        {
            if (!s_DragActive || s_DragGraphView == null)
                return;

            var evt = Event.current;
            var mouseReleased = evt.type == EventType.MouseUp;
            var hotControlReleased = s_HandleHot &&
                                     GUIUtility.hotControl == 0 &&
                                     (evt.type == EventType.Repaint || evt.type == EventType.Layout);

            if (!mouseReleased && !hotControlReleased)
                return;

            s_DragGraphView.EndDrag();
            if (s_DragWindow != null)
                PcgGraphEditorCookBridge.NotifyGraphChanged(s_DragWindow, immediate: true);

            s_DragActive = false;
            s_HandleHot = false;
            s_DragGraphView = null;
            s_DragWindow = null;
        }

        private static Transform FindPreviewAnchor(PcgGraphEditorWindow window)
        {
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

                return component.transform;
            }

            return null;
        }

        private static Vector3 LocalToWorld(Vector3 local, Transform anchor) =>
            anchor != null ? anchor.TransformPoint(local) : local;

        private static Vector3 WorldToLocal(Vector3 world, Transform anchor) =>
            anchor != null ? anchor.InverseTransformPoint(world) : world;
    }
}
#endif
