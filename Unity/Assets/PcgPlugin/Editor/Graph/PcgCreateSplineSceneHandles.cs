#if UNITY_EDITOR
using System.Collections.Generic;
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
        private static readonly Color s_CurvePreviewColor = new(1f, 0.65f, 0.1f, 0.55f);
        private static readonly Color s_HandleColor = new(0.25f, 0.85f, 1f, 1f);
        private static readonly Color s_SelectedHandleColor = new(1f, 0.45f, 0.15f, 1f);
        private static readonly Color s_SegmentPickColor = new(0.9f, 0.9f, 0.2f, 0.9f);

        private const float SceneViewToolsPanelWidth = 48f;
        private const float SceneOverlayMargin = 12f;

        private static readonly Dictionary<string, int> s_SelectedPointByNode = new();
        private static GUIStyle s_ShortcutHintStyle;

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

            var activeNodes = new List<(PcgGraphEditorWindow window, PcgGraphView graphView, PcgManifestNodeView node)>();
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.HasLoadedGraph || window.GraphView == null)
                    continue;

                foreach (var node in window.GraphView.selection.OfType<PcgManifestNodeView>())
                {
                    if (node.NodeType != "CreateSpline")
                        continue;

                    activeNodes.Add((window, window.GraphView, node));
                }
            }

            if (activeNodes.Count == 0)
                return;

            HandleSplineShortcuts(sceneView, activeNodes);
            DrawSceneOverlay(activeNodes[0].node);

            foreach (var (window, graphView, node) in activeNodes)
                DrawNodeSpline(sceneView, window, graphView, node);

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
            var sceneOffset = ReadSceneOffset(nodeData);
            var editPlane = ReadEditPlane(nodeData);
            var usesExplicit = PcgSplineControlPoints.HasExplicitControlPoints(nodeData);
            var points = PcgSplineControlPoints.GetEffectivePoints(nodeData);
            if (points.Count < 2)
                return;

            var worldPoints = new Vector3[points.Count];
            for (var i = 0; i < points.Count; i++)
                worldPoints[i] = LocalToWorld(points[i] + sceneOffset, anchor);

            var selectedIndex = GetSelectedPointIndex(node.NodeId, points.Count);
            var closed = ReadBool(nodeData, "closed", false);
            var mode = nodeData?.GetRaw("mode")?.ToString() ?? "polyline";

            if (mode == "catmullRom")
            {
                var preview = SampleCatmullRom(points, sceneOffset, anchor, closed, ReadInt(nodeData, "subdivisions", 8));
                if (preview.Count >= 2)
                {
                    Handles.color = s_CurvePreviewColor;
                    Handles.DrawAAPolyLine(2f, preview.ToArray());
                }
            }

            Handles.color = s_ControlLineColor;
            Handles.DrawAAPolyLine(3f, worldPoints);
            if (closed && worldPoints.Length >= 2)
                Handles.DrawLine(worldPoints[^1], worldPoints[0]);

            if (TrySelectPointClick(worldPoints, node.NodeId))
            {
                selectedIndex = GetSelectedPointIndex(node.NodeId, points.Count);
                sceneView.Repaint();
            }

            if (TryInsertOnSegmentClick(
                    sceneView,
                    window,
                    graphView,
                    node,
                    points,
                    worldPoints,
                    sceneOffset,
                    editPlane,
                    usesExplicit,
                    closed,
                    selectedIndex,
                    anchor))
            {
                sceneView.Repaint();
                return;
            }

            var changed = false;
            for (var i = 0; i < worldPoints.Length; i++)
            {
                var isSelected = i == selectedIndex;
                Handles.color = isSelected ? s_SelectedHandleColor : s_HandleColor;

                if (isSelected)
                {
                    var pickSize = HandleUtility.GetHandleSize(worldPoints[i]) * 0.11f;
                    Handles.SphereHandleCap(0, worldPoints[i], Quaternion.identity, pickSize, EventType.Repaint);
                }

                EditorGUI.BeginChangeCheck();
                var newWorld = Handles.PositionHandle(worldPoints[i], Quaternion.identity);
                if (!EditorGUI.EndChangeCheck())
                    continue;

                SetSelectedPointIndex(node.NodeId, i);
                BeginSplineDrag(graphView, window);

                var newLocal = WorldToLocal(newWorld, anchor) - sceneOffset;
                newLocal = ConstrainToEditPlane(newLocal, editPlane);
                points[i] = newLocal;
                WritePointsToNode(node, points, usesExplicit);
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

        private static GUIStyle ShortcutHintStyle
        {
            get
            {
                if (s_ShortcutHintStyle != null)
                    return s_ShortcutHintStyle;

                s_ShortcutHintStyle = new GUIStyle(EditorStyles.miniLabel)
                {
                    alignment = TextAnchor.MiddleCenter,
                    wordWrap = true,
                };
                s_ShortcutHintStyle.normal.textColor = new Color(0.12f, 0.12f, 0.12f);
                return s_ShortcutHintStyle;
            }
        }

        private static void DrawSceneOverlay(PcgManifestNodeView node)
        {
            var nodeData = node.CollectData();
            var pointCount = PcgSplineControlPoints.GetEffectivePoints(nodeData).Count;
            var selectedIndex = GetSelectedPointIndex(node.NodeId, pointCount);

            Handles.BeginGUI();
            try
            {
                const float width = 300f;
                const float height = 118f;
                var area = new Rect(
                    SceneViewToolsPanelWidth + SceneOverlayMargin,
                    SceneOverlayMargin,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                GUILayout.BeginArea(area);
                GUILayout.Space(6f);
                GUILayout.Label($"Create Spline — {node.GetDisplayTitle()}", EditorStyles.boldLabel);
                GUILayout.Label(
                    $"Points: {pointCount}   Selected: {(selectedIndex >= 0 ? selectedIndex.ToString() : "none")}",
                    EditorStyles.miniLabel);

                GUILayout.BeginHorizontal();
                GUI.enabled = pointCount >= 2;
                if (GUILayout.Button("Insert Point (I)", GUILayout.Height(22f)))
                {
                    if (InsertPointForNode(node, selectedIndex))
                        selectedIndex = GetSelectedPointIndex(node.NodeId, pointCount + 1);
                }

                GUI.enabled = pointCount > 2 && selectedIndex >= 0;
                if (GUILayout.Button("Delete Point (Del)", GUILayout.Height(22f)))
                {
                    if (DeleteSelectedPointForNode(node))
                    {
                        pointCount = PcgSplineControlPoints.GetEffectivePoints(node.CollectData()).Count;
                        selectedIndex = GetSelectedPointIndex(node.NodeId, pointCount);
                    }
                }

                GUI.enabled = true;
                GUILayout.EndHorizontal();

                GUILayout.Label(
                    "I insert · Del delete · Shift+click segment insert",
                    ShortcutHintStyle);
                GUILayout.EndArea();
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void HandleSplineShortcuts(
            SceneView sceneView,
            IReadOnlyList<(PcgGraphEditorWindow window, PcgGraphView graphView, PcgManifestNodeView node)> activeNodes)
        {
            var evt = Event.current;
            if (evt.type != EventType.KeyDown || EditorGUIUtility.editingTextField)
                return;

            if (evt.keyCode != KeyCode.I &&
                evt.keyCode != KeyCode.Delete &&
                evt.keyCode != KeyCode.Backspace)
                return;

            var target = activeNodes[0].node;
            var selectedIndex = GetSelectedPointIndex(target.NodeId, int.MaxValue);

            if (evt.keyCode == KeyCode.I)
            {
                if (InsertPointForNode(target, selectedIndex))
                {
                    evt.Use();
                    sceneView.Repaint();
                }

                return;
            }

            if (selectedIndex < 0)
                return;

            if (DeleteSelectedPointForNode(target))
            {
                evt.Use();
                sceneView.Repaint();
            }
        }

        private static bool TrySelectPointClick(IReadOnlyList<Vector3> worldPoints, string nodeId)
        {
            var evt = Event.current;
            if (evt.type != EventType.MouseDown ||
                evt.button != 0 ||
                evt.shift ||
                evt.alt ||
                evt.control)
                return false;

            var bestIndex = -1;
            var bestDistance = float.MaxValue;
            for (var i = 0; i < worldPoints.Count; i++)
            {
                var distance = HandleUtility.DistanceToCircle(worldPoints[i], 0f);
                if (distance > 16f || distance >= bestDistance)
                    continue;

                bestDistance = distance;
                bestIndex = i;
            }

            if (bestIndex < 0)
                return false;

            SetSelectedPointIndex(nodeId, bestIndex);
            return true;
        }

        private static bool TryInsertOnSegmentClick(
            SceneView sceneView,
            PcgGraphEditorWindow window,
            PcgGraphView graphView,
            PcgManifestNodeView node,
            List<Vector3> points,
            Vector3[] worldPoints,
            Vector3 sceneOffset,
            string editPlane,
            bool usesExplicit,
            bool closed,
            int selectedIndex,
            Transform anchor)
        {
            var evt = Event.current;
            if (evt.type != EventType.MouseDown ||
                evt.button != 0 ||
                !evt.shift ||
                evt.alt ||
                evt.control)
                return false;

            var ray = HandleUtility.GUIPointToWorldRay(evt.mousePosition);
            if (!TryPickSegment(worldPoints, closed, ray, 12f, out var segmentIndex, out var hitWorld))
                return false;

            var insertIndex = segmentIndex + 1;
            var localPoint = ConstrainToEditPlane(WorldToLocal(hitWorld, anchor) - sceneOffset, editPlane);

            graphView.WithUndo("Insert Spline Control Point", () =>
            {
                points.Insert(insertIndex, localPoint);
                WritePointsToNode(node, points, usesExplicit);
            });

            SetSelectedPointIndex(node.NodeId, insertIndex);
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);

            Handles.color = s_SegmentPickColor;
            Handles.SphereHandleCap(0, hitWorld, Quaternion.identity, HandleUtility.GetHandleSize(hitWorld) * 0.08f, EventType.Repaint);

            evt.Use();
            HandleUtility.Repaint();
            return true;
        }

        private static bool InsertPointForNode(PcgManifestNodeView node, int selectedIndex)
        {
            if (!TryGetActiveContext(node, out var window, out var graphView))
                return false;

            var nodeData = node.CollectData();
            var usesExplicit = PcgSplineControlPoints.HasExplicitControlPoints(nodeData);
            var points = PcgSplineControlPoints.GetEffectivePoints(nodeData);
            if (points.Count < 2)
                return false;

            var insertIndex = selectedIndex >= 0 && selectedIndex < points.Count - 1
                ? selectedIndex + 1
                : points.Count;

            var left = points[Mathf.Clamp(insertIndex - 1, 0, points.Count - 1)];
            var right = points[Mathf.Clamp(insertIndex, 0, points.Count - 1)];
            var midpoint = ConstrainToEditPlane((left + right) * 0.5f, ReadEditPlane(nodeData));

            graphView.WithUndo("Insert Spline Control Point", () =>
            {
                points.Insert(insertIndex, midpoint);
                WritePointsToNode(node, points, usesExplicit);
            });

            SetSelectedPointIndex(node.NodeId, insertIndex);
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            SceneView.RepaintAll();
            return true;
        }

        private static bool DeleteSelectedPointForNode(PcgManifestNodeView node)
        {
            if (!TryGetActiveContext(node, out var window, out var graphView))
                return false;

            var nodeData = node.CollectData();
            var usesExplicit = PcgSplineControlPoints.HasExplicitControlPoints(nodeData);
            var points = PcgSplineControlPoints.GetEffectivePoints(nodeData);
            var selectedIndex = GetSelectedPointIndex(node.NodeId, points.Count);
            if (selectedIndex < 0 || points.Count <= 2)
                return false;

            graphView.WithUndo("Delete Spline Control Point", () =>
            {
                points.RemoveAt(selectedIndex);
                WritePointsToNode(node, points, usesExplicit);
            });

            SetSelectedPointIndex(node.NodeId, Mathf.Clamp(selectedIndex, 0, points.Count - 1));
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            SceneView.RepaintAll();
            return true;
        }

        private static bool TryGetActiveContext(
            PcgManifestNodeView node,
            out PcgGraphEditorWindow window,
            out PcgGraphView graphView)
        {
            window = null;
            graphView = null;

            foreach (var candidate in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (candidate == null || !candidate.HasLoadedGraph || candidate.GraphView == null)
                    continue;

                if (!candidate.GraphView.selection.OfType<PcgManifestNodeView>().Any(n => n.NodeId == node.NodeId))
                    continue;

                window = candidate;
                graphView = candidate.GraphView;
                return true;
            }

            return false;
        }

        private static void WritePointsToNode(
            PcgManifestNodeView node,
            IReadOnlyList<Vector3> points,
            bool usesExplicit)
        {
            if (usesExplicit || points.Count > 2)
            {
                node.SetPropertyValue("controlPoints", PcgSplineControlPoints.Serialize(points));
                return;
            }

            if (points.Count < 2)
                return;

            node.SetPropertyValue("startX", points[0].x);
            node.SetPropertyValue("startY", points[0].y);
            node.SetPropertyValue("startZ", points[0].z);
            node.SetPropertyValue("endX", points[1].x);
            node.SetPropertyValue("endY", points[1].y);
            node.SetPropertyValue("endZ", points[1].z);
            node.SetPropertyValue("controlPoints", "[]");
        }

        private static int GetSelectedPointIndex(string nodeId, int pointCount)
        {
            if (!s_SelectedPointByNode.TryGetValue(nodeId, out var index))
                return pointCount >= 2 ? 0 : -1;

            if (index < 0 || index >= pointCount)
                return pointCount >= 2 ? Mathf.Clamp(index, 0, pointCount - 1) : -1;

            return index;
        }

        private static void SetSelectedPointIndex(string nodeId, int index)
        {
            s_SelectedPointByNode[nodeId] = index;
        }

        private static bool TryPickSegment(
            IReadOnlyList<Vector3> worldPoints,
            bool closed,
            Ray ray,
            float maxPixelDistance,
            out int segmentIndex,
            out Vector3 hitWorld)
        {
            segmentIndex = -1;
            hitWorld = Vector3.zero;

            var bestDistance = float.MaxValue;
            var segmentCount = closed ? worldPoints.Count : worldPoints.Count - 1;
            for (var i = 0; i < segmentCount; i++)
            {
                var a = worldPoints[i];
                var b = worldPoints[(i + 1) % worldPoints.Count];
                if (!TryRaySegmentClosestPoint(ray, a, b, out var closest))
                    continue;

                var screenDist = HandleUtility.DistanceToCircle(closest, 0f);
                if (screenDist > maxPixelDistance || screenDist >= bestDistance)
                    continue;

                bestDistance = screenDist;
                segmentIndex = i;
                hitWorld = closest;
            }

            return segmentIndex >= 0;
        }

        private static bool TryRaySegmentClosestPoint(Ray ray, Vector3 a, Vector3 b, out Vector3 closest)
        {
            closest = Vector3.zero;
            var segment = b - a;
            var segmentLengthSq = segment.sqrMagnitude;
            if (segmentLengthSq < 1e-8f)
                return false;

            var t = Mathf.Clamp01(Vector3.Dot(ray.origin - a, segment) / segmentLengthSq);
            var pointOnSegment = a + segment * t;
            var toPoint = pointOnSegment - ray.origin;
            var projection = Vector3.Project(toPoint, ray.direction);
            closest = ray.origin + projection;

            var along = Vector3.Dot(projection, ray.direction);
            if (along < 0f)
                return false;

            var lateral = toPoint - projection;
            return lateral.sqrMagnitude < 4f;
        }

        private static List<Vector3> SampleCatmullRom(
            IReadOnlyList<Vector3> localPoints,
            Vector3 sceneOffset,
            Transform anchor,
            bool closed,
            int subdivisionsPerSegment)
        {
            var samples = new List<Vector3>();
            if (localPoints.Count < 2)
                return samples;

            var count = localPoints.Count;
            var segmentCount = closed ? count : count - 1;
            var steps = Mathf.Max(2, subdivisionsPerSegment);

            for (var seg = 0; seg < segmentCount; seg++)
            {
                var p0 = localPoints[WrapIndex(seg - 1, count, closed)];
                var p1 = localPoints[seg];
                var p2 = localPoints[WrapIndex(seg + 1, count, closed)];
                var p3 = localPoints[WrapIndex(seg + 2, count, closed)];

                var endStep = seg == segmentCount - 1 && !closed ? steps : steps - 1;
                for (var step = 0; step <= endStep; step++)
                {
                    var t = step / (float)steps;
                    var local = CatmullRom(p0, p1, p2, p3, t) + sceneOffset;
                    samples.Add(LocalToWorld(local, anchor));
                }
            }

            return samples;
        }

        private static int WrapIndex(int index, int count, bool closed)
        {
            if (closed)
                return (index % count + count) % count;

            return Mathf.Clamp(index, 0, count - 1);
        }

        private static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
        {
            var t2 = t * t;
            var t3 = t2 * t;
            return 0.5f * (
                (2f * p1) +
                (-p0 + p2) * t +
                (2f * p0 - 5f * p1 + 4f * p2 - p3) * t2 +
                (-p0 + 3f * p1 - 3f * p2 + p3) * t3);
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

        private static Vector3 ReadSceneOffset(PcgNodeData data)
        {
            if (data == null)
                return Vector3.zero;

            return new Vector3(
                ReadFloat(data, "sceneOffsetX", 0f),
                ReadFloat(data, "sceneOffsetY", 0f),
                ReadFloat(data, "sceneOffsetZ", 0f));
        }

        private static string ReadEditPlane(PcgNodeData data) =>
            data?.GetRaw("editPlane")?.ToString() ?? "none";

        private static Vector3 ConstrainToEditPlane(Vector3 local, string editPlane) =>
            editPlane switch
            {
                "xy" => new Vector3(local.x, local.y, 0f),
                "xz" => new Vector3(local.x, 0f, local.z),
                "yz" => new Vector3(0f, local.y, local.z),
                _ => local,
            };

        private static bool ReadBool(PcgNodeData data, string key, bool defaultValue)
        {
            var raw = data?.GetRaw(key);
            return raw switch
            {
                bool b => b,
                string s when bool.TryParse(s, out var parsed) => parsed,
                _ => defaultValue,
            };
        }

        private static int ReadInt(PcgNodeData data, string key, int defaultValue)
        {
            var raw = data?.GetRaw(key);
            return raw switch
            {
                int i => i,
                long l => (int)l,
                float f => Mathf.RoundToInt(f),
                double d => (int)d,
                string s when int.TryParse(s, out var parsed) => parsed,
                _ => defaultValue,
            };
        }

        private static float ReadFloat(PcgNodeData data, string key, float defaultValue)
        {
            var raw = data?.GetRaw(key);
            if (raw == null)
                return defaultValue;

            return raw switch
            {
                float f => f,
                double d => (float)d,
                int i => i,
                long l => l,
                _ => float.TryParse(
                    raw.ToString(),
                    System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture,
                    out var parsed)
                    ? parsed
                    : defaultValue,
            };
        }
    }
}
#endif
