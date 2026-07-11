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
        private static readonly Color s_TangentLineColor = new(0.6f, 1f, 0.4f, 0.7f);
        private static readonly Color s_TangentHandleColor = new(0.4f, 0.9f, 0.3f, 1f);
        private static readonly Color s_TangentHandleActiveColor = new(0.7f, 1f, 0.5f, 1f);
        private static readonly Color s_UnselectedPointColor = new(0.5f, 0.5f, 0.5f, 0.6f);

        private const float SceneViewToolsPanelWidth = 48f;
        private const float SceneOverlayMargin = 12f;

        private static readonly Dictionary<string, HashSet<int>> s_SelectedPointByNode = new();
        private static GUIStyle s_ShortcutHintStyle;

        private static bool s_DragActive;
        private static bool s_HandleHot;
        private static PcgGraphView s_DragGraphView;
        private static PcgGraphEditorWindow s_DragWindow;
        private static bool s_TangentDragActive;

        private static bool s_PcgModeActive;
        private static PcgGraphEditorWindow s_ActiveWindow;
        private static GameObject s_LockedSelection;
        private static bool s_SelectionGuard;
        private static Tool s_PrevTool;

        private enum OthersDisplayMode
        {
            ShowAll,
            HideOthers,
            IsolatePCG,
        }

        private static OthersDisplayMode s_OthersDisplay = OthersDisplayMode.ShowAll;
        private static readonly HashSet<Renderer> s_HiddenRenderers = new();

        // --- Procedurally generated toolbar icons ---
        private static Texture2D IconObjectNormal => s_IconObjectNormal ??= MakeIcon(new(0.7f, 0.7f, 0.7f), DrawObjectIcon);
        private static Texture2D IconObjectActive => s_IconObjectActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawObjectIcon);
        private static Texture2D IconSplineNormal => s_IconSplineNormal ??= MakeIcon(new(0.7f, 0.7f, 0.7f), DrawSplineIcon);
        private static Texture2D IconSplineActive => s_IconSplineActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawSplineIcon);
        private static Texture2D IconVertexNormal => s_IconVertexNormal ??= MakeIcon(new(0.5f, 0.5f, 0.5f), DrawVertexIcon);
        private static Texture2D IconEdgeNormal => s_IconEdgeNormal ??= MakeIcon(new(0.5f, 0.5f, 0.5f), DrawEdgeIcon);
        private static Texture2D IconFaceNormal => s_IconFaceNormal ??= MakeIcon(new(0.5f, 0.5f, 0.5f), DrawFaceIcon);
        private static Texture2D IconExit => s_IconExit ??= MakeIcon(new(0.85f, 0.5f, 0.4f), DrawExitIcon);

        private static Texture2D s_IconObjectNormal, s_IconObjectActive;
        private static Texture2D s_IconSplineNormal, s_IconSplineActive;
        private static Texture2D s_IconVertexNormal, s_IconEdgeNormal, s_IconFaceNormal;
        private static Texture2D s_IconExit;

        private const int IconSize = 16;

        // --- Icon generation ---

        private delegate void IconDrawFunc(Color[] px, int size, Color c);

        private static Texture2D MakeIcon(Color baseColor, IconDrawFunc drawFn)
        {
            var tex = new Texture2D(IconSize, IconSize, TextureFormat.RGBA32, false)
            {
                hideFlags = HideFlags.HideAndDontSave,
                filterMode = FilterMode.Bilinear,
            };
            var px = new Color[IconSize * IconSize];
            for (var i = 0; i < px.Length; i++)
                px[i] = Color.clear;
            drawFn(px, IconSize, baseColor);
            tex.SetPixels(px);
            tex.Apply();
            return tex;
        }

        private static void SetPx(Color[] px, int size, int x, int y, Color c)
        {
            if (x < 0 || x >= size || y < 0 || y >= size) return;
            px[y * size + x] = c;
        }

        private static void DrawLine(Color[] px, int size, int x0, int y0, int x1, int y1, Color c)
        {
            int dx = Mathf.Abs(x1 - x0), dy = Mathf.Abs(y1 - y0);
            int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int err = dx - dy;
            while (true)
            {
                SetPx(px, size, x0, y0, c);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x0 += sx; }
                if (e2 < dx) { err += dx; y0 += sy; }
            }
        }

        private static void DrawObjectIcon(Color[] px, int size, Color c)
        {
            // Wireframe cube — 3D-ish box
            int m = 2, s = size - 3;
            DrawLine(px, size, m, m, s, m, c);          // top
            DrawLine(px, size, m, s, s, s, c);          // bottom
            DrawLine(px, size, m, m, m, s, c);          // left
            DrawLine(px, size, s, m, s, s, c);          // right
            // depth lines
            DrawLine(px, size, m, m, m + 2, m - 2, c);
            DrawLine(px, size, s, m, s + 2, m - 2, c);
            DrawLine(px, size, m + 2, m - 2, s + 2, m - 2, c);
        }

        private static void DrawSplineIcon(Color[] px, int size, Color c)
        {
            // Wavy curve through 4 points
            int m = 2;
            for (int i = 0; i < size - m * 2; i++)
            {
                float t = (float)i / (size - m * 2 - 1);
                float y = Mathf.Sin(t * Mathf.PI * 2f) * 3f + size / 2f;
                SetPx(px, size, m + i, Mathf.RoundToInt(y), c);
                SetPx(px, size, m + i, Mathf.RoundToInt(y) + 1, c); // thicken
            }
        }

        private static void DrawVertexIcon(Color[] px, int size, Color c)
        {
            // Single dot with ring
            int cx = size / 2, cy = size / 2;
            for (int r = 1; r <= 2; r++)
            {
                for (int a = 0; a < 360; a += 30)
                {
                    float rad = a * Mathf.Deg2Rad;
                    SetPx(px, size, cx + Mathf.RoundToInt(Mathf.Cos(rad) * r), cy + Mathf.RoundToInt(Mathf.Sin(rad) * r), c);
                }
            }
            SetPx(px, size, cx, cy, c);
        }

        private static void DrawEdgeIcon(Color[] px, int size, Color c)
        {
            // Diagonal line with endpoint dots
            int m = 2, s = size - 3;
            DrawLine(px, size, m, s, s, m, c);
            SetPx(px, size, m, s, c); SetPx(px, size, m + 1, s, c); SetPx(px, size, m, s - 1, c);
            SetPx(px, size, s, m, c); SetPx(px, size, s - 1, m, c); SetPx(px, size, s, m + 1, c);
        }

        private static void DrawFaceIcon(Color[] px, int size, Color c)
        {
            // Filled triangle
            int m = 2, s = size - 3;
            int mid = size / 2;
            DrawLine(px, size, mid, m, m, s, c);     // left edge
            DrawLine(px, size, mid, m, s, s, c);    // right edge
            DrawLine(px, size, m, s, s, s, c);      // bottom
            // fill
            for (int y = m + 1; y < s; y++)
            {
                int half = (y - m) * (s - mid) / (s - m);
                for (int x = mid - half; x <= mid + half; x++)
                    SetPx(px, size, x, y, c * 0.5f);
            }
        }

        private static void DrawExitIcon(Color[] px, int size, Color c)
        {
            // X in a circle
            int cx = size / 2, cy = size / 2;
            int r = size / 2 - 1;
            // circle
            for (int a = 0; a < 360; a += 20)
            {
                float rad = a * Mathf.Deg2Rad;
                SetPx(px, size, cx + Mathf.RoundToInt(Mathf.Cos(rad) * r), cy + Mathf.RoundToInt(Mathf.Sin(rad) * r), c);
            }
            // X
            int m = 4, s = size - 5;
            DrawLine(px, size, m, m, s, s, c);
            DrawLine(px, size, s, m, m, s, c);
        }

        static PcgCreateSplineSceneHandles()
        {
            SceneView.duringSceneGui += OnSceneGui;
            Selection.selectionChanged += OnSelectionChanged;
        }

        private static void OnSelectionChanged()
        {
            if (!s_PcgModeActive || s_LockedSelection == null || s_SelectionGuard)
                return;

            if (Selection.activeGameObject != s_LockedSelection)
            {
                s_SelectionGuard = true;
                Selection.activeGameObject = s_LockedSelection;
                s_SelectionGuard = false;
            }
        }

        private static void OnSceneGui(SceneView sceneView)
        {
            if (Application.isPlaying)
                return;

            var graphWindow = FindGraphWindow();

            if (graphWindow == null)
            {
                if (s_PcgModeActive)
                    ExitPcgMode();
                return;
            }

            if (!s_PcgModeActive)
            {
                DrawPcgModeEntryOverlay(sceneView, graphWindow);
                return;
            }

            DrawPcgModeToolbar(sceneView, graphWindow);

            var splineNodes = new List<(PcgGraphEditorWindow window, PcgGraphView graphView, PcgManifestNodeView node)>();
            foreach (var node in graphWindow.GraphView.selection.OfType<PcgManifestNodeView>())
            {
                if (node.NodeType == "CreateSpline")
                    splineNodes.Add((graphWindow, graphWindow.GraphView, node));
            }

            var ctx = graphWindow.GraphView.SceneEditContext;
            if (splineNodes.Count > 0 && ctx.IsComponentMode && ctx.Domain == SceneEditDomain.SplineControlPoint)
            {
                HandleSplineShortcuts(sceneView, splineNodes);
                DrawSplineOverlay(splineNodes[0].node);

                foreach (var (window, graphView, node) in splineNodes)
                    DrawNodeSpline(sceneView, window, graphView, node);
            }
            else
            {
                DrawPcgModeStatusOverlay(graphWindow);
            }

            TryEndSplineDrag();

            // AddDefaultControl registers a fallback that absorbs clicks on empty
            // space, preventing Unity's hierarchy picker from changing selection.
            // It must be called AFTER all GUI buttons and 3D handles have registered
            // their hit tests so they take priority over the default control.
            //
            // Do NOT set Selection.activeGameObject here — doing so during MouseDown
            // clears GUIUtility.hotControl, which prevents GUILayout.Button from
            // detecting clicks (Exit, Insert Point, etc.). Selection locking is
            // handled by OnSelectionChanged instead.
            if (s_LockedSelection != null)
            {
                HandleUtility.AddDefaultControl(GUIUtility.GetControlID(FocusType.Passive));
            }
        }

        private static PcgGraphEditorWindow FindGraphWindow()
        {
            if (s_PcgModeActive && s_ActiveWindow != null && s_ActiveWindow.HasLoadedGraph)
                return s_ActiveWindow;

            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window != null && window.HasLoadedGraph && window.GraphView != null)
                    return window;
            }
            return null;
        }

        private static void EnterPcgMode(PcgGraphEditorWindow window)
        {
            s_PcgModeActive = true;
            s_ActiveWindow = window;
            window.GraphView.SetSceneMode(SceneEditLevel.Object, SceneEditDomain.None);

            var anchor = FindPreviewAnchor(window);
            if (anchor != null)
            {
                s_LockedSelection = anchor.gameObject;
                s_SelectionGuard = true;
                Selection.activeGameObject = s_LockedSelection;
                s_SelectionGuard = false;
            }

            s_PrevTool = Tools.current;
            Tools.current = Tool.None;
            ApplyOthersDisplayMode();
        }

        private static void ExitPcgMode()
        {
            s_PcgModeActive = false;
            s_ActiveWindow = null;
            s_LockedSelection = null;
            s_SelectedPointByNode.Clear();
            Tools.current = s_PrevTool;
            RestoreHiddenRenderers();
        }

        private static void ApplyOthersDisplayMode()
        {
            RestoreHiddenRenderers();

            if (s_OthersDisplay == OthersDisplayMode.ShowAll)
                return;

            var pcgObjects = new HashSet<GameObject>();
            if (s_LockedSelection != null)
            {
                pcgObjects.Add(s_LockedSelection);
                foreach (var comp in s_LockedSelection.GetComponentsInChildren<PcgGraphComponent>(true))
                    pcgObjects.Add(comp.gameObject);
            }

            foreach (var renderer in Object.FindObjectsByType<Renderer>(FindObjectsInactive.Include, FindObjectsSortMode.None))
            {
                if (renderer == null || renderer.transform == null)
                    continue;

                var go = renderer.gameObject;
                if (pcgObjects.Contains(go))
                    continue;

                // In Isolate mode also hide the PCG object's siblings under the same parent
                s_HiddenRenderers.Add(renderer);
                renderer.enabled = false;
            }
        }

        private static void RestoreHiddenRenderers()
        {
            foreach (var renderer in s_HiddenRenderers)
            {
                if (renderer != null)
                    renderer.enabled = true;
            }
            s_HiddenRenderers.Clear();
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

            var selectedIndices = GetSelectedPointIndices(node.NodeId, points.Count);
            var closed = ReadBool(nodeData, "closed", false);
            var mode = nodeData?.GetRaw("mode")?.ToString() ?? "polyline";

            if (mode == "catmullRom")
            {
                var previewTangents = PcgSplineControlPoints.GetTangents(points, nodeData, closed);
                var preview = SampleCatmullRom(points, previewTangents, sceneOffset, anchor, closed, ReadInt(nodeData, "subdivisions", 8));
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

            // Read tangent data (computed from Catmull-Rom or explicitly stored)
            var tangents = PcgSplineControlPoints.GetTangents(points, nodeData, closed);
            var hasExplicitTangents = PcgSplineControlPoints.HasExplicitTangents(nodeData);
            // Normalise tangent display to a fixed visual length so handles are
            // always visible and never collapse to a degenerate point.
            var tangentWorldPositions = new Vector3[points.Count * 2];
            for (var i = 0; i < points.Count; i++)
            {
                var wp = worldPoints[i];
                var tangentDirWorld = LocalDirToWorldDir(tangents[i], anchor);
                var displayLen = HandleUtility.GetHandleSize(wp) * 0.4f;
                if (tangentDirWorld.sqrMagnitude > 1e-8f)
                    tangentDirWorld = tangentDirWorld.normalized * displayLen;
                else
                    tangentDirWorld = Vector3.forward * displayLen;
                tangentWorldPositions[i * 2] = wp + tangentDirWorld;       // out-tangent
                tangentWorldPositions[i * 2 + 1] = wp - tangentDirWorld;   // in-tangent (mirrored)
            }

            var changed = false;

            // Draw tangent handles for selected points (BEFORE selection click
            // so their hit tests register first and grab hotControl)
            if (selectedIndices.Count > 0)
            {
                changed |= DrawTangentHandles(
                    sceneView, window, graphView, node, points, worldPoints,
                    tangents, tangentWorldPositions, sceneOffset, editPlane,
                    usesExplicit, hasExplicitTangents, closed, anchor, selectedIndices);
            }

            // Draw control point handles — only selected points get a PositionHandle
            // (BEFORE selection click so PositionHandle axes register first)
            for (var i = 0; i < worldPoints.Length; i++)
            {
                var isSelected = selectedIndices.Contains(i);

                if (!isSelected)
                {
                    // Unselected: small clickable sphere only, no position handle
                    if (Event.current.type == EventType.Repaint)
                    {
                        Handles.color = s_UnselectedPointColor;
                        var unselectedSize = HandleUtility.GetHandleSize(worldPoints[i]) * 0.08f;
                        Handles.SphereHandleCap(0, worldPoints[i], Quaternion.identity, unselectedSize, EventType.Repaint);
                    }
                    continue;
                }

                // Selected: larger sphere + position handle
                if (Event.current.type == EventType.Repaint)
                {
                    Handles.color = s_SelectedHandleColor;
                    var pickSize = HandleUtility.GetHandleSize(worldPoints[i]) * 0.11f;
                    Handles.SphereHandleCap(0, worldPoints[i], Quaternion.identity, pickSize, EventType.Repaint);
                }

                EditorGUI.BeginChangeCheck();
                var newWorld = Handles.PositionHandle(worldPoints[i], Quaternion.identity);
                if (!EditorGUI.EndChangeCheck())
                    continue;

                BeginSplineDrag(graphView, window);

                var delta = newWorld - worldPoints[i];
                foreach (var selIdx in selectedIndices)
                {
                    var newLocal = WorldToLocal(worldPoints[selIdx] + delta, anchor) - sceneOffset;
                    newLocal = ConstrainToEditPlane(newLocal, editPlane);
                    points[selIdx] = newLocal;
                }
                WritePointsToNode(node, points, usesExplicit);
                changed = true;
                break;
            }

            // Selection click handling — AFTER handles so hotControl is set
            // by PositionHandle / FreeMoveHandle before we check it.
            if (TrySelectPointClick(worldPoints, node.NodeId))
            {
                selectedIndices = GetSelectedPointIndices(node.NodeId, points.Count);
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
                    anchor))
            {
                sceneView.Repaint();
                return;
            }

            if (!changed)
                return;

            graphView.CommitState();
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            sceneView.Repaint();
            HandleUtility.Repaint();
        }

        private static bool DrawTangentHandles(
            SceneView sceneView,
            PcgGraphEditorWindow window,
            PcgGraphView graphView,
            PcgManifestNodeView node,
            List<Vector3> points,
            Vector3[] worldPoints,
            List<Vector3> tangents,
            Vector3[] tangentWorldPositions,
            Vector3 sceneOffset,
            string editPlane,
            bool usesExplicit,
            bool hasExplicitTangents,
            bool closed,
            Transform anchor,
            HashSet<int> selectedIndices)
        {
            var changed = false;

            for (var i = 0; i < points.Count; i++)
            {
                if (!selectedIndices.Contains(i))
                    continue;

                var wp = worldPoints[i];
                var outWorld = tangentWorldPositions[i * 2];
                var inWorld = tangentWorldPositions[i * 2 + 1];

                // Skip tangent handles when the tangent is too short — drawing a
                // near-zero-length line or overlapping sphere caps produces black
                // pixel artifacts, especially during Scene View rotation.
                var tangentWorldLen = Vector3.Distance(wp, outWorld);
                var minHandleLen = HandleUtility.GetHandleSize(wp) * 0.25f;
                if (tangentWorldLen < minHandleLen)
                    continue;

                // Draw tangent lines (visual only — guard against non-Repaint events
                // to prevent handle rendering from leaking into docked neighbour windows)
                if (Event.current.type == EventType.Repaint)
                {
                    Handles.color = s_TangentLineColor;
                    Handles.DrawLine(wp, outWorld);
                    Handles.DrawLine(wp, inWorld);
                }

                var handleSize = HandleUtility.GetHandleSize(wp) * 0.07f;

                // Out-tangent handle (draggable)
                Handles.color = s_TangentHandleColor;
                EditorGUI.BeginChangeCheck();
                var newOut = Handles.FreeMoveHandle(outWorld, handleSize, Vector3.zero, Handles.SphereHandleCap);
                if (EditorGUI.EndChangeCheck())
                {
                    // Convert the dragged world position back to a tangent direction.
                    // The display was normalised to a fixed length, so we store the
                    // raw direction scaled by the drag distance ratio.
                    var dragWorldDir = newOut - wp;
                    var newTangentLocal = WorldDirToLocalDir(dragWorldDir, anchor);
                    newTangentLocal = ConstrainToEditPlane(newTangentLocal, editPlane);
                    tangents[i] = newTangentLocal;

                    var displayLen = HandleUtility.GetHandleSize(wp) * 0.4f;
                    var dirLocalWorld = LocalDirToWorldDir(newTangentLocal, anchor);
                    if (dirLocalWorld.sqrMagnitude > 1e-8f)
                        dirLocalWorld = dirLocalWorld.normalized * displayLen;
                    tangentWorldPositions[i * 2] = wp + dirLocalWorld;
                    tangentWorldPositions[i * 2 + 1] = wp - dirLocalWorld;

                    BeginSplineDrag(graphView, window);
                    s_TangentDragActive = true;
                    WriteTangentsToNode(node, tangents);
                    changed = true;
                }

                // In-tangent handle (draggable, mirrors out-tangent)
                Handles.color = s_TangentHandleColor;
                EditorGUI.BeginChangeCheck();
                var newIn = Handles.FreeMoveHandle(inWorld, handleSize, Vector3.zero, Handles.SphereHandleCap);
                if (EditorGUI.EndChangeCheck())
                {
                    var dragWorldDir = wp - newIn;
                    var newTangentLocal = WorldDirToLocalDir(dragWorldDir, anchor);
                    newTangentLocal = ConstrainToEditPlane(newTangentLocal, editPlane);
                    tangents[i] = newTangentLocal;

                    var displayLen = HandleUtility.GetHandleSize(wp) * 0.4f;
                    var dirLocalWorld = LocalDirToWorldDir(newTangentLocal, anchor);
                    if (dirLocalWorld.sqrMagnitude > 1e-8f)
                        dirLocalWorld = dirLocalWorld.normalized * displayLen;
                    tangentWorldPositions[i * 2] = wp + dirLocalWorld;
                    tangentWorldPositions[i * 2 + 1] = wp - dirLocalWorld;

                    BeginSplineDrag(graphView, window);
                    s_TangentDragActive = true;
                    WriteTangentsToNode(node, tangents);
                    changed = true;
                }
            }

            return changed;
        }

        private static Vector3 WorldDirToLocalDir(Vector3 worldDir, Transform anchor) =>
            anchor != null ? anchor.InverseTransformDirection(worldDir) : worldDir;

        private static Vector3 LocalDirToWorldDir(Vector3 localDir, Transform anchor) =>
            anchor != null ? anchor.TransformDirection(localDir) : localDir;

        private static void WriteTangentsToNode(PcgManifestNodeView node, IReadOnlyList<Vector3> tangents)
        {
            node.SetPropertyValue("tangents", PcgSplineControlPoints.SerializeTangents(tangents));
        }

        private static void InsertTangentAtIndex(
            PcgManifestNodeView node,
            PcgNodeData nodeData,
            List<Vector3> points,
            int insertIndex,
            bool closed)
        {
            if (!PcgSplineControlPoints.HasExplicitTangents(nodeData))
                return;

            var tangents = PcgSplineControlPoints.ParseTangents(
                nodeData.GetRaw("tangents")?.ToString() ?? "[]");
            if (tangents.Count != points.Count - 1)
                return;

            var computed = PcgSplineControlPoints.ComputeCatmullRomTangents(points, closed);
            tangents.Insert(insertIndex, computed.Count > insertIndex
                ? computed[insertIndex]
                : Vector3.zero);
            WriteTangentsToNode(node, tangents);
        }

        private static void DeleteTangentsAtIndices(
            PcgManifestNodeView node,
            PcgNodeData nodeData,
            List<int> sortedIndices)
        {
            if (!PcgSplineControlPoints.HasExplicitTangents(nodeData))
                return;

            var tangents = PcgSplineControlPoints.ParseTangents(
                nodeData.GetRaw("tangents")?.ToString() ?? "[]");
            if (tangents.Count == 0)
                return;

            foreach (var idx in sortedIndices)
            {
                if (idx >= 0 && idx < tangents.Count)
                    tangents.RemoveAt(idx);
            }

            WriteTangentsToNode(node, tangents);
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

        private static void DrawPcgModeEntryOverlay(SceneView sceneView, PcgGraphEditorWindow window)
        {
            Handles.BeginGUI();
            try
            {
                const float width = 200f;
                const float height = 64f;
                var area = new Rect(
                    SceneViewToolsPanelWidth + SceneOverlayMargin,
                    SceneOverlayMargin,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                GUILayout.BeginArea(area);
                GUILayout.Space(6f);
                var assetName = window.CurrentAssetPath;
                if (string.IsNullOrEmpty(assetName))
                    assetName = "untitled";
                GUILayout.Label($"PCG: {assetName}", EditorStyles.boldLabel);
                if (GUILayout.Button("Enter PCG Mode", GUILayout.Height(24f)))
                {
                    EnterPcgMode(window);
                    sceneView.Repaint();
                }
                GUILayout.EndArea();
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawPcgModeToolbar(SceneView sceneView, PcgGraphEditorWindow window)
        {
            var ctx = window.GraphView.SceneEditContext;

            Handles.BeginGUI();
            try
            {
                const float toolbarHeight = 26f;
                var toolbarArea = new Rect(
                    SceneViewToolsPanelWidth + SceneOverlayMargin,
                    SceneOverlayMargin,
                    260f,
                    toolbarHeight);
                GUI.Box(toolbarArea, GUIContent.none, EditorStyles.toolbar);

                GUILayout.BeginArea(toolbarArea);
                GUILayout.BeginHorizontal();

                // Object mode — cube icon
                if (IconToolbarButton(IconObjectActive, IconObjectNormal, "Object Mode", ctx.IsObjectMode))
                    window.GraphView.SetSceneMode(SceneEditLevel.Object, SceneEditDomain.None);

                // Spline CP — curve icon
                bool splineEnabled = ctx.SupportsDomain(SceneEditDomain.SplineControlPoint);
                GUI.enabled = splineEnabled;
                if (IconToolbarButton(IconSplineActive, IconSplineNormal, "Spline Control Points",
                        ctx.IsComponentMode && ctx.Domain == SceneEditDomain.SplineControlPoint))
                    window.GraphView.SetSceneMode(SceneEditLevel.Component, SceneEditDomain.SplineControlPoint);
                GUI.enabled = true;

                // Vertex / Edge / Face — disabled placeholders
                GUI.enabled = false;
                IconToolbarButton(IconVertexNormal, IconVertexNormal, "Vertex (not available)", false);
                IconToolbarButton(IconEdgeNormal, IconEdgeNormal, "Edge (not available)", false);
                IconToolbarButton(IconFaceNormal, IconFaceNormal, "Face (not available)", false);
                GUI.enabled = true;

                GUILayout.Space(4);

                // Display mode popup — short label
                var oldDisplay = s_OthersDisplay;
                s_OthersDisplay = (OthersDisplayMode)EditorGUILayout.EnumPopup(
                    s_OthersDisplay, EditorStyles.toolbarPopup,
                    GUILayout.Width(70));
                if (s_OthersDisplay != oldDisplay)
                    ApplyOthersDisplayMode();

                GUILayout.Space(4);

                // Exit — X icon
                var exitColor = GUI.color;
                GUI.color = new Color(1f, 0.7f, 0.5f);
                if (GUILayout.Button(new GUIContent(IconExit, "Exit PCG Mode"), EditorStyles.toolbarButton,
                        GUILayout.Width(24f)))
                {
                    ExitPcgMode();
                    sceneView.Repaint();
                }
                GUI.color = exitColor;

                GUILayout.EndHorizontal();
                GUILayout.EndArea();
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static bool IconToolbarButton(Texture2D activeIcon, Texture2D normalIcon, string tooltip, bool active)
        {
            var icon = active ? activeIcon : normalIcon;
            var content = new GUIContent(icon, tooltip);
            var oldBg = GUI.backgroundColor;
            if (active)
                GUI.backgroundColor = new Color(0.4f, 0.6f, 0.9f);
            var clicked = GUILayout.Button(content, EditorStyles.toolbarButton, GUILayout.Width(24f), GUILayout.Height(20f));
            GUI.backgroundColor = oldBg;
            return clicked;
        }

        private static void DrawPcgModeStatusOverlay(PcgGraphEditorWindow window)
        {
            var ctx = window.GraphView.SceneEditContext;
            var sel = window.GraphView.selection.OfType<PcgGraphNodeBase>().FirstOrDefault();
            var selText = sel != null ? sel.NodeType : "(nothing)";

            Handles.BeginGUI();
            try
            {
                const float width = 300f;
                const float height = 52f;
                var area = new Rect(
                    SceneViewToolsPanelWidth + SceneOverlayMargin,
                    SceneOverlayMargin + 30f,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                var paddedArea = new Rect(area.x + 6f, area.y, area.width - 6f, area.height);
                GUILayout.BeginArea(paddedArea);
                GUILayout.Space(4f);
                GUILayout.Label($"Mode: {ctx.Level}" + (ctx.Domain != SceneEditDomain.None ? $" / {ctx.Domain}" : ""), EditorStyles.boldLabel);
                GUILayout.Label($"Selected: {selText}", EditorStyles.miniLabel);
                GUILayout.EndArea();
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawSplineOverlay(PcgManifestNodeView node)
        {
            var nodeData = node.CollectData();
            var pointCount = PcgSplineControlPoints.GetEffectivePoints(nodeData).Count;
            var selectedIndices = GetSelectedPointIndices(node.NodeId, pointCount);
            var selectedCount = selectedIndices.Count;

            Handles.BeginGUI();
            try
            {
                const float width = 300f;
                const float height = 118f;
                var area = new Rect(
                    SceneViewToolsPanelWidth + SceneOverlayMargin,
                    SceneOverlayMargin + 30f,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                GUILayout.BeginArea(area);
                GUILayout.Space(6f);
                GUILayout.Label($"Create Spline — {node.GetDisplayTitle()}", EditorStyles.boldLabel);
                GUILayout.Label(
                    $"Points: {pointCount}   Selected: {selectedCount}",
                    EditorStyles.miniLabel);

                GUILayout.BeginHorizontal();
                GUI.enabled = pointCount >= 2;
                if (GUILayout.Button("Insert Point (I)", GUILayout.Height(22f)))
                {
                    if (InsertPointForNode(node, selectedIndices))
                    {
                        pointCount = PcgSplineControlPoints.GetEffectivePoints(node.CollectData()).Count;
                        selectedIndices = GetSelectedPointIndices(node.NodeId, pointCount);
                        selectedCount = selectedIndices.Count;
                    }
                }

                GUI.enabled = pointCount > 2 && selectedCount > 0;
                if (GUILayout.Button("Delete Point (Del)", GUILayout.Height(22f)))
                {
                    if (DeleteSelectedPointForNode(node))
                    {
                        pointCount = PcgSplineControlPoints.GetEffectivePoints(node.CollectData()).Count;
                        selectedIndices = GetSelectedPointIndices(node.NodeId, pointCount);
                        selectedCount = selectedIndices.Count;
                    }
                }

                GUI.enabled = true;
                GUILayout.EndHorizontal();

                GUILayout.Label(
                    "Click point to select · Shift+click multi-select · Ctrl+click segment insert · Drag green handles for tangent",
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
            var selectedIndices = GetSelectedPointIndices(target.NodeId, int.MaxValue);

            if (evt.keyCode == KeyCode.I)
            {
                if (InsertPointForNode(target, selectedIndices))
                {
                    evt.Use();
                    sceneView.Repaint();
                }

                return;
            }

            if (selectedIndices.Count == 0)
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
                evt.alt ||
                evt.control)
                return false;

            // If a handle is already hot (PositionHandle axis or FreeMoveHandle),
            // don't intercept — let the handle receive the drag.
            if (GUIUtility.hotControl != 0)
                return false;

            var bestIndex = -1;
            var bestDistance = float.MaxValue;
            for (var i = 0; i < worldPoints.Count; i++)
            {
                var distance = HandleUtility.DistanceToCircle(worldPoints[i], 0f);
                if (distance > 20f || distance >= bestDistance)
                    continue;

                bestDistance = distance;
                bestIndex = i;
            }

            if (bestIndex < 0)
            {
                // Click on empty space — deselect all (no modifiers)
                if (!evt.shift)
                {
                    SetSelectedPointIndices(nodeId, new HashSet<int>());
                    evt.Use();
                    return true;
                }
                return false;
            }

            if (evt.shift)
            {
                // Shift+click: toggle selection, consume event (no drag)
                evt.Use();
                var current = GetSelectedPointIndices(nodeId, worldPoints.Count);
                if (current.Contains(bestIndex))
                    current.Remove(bestIndex);
                else
                    current.Add(bestIndex);
                SetSelectedPointIndices(nodeId, current);
            }
            else
            {
                // Normal click on a point:
                // - If already selected: DON'T consume — let PositionHandle
                //   receive the MouseDown and start a drag.
                // - If newly selected: select and consume (user clicks again
                //   to drag, standard select-then-move UX).
                var current = GetSelectedPointIndices(nodeId, worldPoints.Count);
                if (current.Count == 1 && current.Contains(bestIndex))
                {
                    // Already solely selected — let event flow to PositionHandle
                    return true;
                }

                SetSelectedPointIndices(nodeId, new HashSet<int> { bestIndex });
                evt.Use();
            }

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
            Transform anchor)
        {
            var evt = Event.current;
            if (evt.type != EventType.MouseDown ||
                evt.button != 0 ||
                !evt.control ||
                evt.alt ||
                evt.shift)
                return false;

            var nodeData = node.CollectData();

            var ray = HandleUtility.GUIPointToWorldRay(evt.mousePosition);
            if (!TryPickSegment(worldPoints, closed, ray, 12f, out var segmentIndex, out var hitWorld))
                return false;

            var insertIndex = segmentIndex + 1;
            var localPoint = ConstrainToEditPlane(WorldToLocal(hitWorld, anchor) - sceneOffset, editPlane);

            graphView.WithUndo("Insert Spline Control Point", () =>
            {
                points.Insert(insertIndex, localPoint);
                WritePointsToNode(node, points, usesExplicit);
                InsertTangentAtIndex(node, nodeData, points, insertIndex, closed);
            });

            SetSelectedPointIndices(node.NodeId, new HashSet<int> { insertIndex });
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);

            Handles.color = s_SegmentPickColor;
            Handles.SphereHandleCap(0, hitWorld, Quaternion.identity, HandleUtility.GetHandleSize(hitWorld) * 0.08f, EventType.Repaint);

            evt.Use();
            HandleUtility.Repaint();
            return true;
        }

        private static bool InsertPointForNode(PcgManifestNodeView node, HashSet<int> selectedIndices)
        {
            if (!TryGetActiveContext(node, out var window, out var graphView))
                return false;

            var nodeData = node.CollectData();
            var usesExplicit = PcgSplineControlPoints.HasExplicitControlPoints(nodeData);
            var points = PcgSplineControlPoints.GetEffectivePoints(nodeData);
            if (points.Count < 2)
                return false;

            var closed = ReadBool(nodeData, "closed", false);

            var insertIndex = selectedIndices.Count > 0
                ? Mathf.Clamp(selectedIndices.Max() + 1, 0, points.Count)
                : points.Count;

            var left = points[Mathf.Clamp(insertIndex - 1, 0, points.Count - 1)];
            var right = points[Mathf.Clamp(insertIndex, 0, points.Count - 1)];
            var midpoint = ConstrainToEditPlane((left + right) * 0.5f, ReadEditPlane(nodeData));

            graphView.WithUndo("Insert Spline Control Point", () =>
            {
                points.Insert(insertIndex, midpoint);
                WritePointsToNode(node, points, usesExplicit);
                InsertTangentAtIndex(node, nodeData, points, insertIndex, closed);
            });

            SetSelectedPointIndices(node.NodeId, new HashSet<int> { insertIndex });
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
            var selectedIndices = GetSelectedPointIndices(node.NodeId, points.Count);
            if (selectedIndices.Count == 0 || points.Count <= 2)
                return false;

            if (points.Count - selectedIndices.Count < 2)
                return false;

            graphView.WithUndo("Delete Spline Control Points", () =>
            {
                var sorted = selectedIndices.OrderByDescending(i => i).ToList();
                foreach (var idx in sorted)
                    points.RemoveAt(idx);
                WritePointsToNode(node, points, usesExplicit);
                DeleteTangentsAtIndices(node, nodeData, sorted);
            });

            SetSelectedPointIndices(node.NodeId, new HashSet<int>());
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

        private static HashSet<int> GetSelectedPointIndices(string nodeId, int pointCount)
        {
            if (!s_SelectedPointByNode.TryGetValue(nodeId, out var indices) || indices.Count == 0)
                return new HashSet<int>();

            var valid = new HashSet<int>();
            foreach (var idx in indices)
            {
                if (idx >= 0 && idx < pointCount)
                    valid.Add(idx);
            }

            return valid;
        }

        private static void SetSelectedPointIndices(string nodeId, HashSet<int> indices)
        {
            s_SelectedPointByNode[nodeId] = indices;
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
            IReadOnlyList<Vector3> tangents,
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
                var p1 = localPoints[seg];
                var p2 = localPoints[(seg + 1) % count];
                // Hermite tangents at each endpoint
                var t1 = tangents[seg];
                var t2 = tangents[(seg + 1) % count];

                var endStep = seg == segmentCount - 1 && !closed ? steps : steps - 1;
                for (var step = 0; step <= endStep; step++)
                {
                    var t = step / (float)steps;
                    var local = CubicHermite(p1, t1, p2, t2, t) + sceneOffset;
                    samples.Add(LocalToWorld(local, anchor));
                }
            }

            return samples;
        }

        /// <summary>
        /// Cubic Hermite interpolation. Equivalent to Catmull-Rom when
        /// t1 = 0.5*(p2-p0), t2 = 0.5*(p3-p1).
        /// </summary>
        private static Vector3 CubicHermite(Vector3 p1, Vector3 t1, Vector3 p2, Vector3 t2, float t)
        {
            var t2v = t * t;
            var t3v = t2v * t;
            return (2f * t3v - 3f * t2v + 1f) * p1
                   + (t3v - 2f * t2v + t) * t1
                   + (-2f * t3v + 3f * t2v) * p2
                   + (t3v - t2v) * t2;
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
            s_TangentDragActive = false;
            s_DragGraphView = null;
            s_DragWindow = null;
        }

        /// <summary>
        /// Force-clear any stale drag state. Called by <see cref="PcgGraphView"/>
        /// when the user starts dragging a node in the GraphView, to prevent
        /// an unfinished Scene View handle drag from blocking the new drag.
        /// </summary>
        internal static void ForceClearDragState()
        {
            s_DragActive = false;
            s_HandleHot = false;
            s_TangentDragActive = false;
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
