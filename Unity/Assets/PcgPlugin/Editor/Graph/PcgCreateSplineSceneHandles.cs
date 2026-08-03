#if UNITY_EDITOR
using System.Collections.Generic;
using System.Linq;
using DJTechEditor.PCG;
using DJTechEditor.PCG.Rendering;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Houdini-style Scene View editing for spline authoring nodes
    /// selected in an open Graph Editor window.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgCreateSplineSceneHandles
    {
        private static readonly Color s_ControlLineColor = new(1f, 0.85f, 0.2f, 0.85f);
        private static readonly Color s_CurvePreviewColor = new(1f, 0.65f, 0.1f, 0.55f);
        private static readonly Color s_HandleColor = new(0.25f, 0.85f, 1f, 1f);
        private static readonly Color s_SelectedHandleColor = new(1f, 0.45f, 0.15f, 1f);
        private static readonly Color s_TangentLineColor = new(0.6f, 1f, 0.4f, 0.7f);
        private static readonly Color s_TangentHandleColor = new(0.4f, 0.9f, 0.3f, 1f);
        private static readonly Color s_TangentHandleActiveColor = new(0.7f, 1f, 0.5f, 1f);
        private static readonly Color s_UnselectedPointColor = new(0.5f, 0.5f, 0.5f, 0.6f);

        private const float SceneViewToolsPanelWidth = 48f;
        private const float SceneOverlayMargin = 12f;

        private static readonly Dictionary<string, HashSet<int>> s_SelectedPointByNode = new();
        private static readonly Dictionary<string, int> s_ActivePointByNode = new();
        private static GUIStyle s_ShortcutHintStyle;

        private static bool s_DragActive;
        private static bool s_HandleHot;
        private static PcgGraphView s_DragGraphView;
        private static PcgGraphEditorWindow s_DragWindow;
        private static ManualSplineDragMode s_ManualSplineDragMode;
        private static string s_ManualSplineDragNodeId;
        private static Vector2 s_ManualSplineDragStartMouse;
        private static Vector3 s_ManualSplineDragAxis;
        private static Vector2 s_ManualSplineDragScreenAxis;
        private static float s_ManualSplineDragWorldPerPixel;
        private static Plane s_ManualSplineDragPlane;
        private static Vector3 s_ManualSplineDragPlaneHit;
        private static List<Vector3> s_ManualSplineDragStartPoints;
        private static HashSet<int> s_ManualSplineDragSelection;

        private enum ManualSplineDragMode
        {
            None,
            Free,
            X,
            Y,
            Z,
        }

        private static bool s_PcgModeActive;
        private enum DiagnosticSceneViewStage
        {
            Production,
            FullIsolation,
            ToolLifecycleOnly,
            ReadOnlyStatusOverlay,
            InteractiveToolbar,
            CoreModeSetup,
            CoreSceneGuiShell,
            CoreShellWithGroupUi,
            PcgCoreSceneGui,
        }

        private static readonly DiagnosticSceneViewStage DiagnosticStage =
            DiagnosticSceneViewStage.Production;
        private static PcgGraphEditorWindow s_ActiveWindow;
        private static PcgGraphComponent s_ActiveComponent;

        /// <summary>True while Scene View toolbar is in Enter PCG Mode.</summary>
        internal static bool IsPcgModeActive => s_PcgModeActive;
        internal static bool IsSceneViewInputIsolated =>
            s_PcgModeActive &&
            DiagnosticStage != DiagnosticSceneViewStage.Production &&
            DiagnosticStage != DiagnosticSceneViewStage.CoreSceneGuiShell &&
            DiagnosticStage != DiagnosticSceneViewStage.CoreShellWithGroupUi &&
            DiagnosticStage != DiagnosticSceneViewStage.PcgCoreSceneGui;

        /// <summary>
        /// Independent SceneView tools remain enabled during the full PCG pipeline.
        /// Isolation is limited to explicitly selected diagnostic stages.
        /// </summary>
        internal static bool IsExternalSceneHandleIsolation =>
            s_PcgModeActive && DiagnosticStage != DiagnosticSceneViewStage.Production;

        /// <summary>Component locked for Scene View edits in PCG Mode; null when inactive.</summary>
        internal static PcgGraphComponent ActivePcgModeComponent => s_ActiveComponent;
        private static GameObject s_LockedSelection;
        private static bool s_SelectionGuard;
        private static Tool s_PrevTool = Tool.Move;
        private static bool s_HasPrevTool;
        private static bool s_ExitScheduled;
        private static bool s_ScheduledExitNotifiesGraphChanged;

        private const string SessionToolOverrideActive =
            "DJTechEditor.PCG.PcgMode.ToolOverrideActive";
        private const string SessionPreviousTool =
            "DJTechEditor.PCG.PcgMode.PreviousTool";

        // SceneView invokes duringSceneGui several times per frame. Resolve the selected
        // component/window only when selection changes instead of hitting AssetDatabase
        // and Resources.FindObjectsOfTypeAll on every layout/repaint event.
        private static GameObject s_CachedSelection;
        private static PcgGraphComponent s_CachedSelectionComponent;
        private static PcgGraphAsset s_CachedSelectionGraphAsset;
        private static string s_CachedSelectionAssetPath;
        private static PcgGraphEditorWindow s_CachedSelectionWindow;
        private static bool s_SelectionCacheDirty = true;

        private enum OthersDisplayMode
        {
            ShowAll,
            HideOthers,
            IsolatePCG,
        }

        private static OthersDisplayMode s_OthersDisplay = OthersDisplayMode.ShowAll;
        private static readonly Dictionary<Renderer, bool> s_HiddenRenderers = new();

        // Houdini-style marker display — independent of Vertex/Edge/Face Group View.
        private const string PrefDisplayPoints = "Pcg.Display.Points";
        private const string PrefDisplayEdges = "Pcg.Display.Edges";
        private static bool s_DisplayPoints = EditorPrefs.GetBool(PrefDisplayPoints, false);
        private static bool s_DisplayEdges = EditorPrefs.GetBool(PrefDisplayEdges, true);

        // --- Procedurally generated toolbar icons ---
        private static Texture2D IconObjectNormal => s_IconObjectNormal ??= MakeIcon(new(0.7f, 0.7f, 0.7f), DrawObjectIcon);
        private static Texture2D IconObjectActive => s_IconObjectActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawObjectIcon);
        private static Texture2D IconSplineNormal => s_IconSplineNormal ??= MakeIcon(new(0.7f, 0.7f, 0.7f), DrawSplineIcon);
        private static Texture2D IconSplineActive => s_IconSplineActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawSplineIcon);
        private static Texture2D IconVertexNormal => s_IconVertexNormal ??= MakeIcon(new(0.5f, 0.5f, 0.5f), DrawVertexIcon);
        private static Texture2D IconVertexActive => s_IconVertexActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawVertexIcon);
        private static Texture2D IconEdgeNormal => s_IconEdgeNormal ??= MakeIcon(new(0.5f, 0.5f, 0.5f), DrawEdgeIcon);
        private static Texture2D IconEdgeActive => s_IconEdgeActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawEdgeIcon);
        private static Texture2D IconFaceNormal => s_IconFaceNormal ??= MakeIcon(new(0.5f, 0.5f, 0.5f), DrawFaceIcon);
        private static Texture2D IconFaceActive => s_IconFaceActive ??= MakeIcon(new(0.4f, 0.6f, 0.9f), DrawFaceIcon);
        private static Texture2D IconExit => s_IconExit ??= MakeIcon(new(0.85f, 0.5f, 0.4f), DrawExitIcon);
        // Display markers — Points = Houdini blue; Edges keep orange so toggles stay distinct
        private static Texture2D IconDisplayPointsOff => s_IconDisplayPointsOff ??= MakeIcon(new(0.55f, 0.55f, 0.55f), DrawVertexIcon);
        private static Texture2D IconDisplayPointsOn => s_IconDisplayPointsOn ??= MakeIcon(new(0.25f, 0.55f, 1f), DrawVertexIcon);
        private static Texture2D IconDisplayEdgesOff => s_IconDisplayEdgesOff ??= MakeIcon(new(0.55f, 0.55f, 0.55f), DrawEdgeIcon);
        private static Texture2D IconDisplayEdgesOn => s_IconDisplayEdgesOn ??= MakeIcon(new(1f, 0.65f, 0.12f), DrawEdgeIcon);
        private static Texture2D IconGroupListOff => s_IconGroupListOff ??= MakeIcon(new(0.55f, 0.55f, 0.55f), DrawGroupListIcon);
        private static Texture2D IconGroupListOn => s_IconGroupListOn ??= MakeIcon(new(0.35f, 0.85f, 0.45f), DrawGroupListIcon);

        private static Texture2D s_IconObjectNormal, s_IconObjectActive;
        private static Texture2D s_IconSplineNormal, s_IconSplineActive;
        private static Texture2D s_IconVertexNormal, s_IconEdgeNormal, s_IconFaceNormal;
        private static Texture2D s_IconVertexActive, s_IconEdgeActive, s_IconFaceActive;
        private static Texture2D s_IconExit;
        private static Texture2D s_IconDisplayPointsOff, s_IconDisplayPointsOn;
        private static Texture2D s_IconDisplayEdgesOff, s_IconDisplayEdgesOn;
        private static Texture2D s_IconGroupListOff, s_IconGroupListOn;

        // --- Group viewer state (Houdini-style list + hover preview) ---
        private static string s_SelectedGroupName;
        private static string s_SelectedGroupSource;
        private static string s_SelectedGroupDomain;
        private static string s_SelectedGroupNodeId;
        private static string s_HoverGroupName;
        private static string s_HoverGroupSource;
        private static string s_HoverGroupDomain;
        private static bool s_GroupListOpen;
        private static string s_GroupListFilter = "*";
        private static Vector2 s_GroupListScroll;
        private static readonly List<GroupInfo> s_AvailableGroups = new();

        // Houdini viewport group highlight (prim selection orange)
        private static readonly Color s_GroupFaceFill = new(1f, 0.45f, 0.08f, 0.28f);
        private static readonly Color s_GroupFaceOutline = new(1f, 0.55f, 0.12f, 0.95f);

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

        private static void DrawGroupListIcon(Color[] px, int size, Color c)
        {
            // Stacked rows — Houdini group-list affordance
            int m = 3;
            for (int row = 0; row < 3; row++)
            {
                int y = m + row * 4;
                for (int x = m; x < size - m; x++)
                {
                    SetPx(px, size, x, y, c);
                    SetPx(px, size, x, y + 1, c);
                }
                // Left color swatch
                SetPx(px, size, m, y, c);
                SetPx(px, size, m + 1, y, c);
                SetPx(px, size, m, y + 1, c);
                SetPx(px, size, m + 1, y + 1, c);
            }
        }

        static PcgCreateSplineSceneHandles()
        {
            RecoverInterruptedEditorToolOverride();

            SceneView.duringSceneGui -= OnSceneGui;
            Selection.selectionChanged -= OnSelectionChanged;
            AssemblyReloadEvents.beforeAssemblyReload -= OnBeforeAssemblyReload;
            EditorApplication.playModeStateChanged -= OnPlayModeStateChanged;
            SceneView.duringSceneGui += OnSceneGui;
            Selection.selectionChanged += OnSelectionChanged;
            AssemblyReloadEvents.beforeAssemblyReload += OnBeforeAssemblyReload;
            EditorApplication.playModeStateChanged += OnPlayModeStateChanged;
        }

        private static void OnBeforeAssemblyReload()
        {
            if (s_PcgModeActive || HasPersistedEditorToolOverride())
                ExitPcgMode(notifyGraphChanged: false);
        }

        private static void OnPlayModeStateChanged(PlayModeStateChange state)
        {
            if (state == PlayModeStateChange.ExitingEditMode &&
                (s_PcgModeActive || HasPersistedEditorToolOverride()))
            {
                ExitPcgMode(notifyGraphChanged: false);
            }
        }

        private static void OnSelectionChanged()
        {
            s_SelectionCacheDirty = true;

            if (!s_PcgModeActive || s_LockedSelection == null || s_SelectionGuard)
                return;

            if (ShouldRestoreLockedSelection())
            {
                // Block scene/hierarchy object picks only. Project-window asset
                // selections keep activeGameObject null and must not be overridden.
                RestoreLockedSelection();
            }
        }

        /// <summary>
        /// True when PCG Mode should snap Selection back to the locked scene object.
        /// Project assets and other non-scene objects are left alone.
        /// </summary>
        private static bool ShouldRestoreLockedSelection()
        {
            if (!s_PcgModeActive || s_LockedSelection == null)
                return false;

            var activeGo = Selection.activeGameObject;
            if (activeGo == s_LockedSelection)
                return false;

            if (activeGo != null)
                return IsObjectInLoadedScene(activeGo);

            // Cleared scene selection, or a Project/Inspector asset is active.
            var activeObject = Selection.activeObject;
            if (activeObject != null && !IsObjectInLoadedScene(activeObject))
                return false;

            return true;
        }

        private static bool IsObjectInLoadedScene(Object obj)
        {
            if (obj is GameObject go)
                return go.scene.IsValid();
            if (obj is Component component)
                return component.gameObject.scene.IsValid();
            return false;
        }

        private static void RestoreLockedSelection()
        {
            if (s_LockedSelection == null || s_SelectionGuard)
                return;

            s_SelectionGuard = true;
            try
            {
                Selection.activeGameObject = s_LockedSelection;
            }
            finally
            {
                s_SelectionGuard = false;
            }
        }

        private static void OnSceneGui(SceneView sceneView)
        {
            if (Application.isPlaying)
                return;

            if (IsSceneViewInputIsolated)
            {
                // Diagnostic stages restore a single UI layer at a time. Custom Handles,
                // selection ownership and preview drawing remain disabled until each layer
                // has proved it cannot interfere with Unity's native transform gizmos.
                if (s_ActiveWindow != null)
                {
                    if (DiagnosticStage == DiagnosticSceneViewStage.ReadOnlyStatusOverlay ||
                        DiagnosticStage == DiagnosticSceneViewStage.InteractiveToolbar ||
                        DiagnosticStage == DiagnosticSceneViewStage.CoreModeSetup)
                    {
                        DrawPcgModeStatusOverlay(sceneView, s_ActiveWindow);
                    }

                    if (DiagnosticStage == DiagnosticSceneViewStage.InteractiveToolbar ||
                        DiagnosticStage == DiagnosticSceneViewStage.CoreModeSetup)
                        DrawPcgModeToolbar(sceneView, s_ActiveWindow);
                }
                return;
            }

            // Do not rely solely on Selection.selectionChanged here. Some editor input
            // paths apply the new selection just before SceneGUI is invoked, so the
            // callback can arrive after this frame. Polling keeps PCG Mode's scene
            // selection lock in effect for those paths too.
            if (s_PcgModeActive && s_LockedSelection != null && ShouldRestoreLockedSelection())
                RestoreLockedSelection();

            // Show the toolbar/entry when the selected object has a PcgGraphComponent,
            // regardless of whether a Graph Editor window is open.
            RefreshSelectionCacheIfNeeded();
            var component = s_PcgModeActive && s_ActiveComponent != null
                ? s_ActiveComponent
                : s_CachedSelectionComponent;

            if (component == null || component.GraphAsset == null)
            {
                if (s_PcgModeActive)
                    ExitPcgMode();
                return;
            }

            var graphWindow = s_PcgModeActive && s_ActiveComponent == component
                ? s_ActiveWindow
                : s_CachedSelectionWindow;

            if (!s_PcgModeActive)
            {
                DrawPcgModeEntryOverlay(sceneView, graphWindow, component);
                return;
            }

            // In PCG mode, keep using the active window. If selection changed to
            // a different component, exit so the user can re-enter with the new one.
            if (s_ActiveComponent != null && s_ActiveComponent != component)
            {
                ExitPcgMode();
                DrawPcgModeEntryOverlay(sceneView, graphWindow, component);
                return;
            }

            if (graphWindow == null)
            {
                ExitPcgMode();
                DrawPcgModeEntryOverlay(sceneView, null, component);
                return;
            }

            var splineNodes = new List<(PcgGraphEditorWindow window, PcgGraphView graphView, PcgManifestNodeView node)>();
            foreach (var node in graphWindow.GraphView.selection.OfType<PcgManifestNodeView>())
            {
                if (IsEditableSplineNode(node.NodeType))
                    splineNodes.Add((graphWindow, graphWindow.GraphView, node));
            }

            var ctx = graphWindow.GraphView.SceneEditContext;
            SyncEditorToolWithSceneContext(ctx);
            var customPcgHandlesEnabled =
                DiagnosticStage != DiagnosticSceneViewStage.CoreSceneGuiShell &&
                DiagnosticStage != DiagnosticSceneViewStage.CoreShellWithGroupUi;
            var groupUiEnabled =
                DiagnosticStage != DiagnosticSceneViewStage.CoreSceneGuiShell;
            var splineHandlesActive =
                customPcgHandlesEnabled &&
                splineNodes.Count > 0 &&
                ctx.IsComponentMode &&
                ctx.Domain == SceneEditDomain.SplineControlPoint;
            if (splineHandlesActive)
            {
                HandleSplineShortcuts(sceneView, splineNodes);

                foreach (var (window, graphView, node) in splineNodes)
                    DrawNodeSpline(sceneView, window, graphView, node);
            }

            if (customPcgHandlesEnabled)
                TryEndSplineDrag();

            // Register and process all 3D handles before entering any IMGUI overlay.
            // GUILayout controls may consume MouseDown, which prevents Unity's native
            // PositionHandle from acquiring hotControl when overlays run first.
            DrawPolygonWireOverlay(sceneView, graphWindow);
            DrawPcgModeToolbar(sceneView, graphWindow);

            if (splineHandlesActive)
            {
                DrawSplineOverlay(sceneView, splineNodes[0].node);
            }
            else if (groupUiEnabled && s_GroupListOpen)
            {
                // Panel is opt-in via right-strip Group List button only — never auto-open.
                var groupNode = graphWindow.GraphView.selection.OfType<PcgManifestNodeView>().FirstOrDefault();
                if (groupNode != null)
                {
                    DrawGroupViewerOverlay(sceneView, graphWindow, groupNode);
                    DrawNodeGroupHighlight(sceneView, graphWindow);
                }
                else
                {
                    DrawPcgModeStatusOverlay(sceneView, graphWindow);
                }
            }
            else
            {
                DrawPcgModeStatusOverlay(sceneView, graphWindow);
            }

            // Selection locking is handled by OnSelectionChanged. Do not register an
            // AddDefaultControl fallback here: if PCG Mode exits before that control
            // receives its matching MouseUp, it can leave GUIUtility.hotControl owned
            // by a control that no longer exists and disable every Scene View handle.
        }

        private static void RefreshSelectionCacheIfNeeded()
        {
            var selected = Selection.activeGameObject;
            if (!s_SelectionCacheDirty && s_CachedSelection == selected)
            {
                // Reading the serialized reference is cheap and catches an Inspector
                // graph reassignment without polling GetComponent/AssetDatabase.
                var currentGraphAsset = s_CachedSelectionComponent != null
                    ? s_CachedSelectionComponent.GraphAsset
                    : null;
                if (s_CachedSelectionGraphAsset == currentGraphAsset)
                    return;
            }

            var component = selected != null ? selected.GetComponent<PcgGraphComponent>() : null;
            var graphAsset = component != null ? component.GraphAsset : null;
            s_CachedSelection = selected;
            s_CachedSelectionComponent = component;
            s_CachedSelectionGraphAsset = graphAsset;
            s_CachedSelectionAssetPath = graphAsset != null
                ? AssetDatabase.GetAssetPath(graphAsset)
                : null;
            // A null window is intentionally cached too. The entry button performs a
            // fresh lookup when clicked, so an idle SceneView never polls globally.
            s_CachedSelectionWindow = FindGraphWindowForComponent(component);
            s_SelectionCacheDirty = false;
        }

        private static PcgGraphEditorWindow FindGraphWindowForComponent(PcgGraphComponent component)
        {
            if (component == null || component.GraphAsset == null)
                return null;

            // Fast path: in PCG mode the active window/component pair is already known;
            // skip per-frame AssetDatabase.GetAssetPath / AssetPathToGUID lookups.
            if (s_PcgModeActive && s_ActiveComponent == component &&
                s_ActiveWindow != null && s_ActiveWindow.HasLoadedGraph)
                return s_ActiveWindow;

            var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
            var assetGuid = AssetDatabase.AssetPathToGUID(assetPath);

            // If already in PCG mode, keep using the active window if it matches.
            if (s_PcgModeActive && s_ActiveWindow != null && s_ActiveWindow.HasLoadedGraph &&
                s_ActiveWindow.MatchesGraphAsset(assetPath, assetGuid))
                return s_ActiveWindow;

            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window != null && window.HasLoadedGraph && window.GraphView != null &&
                    window.MatchesGraphAsset(assetPath, assetGuid))
                    return window;
            }
            return null;
        }

        private static void EnterPcgMode(PcgGraphEditorWindow window, PcgGraphComponent component)
        {
            s_PcgModeActive = true;
            s_ActiveWindow = window;
            s_ActiveComponent = component;
            s_CachedSelectionWindow = window;

            if (DiagnosticStage == DiagnosticSceneViewStage.FullIsolation)
            {
                // Baseline for user repro: do not touch selection, tools, renderers,
                // graph context, or SceneView callbacks. Exit through the temporary
                // PCG/Diagnostics menu item below.
                s_LockedSelection = null;
                Debug.Log("[PCG] SceneView input isolation is active. Use PCG/Diagnostics/Exit Isolated PCG Mode to exit.");
                return;
            }

            if (DiagnosticStage == DiagnosticSceneViewStage.ToolLifecycleOnly ||
                DiagnosticStage == DiagnosticSceneViewStage.ReadOnlyStatusOverlay ||
                DiagnosticStage == DiagnosticSceneViewStage.InteractiveToolbar)
            {
                CaptureEditorToolForPcgMode();
                SyncEditorToolWithSceneContext(PcgSceneEditContext.ObjectMode);
                Debug.Log("[PCG] Tool-lifecycle diagnostic is active. All SceneView callbacks remain disabled; use PCG/Diagnostics/Exit Isolated PCG Mode to exit.");
                return;
            }

            if (DiagnosticStage == DiagnosticSceneViewStage.CoreModeSetup)
            {
                // Restore the graph context and selection ownership used by production
                // PCG Mode, but leave every custom 3D Handle disabled. This verifies
                // that mode setup itself never removes Unity's object transform tool.
                if (window != null && window.GraphView != null)
                {
                    window.GraphView.RefreshSceneEditContext();
                    window.GraphView.SetSceneMode(SceneEditLevel.Object, SceneEditDomain.None);
                }

                if (component != null)
                    s_LockedSelection = component.gameObject;

                CaptureEditorToolForPcgMode();
                var setupContext = window != null && window.GraphView != null
                    ? window.GraphView.SceneEditContext
                    : PcgSceneEditContext.ObjectMode;
                SyncEditorToolWithSceneContext(setupContext);
                Debug.Log("[PCG] Core mode-setup diagnostic is active. Custom SceneView handles remain disabled.");
                return;
            }
            // Enter in Object Mode. The GraphView normally retains its last selected
            // node, which can be a spline/group node; deriving the context from that
            // selection here immediately switches Tools.current to None and makes the
            // just-entered Scene View look as though its transform gizmo is broken.
            // Component modes remain available through the PCG toolbar.
            if (window != null && window.GraphView != null)
            {
                window.GraphView.RefreshSceneEditContext();
                window.GraphView.SetSceneMode(SceneEditLevel.Object, SceneEditDomain.None);
                window.GraphView.EnsureMatchSizeScenePreview();
            }

            s_OthersDisplay = OthersDisplayMode.HideOthers;

            // Use the selected component's transform as the preview anchor directly.
            var anchor = component != null ? component.transform : FindPreviewAnchor(window);
            if (anchor != null)
                s_LockedSelection = anchor.gameObject;

            CaptureEditorToolForPcgMode();
            var context = window != null && window.GraphView != null
                ? window.GraphView.SceneEditContext
                : PcgSceneEditContext.ObjectMode;
            SyncEditorToolWithSceneContext(context);
            ApplyOthersDisplayMode();

            // Frame the Scene View on the selected object.
            if (s_LockedSelection != null)
            {
                var renderers = s_LockedSelection.GetComponentsInChildren<Renderer>();
                if (renderers.Length > 0)
                {
                    var bounds = renderers[0].bounds;
                    for (var i = 1; i < renderers.Length; i++)
                        bounds.Encapsulate(renderers[i].bounds);
                    SceneView.lastActiveSceneView?.Frame(bounds, false);
                }
            }
        }

        private static void ExitPcgMode(bool notifyGraphChanged = true)
        {
            // Toolbar and overlay buttons invoke this while Handles.BeginGUI is still
            // active. Releasing hotControl inside that callback is too early: EndGUI
            // can restore the button's control id afterwards, which blocks every
            // native Move/Rotate/Scale handle. Finish the teardown on the next editor
            // tick, after the current IMGUI event has completed.
            if (Event.current != null)
            {
                SchedulePcgModeExit(notifyGraphChanged);
                return;
            }

            ExitPcgModeNow(notifyGraphChanged);
        }

        private static void SchedulePcgModeExit(bool notifyGraphChanged)
        {
            if (s_ExitScheduled)
            {
                // A no-cook shutdown (assembly reload / play-mode transition) wins.
                s_ScheduledExitNotifiesGraphChanged &= notifyGraphChanged;
                return;
            }

            s_ExitScheduled = true;
            s_ScheduledExitNotifiesGraphChanged = notifyGraphChanged;
            EditorApplication.delayCall += CompleteScheduledPcgModeExit;
        }

        private static void CompleteScheduledPcgModeExit()
        {
            var notifyGraphChanged = s_ScheduledExitNotifiesGraphChanged;
            s_ExitScheduled = false;
            s_ScheduledExitNotifiesGraphChanged = false;
            ExitPcgModeNow(notifyGraphChanged);
        }

        [MenuItem("PCG/Diagnostics/Exit Isolated PCG Mode", false, 999)]
        private static void ExitIsolatedPcgMode()
        {
            if (s_PcgModeActive)
                ExitPcgModeNow(notifyGraphChanged: false);
        }

        [MenuItem("PCG/Diagnostics/Exit Isolated PCG Mode", true)]
        private static bool ValidateExitIsolatedPcgMode() =>
            s_PcgModeActive;

        private static void ExitPcgModeNow(bool notifyGraphChanged)
        {
            try
            {
                FinishActiveSplineInteraction(notifyGraphChanged);
                PcgStampOverlaySceneHandles.ForceClearInteractionState(notifyGraphChanged);

                if (s_LockedSelection != null)
                {
                    var gv = s_LockedSelection.GetComponent<PcgGroupVisualizer>();
                    gv?.ClearHighlight();
                }

                ClearGroupHover();
                RestoreHiddenRenderers();
                PcgHeightFieldMaskOverlaySceneHandles.DestroyCachedResources();
            }
            catch (System.Exception exception)
            {
                // Cleanup must continue even if a final cook or preview teardown fails.
                Debug.LogException(exception);
            }
            finally
            {
                PcgScenePreviewRenderer.ReleaseAll();
                s_PcgModeActive = false;
                s_ActiveWindow = null;
                s_ActiveComponent = null;
                s_LockedSelection = null;
                s_SelectedPointByNode.Clear();
                s_ActivePointByNode.Clear();
                s_SelectedGroupName = null;
                s_SelectedGroupSource = null;
                s_SelectedGroupDomain = null;
                s_SelectedGroupNodeId = null;
                s_GroupListOpen = false;
                s_GroupListFilter = "*";
                s_GroupListScroll = Vector2.zero;
                s_AvailableGroups.Clear();
                s_SelectionGuard = false;

                try
                {
                    RestoreEditorToolAfterPcgMode();
                }
                catch (System.Exception exception)
                {
                    Debug.LogException(exception);
                    Tools.current = Tool.Move;
                    SessionState.EraseBool(SessionToolOverrideActive);
                    SessionState.EraseInt(SessionPreviousTool);
                }
                finally
                {
                    ReleaseSceneViewInputState();
                    SceneView.RepaintAll();
                }
            }
        }

        private static void CaptureEditorToolForPcgMode()
        {
            s_PrevTool = GetRestorableEditorTool(Tools.current);
            s_HasPrevTool = true;
            PersistEditorToolOverride();
        }

        private static void SyncEditorToolWithSceneContext(PcgSceneEditContext context)
        {
            if (!s_HasPrevTool)
                CaptureEditorToolForPcgMode();

            if (context.IsComponentMode)
            {
                // Component handles own Scene View input. Preserve a tool selected
                // while in Object Mode, then hide Unity's object transform handle.
                if (Tools.current != Tool.None)
                {
                    s_PrevTool = GetRestorableEditorTool(Tools.current);
                    PersistEditorToolOverride();
                }

                Tools.current = GetEditorToolForSceneContext(context, s_PrevTool);
                return;
            }

            // Object Mode must keep Unity's native transform gizmo usable.
            if (Tools.current == Tool.None)
            {
                Tools.current = GetRestorableEditorTool(s_PrevTool);
                return;
            }

            // Remember W/E/R/T changes made while PCG Object Mode is active.
            s_PrevTool = GetRestorableEditorTool(Tools.current);
            PersistEditorToolOverride();
        }

        private static void RestoreEditorToolAfterPcgMode()
        {
            var persisted = HasPersistedEditorToolOverride();
            var fallback = persisted
                ? (Tool)SessionState.GetInt(SessionPreviousTool, (int)Tool.Move)
                : s_PrevTool;

            if (s_HasPrevTool || persisted || Tools.current == Tool.None)
                Tools.current = GetRestorableEditorTool(fallback);

            s_HasPrevTool = false;
            SessionState.EraseBool(SessionToolOverrideActive);
            SessionState.EraseInt(SessionPreviousTool);
        }

        private static void RecoverInterruptedEditorToolOverride()
        {
            if (!HasPersistedEditorToolOverride())
                return;

            var previous = (Tool)SessionState.GetInt(SessionPreviousTool, (int)Tool.Move);
            Tools.current = GetRestorableEditorTool(previous);
            SessionState.EraseBool(SessionToolOverrideActive);
            SessionState.EraseInt(SessionPreviousTool);
        }

        private static void PersistEditorToolOverride()
        {
            SessionState.SetBool(SessionToolOverrideActive, true);
            SessionState.SetInt(
                SessionPreviousTool,
                (int)GetRestorableEditorTool(s_PrevTool));
        }

        private static bool HasPersistedEditorToolOverride() =>
            SessionState.GetBool(SessionToolOverrideActive, false);

        internal static Tool GetRestorableEditorTool(Tool tool) =>
            tool == Tool.None ? Tool.Move : tool;

        internal static Tool GetEditorToolForSceneContext(
            PcgSceneEditContext context,
            Tool previousTool) =>
            context.IsComponentMode
                ? Tool.None
                : GetRestorableEditorTool(previousTool);

        private static void ReleaseSceneViewInputState()
        {
            // Exiting an editor mode is a hard ownership boundary. A control registered
            // only by PCG Mode cannot receive MouseUp after the mode disappears, so
            // release both IMGUI capture channels explicitly.
            GUIUtility.hotControl = 0;
            GUIUtility.keyboardControl = 0;
            EditorGUIUtility.SetWantsMouseJumping(0);
        }

        private static void FinishActiveSplineInteraction(bool notifyGraphChanged)
        {
            try
            {
                if (s_DragActive && s_DragGraphView != null)
                {
                    s_DragGraphView.EndDrag();
                    if (notifyGraphChanged && s_DragWindow != null)
                        PcgGraphEditorCookBridge.NotifyGraphChanged(s_DragWindow, immediate: true);
                }
            }
            catch (System.Exception exception)
            {
                Debug.LogException(exception);
            }
            finally
            {
                ForceClearDragState();
            }
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
                s_HiddenRenderers[renderer] = renderer.enabled;
                renderer.enabled = false;
            }
        }

        private static void RestoreHiddenRenderers()
        {
            foreach (var (renderer, originalEnabled) in s_HiddenRenderers)
            {
                if (renderer != null)
                    renderer.enabled = originalEnabled;
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
            var changed = false;

            if (TryAddOrInsertPointClick(
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

            if (mode == "catmullRom")
            {
                var previewTangents = PcgSplineControlPoints.GetTangents(points, nodeData, closed);
                var preview = SampleHermiteSpline(points, previewTangents, sceneOffset, anchor, closed, ReadInt(nodeData, "subdivisions", 8));
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

            var activeIndex = GetActivePointIndex(node.NodeId, selectedIndices, points.Count);
            var selectionChangedThisEvent = HandleRawSplinePointSelection(
                sceneView,
                node,
                worldPoints,
                activeIndex,
                ref selectedIndices);

            // Tangents retain priority when their own handles are hit. Unselected
            // control points use the raw, hit-tested selection path above, while the
            // active point keeps Unity's native transform-handle appearance.
            if (selectedIndices.Count > 0 && !selectionChangedThisEvent)
            {
                changed |= DrawTangentHandles(
                    sceneView, window, graphView, node, points, worldPoints,
                    tangents, tangentWorldPositions, sceneOffset, editPlane,
                    usesExplicit, hasExplicitTangents, closed, anchor, selectedIndices);
            }

            activeIndex = GetActivePointIndex(node.NodeId, selectedIndices, points.Count);

            if (Event.current.type == EventType.Repaint)
            {
                for (var i = 0; i < worldPoints.Length; i++)
                {
                    var isSelected = selectedIndices.Contains(i);
                    if (i == activeIndex)
                        continue;

                    Handles.color = isSelected ? s_SelectedHandleColor : s_UnselectedPointColor;
                    var markerSize = HandleUtility.GetHandleSize(worldPoints[i]) * (isSelected ? 0.11f : 0.08f);
                    Handles.SphereHandleCap(
                        0,
                        worldPoints[i],
                        Quaternion.identity,
                        markerSize,
                        EventType.Repaint);
                }
            }

            if (activeIndex >= 0 && !selectionChangedThisEvent)
            {
                var activeWorld = worldPoints[activeIndex];
                var handleRotation = Tools.pivotRotation == PivotRotation.Local && anchor != null
                    ? anchor.rotation
                    : Quaternion.identity;

                EditorGUI.BeginChangeCheck();
                var newWorld = Handles.PositionHandle(activeWorld, handleRotation);
                if (EditorGUI.EndChangeCheck())
                {
                    BeginSplineDrag(graphView, window);

                    var delta = newWorld - activeWorld;
                    foreach (var selectedIndex in selectedIndices)
                    {
                        var movedWorld = worldPoints[selectedIndex] + delta;
                        points[selectedIndex] = WorldToLocal(movedWorld, anchor) - sceneOffset;
                    }

                    WritePointsToNode(node, points, usesExplicit);
                    changed = true;
                }
            }

            if (!changed)
                return;

            graphView.CommitState();
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: false);
            sceneView.Repaint();
            HandleUtility.Repaint();
        }

        private static bool IsEditableSplineNode(string nodeType) =>
            nodeType == "CreateSpline" || nodeType == "CreateBezierSpline";

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
                    newTangentLocal = ConstrainToEditPlane(newTangentLocal, tangents[i], editPlane);
                    tangents[i] = newTangentLocal;

                    var displayLen = HandleUtility.GetHandleSize(wp) * 0.4f;
                    var dirLocalWorld = LocalDirToWorldDir(newTangentLocal, anchor);
                    if (dirLocalWorld.sqrMagnitude > 1e-8f)
                        dirLocalWorld = dirLocalWorld.normalized * displayLen;
                    tangentWorldPositions[i * 2] = wp + dirLocalWorld;
                    tangentWorldPositions[i * 2 + 1] = wp - dirLocalWorld;

                    BeginSplineDrag(graphView, window);
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
                    newTangentLocal = ConstrainToEditPlane(newTangentLocal, tangents[i], editPlane);
                    tangents[i] = newTangentLocal;

                    var displayLen = HandleUtility.GetHandleSize(wp) * 0.4f;
                    var dirLocalWorld = LocalDirToWorldDir(newTangentLocal, anchor);
                    if (dirLocalWorld.sqrMagnitude > 1e-8f)
                        dirLocalWorld = dirLocalWorld.normalized * displayLen;
                    tangentWorldPositions[i * 2] = wp + dirLocalWorld;
                    tangentWorldPositions[i * 2 + 1] = wp - dirLocalWorld;

                    BeginSplineDrag(graphView, window);
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
            List<int> sortedIndices,
            int pointCountAfterDeletion)
        {
            if (!PcgSplineControlPoints.HasExplicitTangents(nodeData))
                return;

            var tangents = PcgSplineControlPoints.ParseTangents(
                nodeData.GetRaw("tangents")?.ToString() ?? "[]");
            if (tangents.Count == 0)
                return;

            if (tangents.Count != pointCountAfterDeletion + sortedIndices.Count)
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

        private static void DrawPcgModeEntryOverlay(SceneView sceneView, PcgGraphEditorWindow window, PcgGraphComponent component)
        {
            Handles.BeginGUI();
            try
            {
                const float width = 320f;
                const float height = 64f;
                var area = new Rect(
                    (sceneView.position.width - width) / 2f,
                    SceneOverlayMargin,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                GUILayout.BeginArea(area);
                GUILayout.Space(6f);
                var assetName = window != null ? window.CurrentAssetPath : null;
                if (string.IsNullOrEmpty(assetName) && s_CachedSelectionComponent == component)
                    assetName = s_CachedSelectionAssetPath;
                if (string.IsNullOrEmpty(assetName))
                    assetName = "untitled";
                GUILayout.Label($"PCG: {assetName}", EditorStyles.boldLabel);
                if (GUILayout.Button("Enter PCG Mode", GUILayout.Height(24f)))
                {
                    // If no graph window is open, open one as a tab next to
                    // the Scene View — same as Shader Graph.
                    if (window == null && component != null && component.GraphAsset != null)
                    {
                        var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
                        var guid = AssetDatabase.AssetPathToGUID(assetPath);

                        // Focus existing window with the same graph if one is open.
                        foreach (var w in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
                        {
                            if (w.selectedGuid == guid)
                            {
                                window = w;
                                break;
                            }
                        }

                        if (window == null)
                        {
                            window = EditorWindow.GetWindow<PcgGraphEditorWindow>(typeof(SceneView));
                            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>("Assets/PcgPlugin/Editor/Icons/pcg-icon-16.png");
                            window.titleContent = new GUIContent("PCG Graph", icon);
                            window.Initialize(guid);
                        }
                    }
                    if (window != null)
                        EnterPcgMode(window, component);
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
            Handles.BeginGUI();
            try
            {
                DrawPcgModeToolbarGui(sceneView, window);
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawPcgModeToolbarAndSidebar(
            SceneView sceneView, PcgGraphEditorWindow window, bool showGroupSidebar)
        {
            // Tuanjie loses the native transform gizmo after three independent
            // BeginGUI/EndGUI pairs in one duringSceneGui event. Keep the two always-on
            // PCG tool strips in one scope; other opt-in panels remain separate.
            Handles.BeginGUI();
            try
            {
                DrawPcgModeToolbarGui(sceneView, window);
                if (showGroupSidebar)
                    DrawGroupListSidebarGui(sceneView, window);
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawPcgModeToolbarGui(SceneView sceneView, PcgGraphEditorWindow window)
        {
            var ctx = window.GraphView.SceneEditContext;

            // Count visible buttons to compute dynamic toolbar width
            const float btnWidth = 24f;
            const float btnHeight = 20f;
            const float spacing = 4f;
            const float popupWidth = 70f;
            const float exitWidth = 24f;
            const float toolbarHeight = 26f;

            int buttonCount = 1; // Object always visible
            bool showSpline = ctx.SupportsDomain(SceneEditDomain.SplineControlPoint);
            if (showSpline) buttonCount++;
            // + Display Points/Edges + Group List (always) + popup + exit
            const int displayButtonCount = 3;
            float toolbarWidth = (buttonCount + displayButtonCount) * (btnWidth + 2f)
                + spacing * 2f + popupWidth + spacing + exitWidth + 6f;

            var toolbarArea = new Rect(
                (sceneView.position.width - toolbarWidth) / 2f,
                SceneOverlayMargin,
                toolbarWidth,
                toolbarHeight);
            GUI.Box(toolbarArea, GUIContent.none, EditorStyles.toolbar);
            var buttonRect = new Rect(
                toolbarArea.x + 3f,
                toolbarArea.y + 3f,
                btnWidth,
                btnHeight);

            // Object mode — cube icon
            if (IconToolbarButton(buttonRect, IconObjectActive, IconObjectNormal, "Object Mode", ctx.IsObjectMode))
            {
                s_GroupListOpen = false;
                ClearGroupHover();
                s_SelectedGroupName = null;
                s_SelectedGroupSource = null;
                s_SelectedGroupDomain = null;
                window.GraphView.SetSceneMode(SceneEditLevel.Object, SceneEditDomain.None);
            }
            buttonRect.x += btnWidth + 2f;

            // Spline CP — curve icon (only when supported)
            if (showSpline)
            {
                if (IconToolbarButton(buttonRect, IconSplineActive, IconSplineNormal, "Spline Control Points",
                        ctx.IsComponentMode && ctx.Domain == SceneEditDomain.SplineControlPoint))
                {
                    s_GroupListOpen = false;
                    ClearGroupHover();
                    window.GraphView.SetSceneMode(SceneEditLevel.Component, SceneEditDomain.SplineControlPoint);
                }
                buttonRect.x += btnWidth + 2f;
            }

            buttonRect.x += spacing;

            // Display markers — Houdini-style, independent of Group View selection
            var pointsBg = new Color(0.3f, 0.5f, 0.95f);
            var edgesBg = new Color(0.85f, 0.55f, 0.15f);
            if (IconToolbarButton(buttonRect, IconDisplayPointsOn, IconDisplayPointsOff,
                    "Display Points — show all geometry points (independent of groups)",
                    s_DisplayPoints, pointsBg))
            {
                s_DisplayPoints = !s_DisplayPoints;
                EditorPrefs.SetBool(PrefDisplayPoints, s_DisplayPoints);
                sceneView.Repaint();
            }
            buttonRect.x += btnWidth + 2f;

            if (IconToolbarButton(buttonRect, IconDisplayEdgesOn, IconDisplayEdgesOff,
                    "Display Edges — show polygon/curve edges (independent of groups)",
                    s_DisplayEdges, edgesBg))
            {
                s_DisplayEdges = !s_DisplayEdges;
                EditorPrefs.SetBool(PrefDisplayEdges, s_DisplayEdges);
                sceneView.Repaint();
            }
            buttonRect.x += btnWidth + 2f;

            if (IconToolbarButton(
                    buttonRect,
                    IconGroupListOn,
                    IconGroupListOff,
                    "Group List — all point/edge/face groups; hover to preview",
                    s_GroupListOpen,
                    new Color(0.3f, 0.7f, 0.4f)))
            {
                s_GroupListOpen = !s_GroupListOpen;
                if (!s_GroupListOpen)
                    ClearGroupHover();
                sceneView.Repaint();
            }
            buttonRect.x += btnWidth + 2f + spacing;

            // Display mode popup — short label
            var oldDisplay = s_OthersDisplay;
            var popupRect = new Rect(buttonRect.x, buttonRect.y, popupWidth, btnHeight);
            s_OthersDisplay = (OthersDisplayMode)EditorGUI.EnumPopup(
                popupRect, s_OthersDisplay, EditorStyles.toolbarPopup);
            if (s_OthersDisplay != oldDisplay)
                ApplyOthersDisplayMode();

            // Exit — X icon
            var exitRect = new Rect(popupRect.xMax + spacing, buttonRect.y, exitWidth, btnHeight);
            var exitColor = GUI.color;
            GUI.color = new Color(1f, 0.7f, 0.5f);
            if (GUI.Button(exitRect, new GUIContent(IconExit, "Exit PCG Mode"), EditorStyles.toolbarButton))
            {
                ExitPcgMode();
                sceneView.Repaint();
            }
            GUI.color = exitColor;
        }

        private static bool IconToolbarButton(
            Rect rect,
            Texture2D activeIcon,
            Texture2D normalIcon,
            string tooltip,
            bool active)
        {
            return IconToolbarButton(
                rect,
                activeIcon,
                normalIcon,
                tooltip,
                active,
                new Color(0.4f, 0.6f, 0.9f));
        }

        private static bool IconToolbarButton(
            Rect rect,
            Texture2D activeIcon, Texture2D normalIcon, string tooltip, bool active, Color activeBackground)
        {
            var icon = active ? activeIcon : normalIcon;
            var content = new GUIContent(icon, tooltip);
            var oldBg = GUI.backgroundColor;
            if (active)
                GUI.backgroundColor = activeBackground;
            var clicked = GUI.Button(rect, content, EditorStyles.toolbarButton);
            GUI.backgroundColor = oldBg;
            return clicked;
        }

        private static void DrawPcgModeStatusOverlay(SceneView sceneView, PcgGraphEditorWindow window)
        {
            var ctx = window.GraphView.SceneEditContext;
            var sel = window.GraphView.selection.OfType<PcgGraphNodeBase>().FirstOrDefault();
            var selText = sel != null ? sel.NodeType : "(nothing)";

            Handles.BeginGUI();
            try
            {
                const float width = 300f;
                const float height = 68f;
                var area = new Rect(
                    (sceneView.position.width - width) / 2f,
                    SceneOverlayMargin + 30f,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                var textX = area.x + 6f;
                var textWidth = area.width - 12f;
                GUI.Label(
                    new Rect(textX, area.y + 5f, textWidth, 18f),
                    $"Mode: {ctx.Level}" + (ctx.Domain != SceneEditDomain.None ? $" / {ctx.Domain}" : ""),
                    EditorStyles.boldLabel);
                GUI.Label(new Rect(textX, area.y + 24f, textWidth, 16f), $"Selected: {selText}", EditorStyles.miniLabel);
                var markers = (s_DisplayPoints ? "Pts " : "") + (s_DisplayEdges ? "Edges" : "");
                if (string.IsNullOrEmpty(markers))
                    markers = "off";
                GUI.Label(new Rect(textX, area.y + 41f, textWidth, 16f), $"Display: {markers.Trim()}", EditorStyles.miniLabel);
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawSplineOverlay(SceneView sceneView, PcgManifestNodeView node)
        {
            var nodeData = node.CollectData();
            var pointCount = PcgSplineControlPoints.GetEffectivePoints(nodeData).Count;
            var selectedIndices = GetSelectedPointIndices(node.NodeId, pointCount);
            var selectedCount = selectedIndices.Count;

            Handles.BeginGUI();
            try
            {
                const float width = 360f;
                const float height = 124f;
                var area = new Rect(
                    (sceneView.position.width - width) / 2f,
                    SceneOverlayMargin + 30f,
                    width,
                    height);
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);

                GUILayout.BeginArea(area);
                GUILayout.Space(6f);
                GUILayout.Label($"{node.GetDisplayTitle()}", EditorStyles.boldLabel);
                GUILayout.Label(
                    $"Points: {pointCount}   Selected: {selectedCount}",
                    EditorStyles.miniLabel);

                GUILayout.BeginHorizontal();
                if (GUILayout.Button("Add Point (A)", GUILayout.Height(22f)))
                {
                    if (AddPointForNode(node))
                    {
                        pointCount = PcgSplineControlPoints.GetEffectivePoints(node.CollectData()).Count;
                        selectedIndices = GetSelectedPointIndices(node.NodeId, pointCount);
                        selectedCount = selectedIndices.Count;
                    }
                }

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
                    "Click/drag point · Shift multi-select · Ctrl/Cmd+click segment inserts, empty space adds · Del deletes",
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

            if (evt.keyCode != KeyCode.A &&
                evt.keyCode != KeyCode.I &&
                evt.keyCode != KeyCode.Delete &&
                evt.keyCode != KeyCode.Backspace)
                return;

            var target = activeNodes[0].node;
            var selectedIndices = GetSelectedPointIndices(target.NodeId, int.MaxValue);

            if (evt.keyCode == KeyCode.A)
            {
                if (AddPointForNode(target))
                {
                    evt.Use();
                    sceneView.Repaint();
                }

                return;
            }

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

        private static void UpdatePointSelection(
            string nodeId,
            int pointCount,
            int pointIndex,
            bool toggle)
        {
            if (pointIndex < 0 || pointIndex >= pointCount)
                return;

            var selected = GetSelectedPointIndices(nodeId, pointCount);
            if (toggle)
            {
                if (selected.Contains(pointIndex))
                    selected.Remove(pointIndex);
                else
                    selected.Add(pointIndex);
            }
            else
            {
                selected.Clear();
                selected.Add(pointIndex);
            }

            SetSelectedPointIndices(nodeId, selected, selected.Contains(pointIndex) ? pointIndex : -1);
        }

        private static bool TryAddOrInsertPointClick(
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
            if (evt.rawType != EventType.MouseDown ||
                evt.button != 0 ||
                (!evt.control && !evt.command) ||
                evt.alt ||
                evt.shift)
                return false;

            var nodeData = node.CollectData();
            var ray = HandleUtility.GUIPointToWorldRay(evt.mousePosition);
            int insertIndex;
            Vector3 localPoint;
            var actionName = "Add Spline Control Point";

            if (TryPickSegment(worldPoints, closed, ray, 12f, out var segmentIndex, out var hitWorld))
            {
                actionName = "Insert Spline Control Point";
                insertIndex = segmentIndex + 1;
                localPoint = WorldToLocal(hitWorld, anchor) - sceneOffset;
                var segEndIndex = (segmentIndex + 1) % points.Count;
                var segT = SegmentBlendT(worldPoints[segmentIndex], worldPoints[segEndIndex], hitWorld);
                var onSegment = Vector3.Lerp(points[segmentIndex], points[segEndIndex], segT);
                localPoint = ConstrainToEditPlane(localPoint, onSegment, editPlane);
            }
            else
            {
                var selected = GetSelectedPointIndices(node.NodeId, points.Count);
                var activeIndex = GetActivePointIndex(node.NodeId, selected, points.Count);
                var referenceIndex = activeIndex >= 0 ? activeIndex : points.Count - 1;
                if (!TryProjectMouseToEditPlane(
                        sceneView,
                        ray,
                        points[referenceIndex],
                        sceneOffset,
                        editPlane,
                        anchor,
                        out hitWorld))
                    return false;

                insertIndex = points.Count;
                localPoint = WorldToLocal(hitWorld, anchor) - sceneOffset;
                localPoint = ConstrainToEditPlane(localPoint, points[referenceIndex], editPlane);
            }

            graphView.WithUndo(actionName, () =>
            {
                points.Insert(insertIndex, localPoint);
                WritePointsToNode(node, points, usesExplicit);
                InsertTangentAtIndex(node, nodeData, points, insertIndex, closed);
            });

            SetSelectedPointIndices(node.NodeId, new HashSet<int> { insertIndex }, insertIndex);
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);

            evt.Use();
            HandleUtility.Repaint();
            return true;
        }

        private static bool AddPointForNode(PcgManifestNodeView node)
        {
            if (!TryGetActiveContext(node, out var window, out var graphView))
                return false;

            var nodeData = node.CollectData();
            var usesExplicit = PcgSplineControlPoints.HasExplicitControlPoints(nodeData);
            var points = PcgSplineControlPoints.GetEffectivePoints(nodeData);
            if (points.Count < 2)
                return false;

            var closed = ReadBool(nodeData, "closed", false);
            Vector3 newPoint;
            if (closed)
            {
                newPoint = (points[^1] + points[0]) * 0.5f;
            }
            else
            {
                var extension = points[^1] - points[^2];
                newPoint = points[^1] + (extension.sqrMagnitude > 1e-8f ? extension : Vector3.right);
            }

            var insertIndex = points.Count;
            graphView.WithUndo("Add Spline Control Point", () =>
            {
                points.Add(newPoint);
                WritePointsToNode(node, points, usesExplicit);
                InsertTangentAtIndex(node, nodeData, points, insertIndex, closed);
            });

            SetSelectedPointIndices(node.NodeId, new HashSet<int> { insertIndex }, insertIndex);
            graphView.RefreshInspector();
            PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            SceneView.RepaintAll();
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
            var activeIndex = GetActivePointIndex(node.NodeId, selectedIndices, points.Count);
            var segmentStart = activeIndex >= 0 ? activeIndex : points.Count - 2;
            if (!closed && segmentStart >= points.Count - 1)
                segmentStart = points.Count - 2;

            var insertIndex = segmentStart + 1;
            var left = points[segmentStart];
            var right = points[(segmentStart + 1) % points.Count];
            var midpoint = (left + right) * 0.5f;

            graphView.WithUndo("Insert Spline Control Point", () =>
            {
                points.Insert(insertIndex, midpoint);
                WritePointsToNode(node, points, usesExplicit);
                InsertTangentAtIndex(node, nodeData, points, insertIndex, closed);
            });

            SetSelectedPointIndices(node.NodeId, new HashSet<int> { insertIndex }, insertIndex);
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
                DeleteTangentsAtIndices(node, nodeData, sorted, points.Count);
            });

            var nextIndex = Mathf.Clamp(selectedIndices.Min(), 0, points.Count - 1);
            SetSelectedPointIndices(node.NodeId, new HashSet<int> { nextIndex }, nextIndex);
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

        private static bool HandleRawSplinePointSelection(
            SceneView sceneView,
            PcgManifestNodeView node,
            IReadOnlyList<Vector3> worldPoints,
            int activeIndex,
            ref HashSet<int> selectedIndices)
        {
            var evt = Event.current;
            if (evt == null ||
                evt.rawType != EventType.MouseDown ||
                evt.button != 0 ||
                evt.alt ||
                evt.control ||
                evt.command ||
                GUIUtility.hotControl != 0)
            {
                return false;
            }

            const float pickRadius = 15f;
            var pointIndex = -1;
            var bestDistance = pickRadius;
            for (var i = 0; i < worldPoints.Count; i++)
            {
                // The active point is owned by Unity's PositionHandle center control.
                // Never consume its MouseDown here or the native gizmo cannot drag.
                if (i == activeIndex ||
                    sceneView.camera == null ||
                    sceneView.camera.WorldToScreenPoint(worldPoints[i]).z <= 0f)
                {
                    continue;
                }

                var distance = Vector2.Distance(
                    evt.mousePosition,
                    HandleUtility.WorldToGUIPoint(worldPoints[i]));
                if (distance > bestDistance)
                    continue;

                bestDistance = distance;
                pointIndex = i;
            }

            if (pointIndex < 0)
                return false;

            UpdatePointSelection(node.NodeId, worldPoints.Count, pointIndex, evt.shift);
            selectedIndices = GetSelectedPointIndices(node.NodeId, worldPoints.Count);
            if (evt.type != EventType.Used)
                evt.Use();
            sceneView.Repaint();
            return true;
        }

        private static bool HandleRawSplinePositionInput(
            SceneView sceneView,
            PcgGraphEditorWindow window,
            PcgGraphView graphView,
            PcgManifestNodeView node,
            List<Vector3> points,
            IReadOnlyList<Vector3> worldPoints,
            Vector3 sceneOffset,
            bool usesExplicit,
            Transform anchor,
            ref HashSet<int> selectedIndices)
        {
            var evt = Event.current;
            if (evt == null)
                return false;

            if (evt.rawType == EventType.MouseDown)
            {
                if (evt.button != 0 || evt.alt || evt.control || evt.command)
                    return false;

                var activeIndex = GetActivePointIndex(node.NodeId, selectedIndices, points.Count);
                var handleRotation = Tools.pivotRotation == PivotRotation.Local && anchor != null
                    ? anchor.rotation
                    : Quaternion.identity;

                if (activeIndex >= 0 &&
                    TryPickManualSplineHandle(
                        sceneView,
                        worldPoints[activeIndex],
                        handleRotation,
                        evt.mousePosition,
                        out var handleMode,
                        out var handleAxis,
                        out var screenAxis,
                        out var worldPerPixel))
                {
                    BeginManualSplineDrag(
                        sceneView,
                        window,
                        graphView,
                        node.NodeId,
                        points,
                        selectedIndices,
                        worldPoints[activeIndex],
                        handleMode,
                        handleAxis,
                        screenAxis,
                        worldPerPixel,
                        evt.mousePosition);
                    ConsumeSplineMouseEvent(evt);
                    return false;
                }

                if (!TryPickSplinePoint(sceneView, worldPoints, evt.mousePosition, out var pointIndex))
                    return false;

                UpdatePointSelection(node.NodeId, points.Count, pointIndex, evt.shift);
                selectedIndices = GetSelectedPointIndices(node.NodeId, points.Count);

                if (selectedIndices.Contains(pointIndex))
                {
                    BeginManualSplineDrag(
                        sceneView,
                        window,
                        graphView,
                        node.NodeId,
                        points,
                        selectedIndices,
                        worldPoints[pointIndex],
                        ManualSplineDragMode.Free,
                        Vector3.zero,
                        Vector2.zero,
                        0f,
                        evt.mousePosition);
                }

                ConsumeSplineMouseEvent(evt);
                sceneView.Repaint();
                return false;
            }

            if (evt.rawType == EventType.MouseDrag &&
                s_ManualSplineDragMode != ManualSplineDragMode.None &&
                s_ManualSplineDragNodeId == node.NodeId &&
                s_ManualSplineDragStartPoints != null &&
                s_ManualSplineDragSelection != null)
            {
                if (!TryGetManualSplineDragDelta(evt.mousePosition, out var worldDelta))
                    return false;

                foreach (var index in s_ManualSplineDragSelection)
                {
                    if (index < 0 ||
                        index >= points.Count ||
                        index >= s_ManualSplineDragStartPoints.Count)
                    {
                        continue;
                    }

                    var startWorld = LocalToWorld(
                        s_ManualSplineDragStartPoints[index] + sceneOffset,
                        anchor);
                    points[index] = WorldToLocal(startWorld + worldDelta, anchor) - sceneOffset;
                }

                WritePointsToNode(node, points, usesExplicit);
                ConsumeSplineMouseEvent(evt);
                sceneView.Repaint();
                return true;
            }

            if (evt.rawType == EventType.MouseUp &&
                s_ManualSplineDragMode != ManualSplineDragMode.None &&
                s_ManualSplineDragNodeId == node.NodeId)
            {
                EndManualSplineDrag();
                ConsumeSplineMouseEvent(evt);
                sceneView.Repaint();
            }

            return false;
        }

        private static void BeginManualSplineDrag(
            SceneView sceneView,
            PcgGraphEditorWindow window,
            PcgGraphView graphView,
            string nodeId,
            IReadOnlyList<Vector3> points,
            IReadOnlyCollection<int> selectedIndices,
            Vector3 activeWorld,
            ManualSplineDragMode mode,
            Vector3 axis,
            Vector2 screenAxis,
            float worldPerPixel,
            Vector2 mousePosition)
        {
            BeginSplineDrag(graphView, window);

            s_ManualSplineDragMode = mode;
            s_ManualSplineDragNodeId = nodeId;
            s_ManualSplineDragStartMouse = mousePosition;
            s_ManualSplineDragAxis = axis;
            s_ManualSplineDragScreenAxis = screenAxis;
            s_ManualSplineDragWorldPerPixel = worldPerPixel;
            s_ManualSplineDragStartPoints = new List<Vector3>(points);
            s_ManualSplineDragSelection = new HashSet<int>(selectedIndices);

            var cameraForward = sceneView != null && sceneView.camera != null
                ? sceneView.camera.transform.forward
                : Vector3.forward;
            s_ManualSplineDragPlane = new Plane(cameraForward, activeWorld);
            s_ManualSplineDragPlaneHit = activeWorld;

            if (mode == ManualSplineDragMode.Free)
            {
                var ray = HandleUtility.GUIPointToWorldRay(mousePosition);
                if (s_ManualSplineDragPlane.Raycast(ray, out var distance))
                    s_ManualSplineDragPlaneHit = ray.GetPoint(distance);
            }
        }

        private static void EndManualSplineDrag()
        {
            s_ManualSplineDragMode = ManualSplineDragMode.None;
            s_ManualSplineDragNodeId = null;
            s_ManualSplineDragStartPoints = null;
            s_ManualSplineDragSelection = null;
            s_ManualSplineDragAxis = Vector3.zero;
            s_ManualSplineDragScreenAxis = Vector2.zero;
            s_ManualSplineDragWorldPerPixel = 0f;
        }

        private static bool TryGetManualSplineDragDelta(
            Vector2 mousePosition,
            out Vector3 worldDelta)
        {
            worldDelta = Vector3.zero;
            if (s_ManualSplineDragMode == ManualSplineDragMode.None)
                return false;

            if (s_ManualSplineDragMode == ManualSplineDragMode.Free)
            {
                var ray = HandleUtility.GUIPointToWorldRay(mousePosition);
                if (!s_ManualSplineDragPlane.Raycast(ray, out var distance))
                    return false;

                worldDelta = ray.GetPoint(distance) - s_ManualSplineDragPlaneHit;
                return true;
            }

            if (s_ManualSplineDragScreenAxis.sqrMagnitude < 1e-6f ||
                s_ManualSplineDragAxis.sqrMagnitude < 1e-6f)
            {
                return false;
            }

            var pixels = Vector2.Dot(
                mousePosition - s_ManualSplineDragStartMouse,
                s_ManualSplineDragScreenAxis);
            var amount = pixels * s_ManualSplineDragWorldPerPixel;
            var snap = s_ManualSplineDragMode switch
            {
                ManualSplineDragMode.X => Mathf.Abs(EditorSnapSettings.move.x),
                ManualSplineDragMode.Y => Mathf.Abs(EditorSnapSettings.move.y),
                ManualSplineDragMode.Z => Mathf.Abs(EditorSnapSettings.move.z),
                _ => 0f,
            };
            if (snap > 1e-6f)
                amount = Mathf.Round(amount / snap) * snap;

            worldDelta = s_ManualSplineDragAxis.normalized * amount;
            return true;
        }

        private static bool TryPickSplinePoint(
            SceneView sceneView,
            IReadOnlyList<Vector3> worldPoints,
            Vector2 mousePosition,
            out int pointIndex)
        {
            pointIndex = -1;
            if (sceneView == null || sceneView.camera == null)
                return false;

            const float pickRadius = 15f;
            var bestDistance = pickRadius;
            for (var i = 0; i < worldPoints.Count; i++)
            {
                if (sceneView.camera.WorldToScreenPoint(worldPoints[i]).z <= 0f)
                    continue;

                var distance = Vector2.Distance(
                    mousePosition,
                    HandleUtility.WorldToGUIPoint(worldPoints[i]));
                if (distance > bestDistance)
                    continue;

                bestDistance = distance;
                pointIndex = i;
            }

            return pointIndex >= 0;
        }

        private static bool TryPickManualSplineHandle(
            SceneView sceneView,
            Vector3 position,
            Quaternion rotation,
            Vector2 mousePosition,
            out ManualSplineDragMode mode,
            out Vector3 axis,
            out Vector2 screenAxis,
            out float worldPerPixel)
        {
            mode = ManualSplineDragMode.None;
            axis = Vector3.zero;
            screenAxis = Vector2.zero;
            worldPerPixel = 0f;

            if (sceneView == null || sceneView.camera == null)
                return false;

            var centerGui = HandleUtility.WorldToGUIPoint(position);
            if (Vector2.Distance(mousePosition, centerGui) <= 14f)
            {
                mode = ManualSplineDragMode.Free;
                return true;
            }

            var handleSize = HandleUtility.GetHandleSize(position) * 0.8f;
            var bestDistance = 10f;
            var axes = new[]
            {
                (ManualSplineDragMode.X, rotation * Vector3.right),
                (ManualSplineDragMode.Y, rotation * Vector3.up),
                (ManualSplineDragMode.Z, rotation * Vector3.forward),
            };

            foreach (var candidate in axes)
            {
                var endpointGui = HandleUtility.WorldToGUIPoint(
                    position + candidate.Item2 * handleSize);
                var projected = endpointGui - centerGui;
                var projectedLength = projected.magnitude;
                if (projectedLength < 8f)
                    continue;

                var distance = DistanceToGuiSegment(
                    mousePosition,
                    centerGui + projected.normalized * 12f,
                    endpointGui);
                if (distance > bestDistance)
                    continue;

                bestDistance = distance;
                mode = candidate.Item1;
                axis = candidate.Item2.normalized;
                screenAxis = projected / projectedLength;
                worldPerPixel = handleSize / projectedLength;
            }

            return mode != ManualSplineDragMode.None;
        }

        private static float DistanceToGuiSegment(Vector2 point, Vector2 start, Vector2 end)
        {
            var segment = end - start;
            var lengthSquared = segment.sqrMagnitude;
            if (lengthSquared < 1e-6f)
                return Vector2.Distance(point, start);

            var t = Mathf.Clamp01(Vector2.Dot(point - start, segment) / lengthSquared);
            return Vector2.Distance(point, start + segment * t);
        }

        private static void DrawManualSplinePositionHandle(
            Vector3 position,
            Quaternion rotation)
        {
            if (Event.current.type != EventType.Repaint)
                return;

            var size = HandleUtility.GetHandleSize(position) * 0.8f;
            var capSize = size * 0.16f;
            var axes = new[]
            {
                (Handles.xAxisColor, rotation * Vector3.right),
                (Handles.yAxisColor, rotation * Vector3.up),
                (Handles.zAxisColor, rotation * Vector3.forward),
            };

            foreach (var candidate in axes)
            {
                var direction = candidate.Item2.normalized;
                var endpoint = position + direction * size;
                Handles.color = candidate.Item1;
                Handles.DrawAAPolyLine(3f, position, endpoint);
                Handles.ArrowHandleCap(
                    0,
                    endpoint,
                    Quaternion.LookRotation(direction),
                    capSize,
                    EventType.Repaint);
            }

            Handles.color = s_SelectedHandleColor;
            Handles.SphereHandleCap(
                0,
                position,
                Quaternion.identity,
                size * 0.13f,
                EventType.Repaint);
        }

        private static void ConsumeSplineMouseEvent(Event evt)
        {
            if (evt != null && evt.type != EventType.Used)
                evt.Use();
        }

        private static int GetActivePointIndex(
            string nodeId,
            IReadOnlyCollection<int> selectedIndices,
            int pointCount)
        {
            if (selectedIndices == null || selectedIndices.Count == 0)
                return -1;

            if (s_ActivePointByNode.TryGetValue(nodeId, out var activeIndex) &&
                activeIndex >= 0 &&
                activeIndex < pointCount &&
                selectedIndices.Contains(activeIndex))
                return activeIndex;

            activeIndex = selectedIndices.First();
            s_ActivePointByNode[nodeId] = activeIndex;
            return activeIndex;
        }

        private static void SetSelectedPointIndices(
            string nodeId,
            HashSet<int> indices,
            int activeIndex = -1)
        {
            s_SelectedPointByNode[nodeId] = indices;
            if (activeIndex >= 0 && indices.Contains(activeIndex))
            {
                s_ActivePointByNode[nodeId] = activeIndex;
                return;
            }

            if (indices.Count > 0)
                s_ActivePointByNode[nodeId] = indices.First();
            else
                s_ActivePointByNode.Remove(nodeId);
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
            var segmentLengthSq = Vector3.Dot(segment, segment);
            if (segmentLengthSq < 1e-8f || ray.direction.sqrMagnitude < 1e-8f)
                return false;

            var direction = ray.direction.normalized;
            var originToA = ray.origin - a;
            var directionDotSegment = Vector3.Dot(direction, segment);
            var directionDotOrigin = Vector3.Dot(direction, originToA);
            var segmentDotOrigin = Vector3.Dot(segment, originToA);
            var denominator = segmentLengthSq - directionDotSegment * directionDotSegment;

            var segmentT = denominator > 1e-8f
                ? Mathf.Clamp01(
                    (segmentDotOrigin - directionDotSegment * directionDotOrigin) /
                    denominator)
                : Mathf.Clamp01(segmentDotOrigin / segmentLengthSq);

            closest = a + segment * segmentT;
            var rayT = Vector3.Dot(closest - ray.origin, direction);
            if (rayT < 0f)
                return false;

            return true;
        }

        private static bool TryProjectMouseToEditPlane(
            SceneView sceneView,
            Ray ray,
            Vector3 referencePoint,
            Vector3 sceneOffset,
            string editPlane,
            Transform anchor,
            out Vector3 hitWorld)
        {
            hitWorld = Vector3.zero;
            var planePoint = LocalToWorld(referencePoint + sceneOffset, anchor);

            Vector3 localNormal;
            switch (editPlane)
            {
                case "xy":
                    localNormal = Vector3.forward;
                    break;
                case "xz":
                    localNormal = Vector3.up;
                    break;
                case "yz":
                    localNormal = Vector3.right;
                    break;
                default:
                    var camera = sceneView != null ? sceneView.camera : null;
                    var viewNormal = camera != null ? camera.transform.forward : ray.direction;
                    var viewPlane = new Plane(viewNormal, planePoint);
                    if (!viewPlane.Raycast(ray, out var viewDistance))
                        return false;
                    hitWorld = ray.GetPoint(viewDistance);
                    return true;
            }

            var worldNormal = LocalDirToWorldDir(localNormal, anchor).normalized;
            var editPlaneWorld = new Plane(worldNormal, planePoint);
            if (!editPlaneWorld.Raycast(ray, out var distance))
                return false;

            hitWorld = ray.GetPoint(distance);
            return true;
        }

        private static List<Vector3> SampleHermiteSpline(
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
            var mouseReleased = evt.type == EventType.MouseUp || evt.rawType == EventType.MouseUp;
            var hotControlReleased = s_HandleHot &&
                                     GUIUtility.hotControl == 0 &&
                                     (evt.type == EventType.Repaint || evt.type == EventType.Layout);

            if (!mouseReleased && !hotControlReleased)
                return;

            EndManualSplineDrag();
            s_DragGraphView.EndDrag();
            if (s_DragWindow != null)
                PcgGraphEditorCookBridge.NotifyGraphChanged(s_DragWindow, immediate: true);

            s_DragActive = false;
            s_HandleHot = false;
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
            EndManualSplineDrag();
            s_DragActive = false;
            s_HandleHot = false;
            s_DragGraphView = null;
            s_DragWindow = null;
        }

        private static Transform FindPreviewAnchor(PcgGraphEditorWindow window)
        {
            // When in PCG mode with a known active component, use it directly.
            if (s_ActiveComponent != null)
                return s_ActiveComponent.transform;

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

        private static Vector3 ConstrainToEditPlane(Vector3 local, Vector3 original, string editPlane) =>
            editPlane switch
            {
                "xy" => new Vector3(local.x, local.y, original.z),
                "xz" => new Vector3(local.x, original.y, local.z),
                "yz" => new Vector3(original.x, local.y, local.z),
                _ => local,
            };

        private static float SegmentBlendT(Vector3 a, Vector3 b, Vector3 p)
        {
            var ab = b - a;
            var lenSq = ab.sqrMagnitude;
            return lenSq < 1e-8f ? 0f : Mathf.Clamp01(Vector3.Dot(p - a, ab) / lenSq);
        }

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

        // ─── Group Viewer ─────────────────────────────────────────

        [System.Serializable]
        private class GroupJsonEntry
        {
            public string name;
            public string domain;
            public int count;
            public long[] members;
            public float[] edgeEndpoints;
        }

        [System.Serializable]
        private class GroupJsonWrapper
        {
            public GroupJsonEntry[] groups;
        }

        private struct GroupInfo
        {
            public string name;
            public string domain;
            public int count;
            public long[] members;
            public float[] edgeEndpoints;
        }

        private static bool IsGroupDomain(SceneEditDomain domain) =>
            domain == SceneEditDomain.Vertex ||
            domain == SceneEditDomain.Edge ||
            domain == SceneEditDomain.Face;

        private static string DomainToString(SceneEditDomain domain) => domain switch
        {
            SceneEditDomain.Vertex => "point",
            SceneEditDomain.Edge => "edge",
            SceneEditDomain.Face => "face",
            _ => "",
        };

        private static SceneEditDomain StringToDomain(string domain) => domain switch
        {
            "point" => SceneEditDomain.Vertex,
            "edge" => SceneEditDomain.Edge,
            "face" => SceneEditDomain.Face,
            _ => SceneEditDomain.None,
        };

        private static void ClearGroupHover()
        {
            s_HoverGroupName = null;
            s_HoverGroupSource = null;
            s_HoverGroupDomain = null;
        }

        private static void GetEffectiveGroupSelection(
            out string name, out string source, out string domain)
        {
            if (!string.IsNullOrEmpty(s_HoverGroupName))
            {
                name = s_HoverGroupName;
                source = s_HoverGroupSource;
                domain = s_HoverGroupDomain;
                return;
            }

            name = s_SelectedGroupName;
            source = s_SelectedGroupSource;
            domain = s_SelectedGroupDomain;
        }

        private static Color DomainSwatchColor(string domain) => domain switch
        {
            "point" => new Color(0.95f, 0.35f, 0.35f, 1f),
            "edge" => new Color(0.2f, 0.9f, 0.75f, 1f),
            "face" => new Color(0.35f, 0.85f, 0.4f, 1f),
            _ => new Color(0.7f, 0.7f, 0.7f, 1f),
        };

        private static bool GroupNameMatchesFilter(string name, string filter)
        {
            if (string.IsNullOrEmpty(filter) || filter == "*")
                return true;
            var f = filter.Trim();
            if (f == "*")
                return true;
            if (f.StartsWith("*") && f.EndsWith("*") && f.Length >= 2)
                return name.IndexOf(f.Substring(1, f.Length - 2), System.StringComparison.OrdinalIgnoreCase) >= 0;
            if (f.StartsWith("*"))
                return name.EndsWith(f.Substring(1), System.StringComparison.OrdinalIgnoreCase);
            if (f.EndsWith("*"))
                return name.StartsWith(f.Substring(0, f.Length - 1), System.StringComparison.OrdinalIgnoreCase);
            return name.IndexOf(f, System.StringComparison.OrdinalIgnoreCase) >= 0;
        }

        /// <summary>
        /// Houdini-style vertical strip on the right edge: Group List toggle only.
        /// All point/edge/face groups appear together in the floating list.
        /// </summary>
        private static void DrawGroupListSidebar(SceneView sceneView, PcgGraphEditorWindow window)
        {
            Handles.BeginGUI();
            try
            {
                DrawGroupListSidebarGui(sceneView, window);
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawGroupListSidebarGui(SceneView sceneView, PcgGraphEditorWindow window)
        {
            // Always show in PCG mode — group preview is a core debug affordance.
            sceneView.wantsMouseMove = s_GroupListOpen;

            const float barW = 32f;
            const float btn = 26f;
            const float pad = 3f;
            float barH = pad * 2f + btn + 4f;
            float viewW = sceneView.position.width;
            float viewH = sceneView.position.height;
            float barY = Mathf.Max(40f, (viewH - barH) * 0.35f);
            var barRect = new Rect(viewW - barW, barY, barW, barH);

            EditorGUI.DrawRect(barRect, new Color(0.18f, 0.18f, 0.18f, 0.92f));
            EditorGUI.DrawRect(new Rect(barRect.x, barRect.y, 1f, barRect.height),
                new Color(0.08f, 0.08f, 0.08f, 1f));

            var buttonRect = new Rect(barRect.x + pad, barRect.y + pad, btn, btn);
            if (s_GroupListOpen)
                EditorGUI.DrawRect(buttonRect, new Color(0.3f, 0.7f, 0.4f));

            var icon = s_GroupListOpen ? IconGroupListOn : IconGroupListOff;
            if (icon != null)
                GUI.DrawTexture(buttonRect, icon, ScaleMode.ScaleToFit, true);

            // Do not use GUILayout.Button here. In Tuanjie SceneView, allocating a
            // further IMGUI control ID beside native transform handles can leave their
            // hotControl unusable even though this button was never clicked.
            var evt = Event.current;
            if (evt != null && evt.type == EventType.MouseDown && evt.button == 0 &&
                buttonRect.Contains(evt.mousePosition))
            {
                s_GroupListOpen = !s_GroupListOpen;
                if (!s_GroupListOpen)
                    ClearGroupHover();
                evt.Use();
                sceneView.Repaint();
            }
        }

        private static bool SidebarIconButton(
            Texture2D icon, string tooltip, bool active, Color activeBackground, float size)
        {
            var content = new GUIContent(icon, tooltip);
            var oldBg = GUI.backgroundColor;
            if (active)
                GUI.backgroundColor = activeBackground;
            var clicked = GUILayout.Button(content, EditorStyles.toolbarButton,
                GUILayout.Width(size), GUILayout.Height(size));
            GUI.backgroundColor = oldBg;
            return clicked;
        }

        private static void ParseGroupsFromJson(string json, List<GroupInfo> output)
        {
            output.Clear();
            if (string.IsNullOrEmpty(json))
                return;
            try
            {
                var root = JsonUtility.FromJson<GroupJsonWrapper>(json);
                if (root?.groups == null)
                    return;
                foreach (var g in root.groups)
                {
                    output.Add(new GroupInfo
                    {
                        name = g.name,
                        domain = g.domain,
                        count = g.count,
                        members = g.members,
                        edgeEndpoints = g.edgeEndpoints,
                    });
                }
            }
            catch
            {
                // JSON may not be in expected format
            }
        }

        private static void DrawGroupViewerOverlay(SceneView sceneView, PcgGraphEditorWindow window, PcgManifestNodeView node)
        {
            if (!s_GroupListOpen)
                return;

            // Show every domain together — no Point/Edge/Face filter.
            var outputGroups = new List<PcgNodeInspector.AvailableGroup>();
            PcgNodeInspector.CollectNodeGroups(node, outputGroups, new HashSet<string>());

            var inputGroups = new List<PcgNodeInspector.AvailableGroup>();
            var inspector = window.GraphView.Inspector;
            if (inspector != null)
                inputGroups = inspector.ResolveUpstreamGroups(node.NodeId);

            var graphView = window.GraphView;
            List<NodeGroupEntry> nodeGroups = null;
            if (graphView != null && graphView.TryGetNodeGroups(node.NodeId, out var perNodeGroups))
                nodeGroups = perNodeGroups;

            // Nodes like GroupTransfer have no manifest outputGroups — surface cook-time
            // groups so Distance Threshold / membership changes are visible and selectable.
            if (nodeGroups != null)
            {
                var seenCook = new HashSet<string>();
                foreach (var g in outputGroups)
                    seenCook.Add($"{g.name}:{g.domain}");
                foreach (var g in nodeGroups)
                {
                    if (string.IsNullOrEmpty(g.name))
                        continue;
                    var domain = string.IsNullOrEmpty(g.domain) ? "edge" : g.domain;
                    if (!seenCook.Add($"{g.name}:{domain}"))
                        continue;
                    outputGroups.Add(new PcgNodeInspector.AvailableGroup
                    {
                        name = g.name,
                        domain = domain,
                        sourceNodeId = node.NodeId,
                        sourceNodeType = node.NodeType,
                        label = g.name,
                    });
                }
            }

            // Prefer this node's cook/declared output over same-named upstream groups
            // (otherwise GroupTransfer distance tweaks look like "no effect" when the
            // list only highlights the source GroupCreate membership).
            if (outputGroups.Count > 0 && inputGroups.Count > 0)
            {
                var outputKeys = new HashSet<string>();
                foreach (var g in outputGroups)
                    outputKeys.Add($"{g.name}:{g.domain}");
                inputGroups = inputGroups
                    .Where(g => !outputKeys.Contains($"{g.name}:{g.domain}"))
                    .ToList();
            }

            List<NodeGroupEntry> inputNodeGroups = null;
            if (inspector != null && inputGroups.Count > 0)
            {
                var firstInput = inputGroups[0];
                if (graphView != null && graphView.TryGetNodeGroups(firstInput.sourceNodeId, out var upstreamGroups))
                    inputNodeGroups = upstreamGroups;
            }

            NodeGroupEntry FindGroupStats(string name, string domain, bool fromOutput)
            {
                var list = fromOutput ? nodeGroups : inputNodeGroups;
                if (list == null) return null;
                var exact = list.FirstOrDefault(g => g.name == name && g.domain == domain);
                if (exact != null) return exact;
                // Cook domain is authoritative when declaration lagged (e.g. static "edge").
                return list.FirstOrDefault(g => g.name == name);
            }

            var allGroups = outputGroups.Concat(inputGroups).ToList();

            if (s_SelectedGroupNodeId != node.NodeId)
            {
                s_SelectedGroupNodeId = node.NodeId;
                s_SelectedGroupName = null;
                s_SelectedGroupSource = null;
                s_SelectedGroupDomain = null;
                ClearGroupHover();
            }

            GetEffectiveGroupSelection(out var effName, out _, out var effDomain);
            if (!string.IsNullOrEmpty(effName) &&
                !allGroups.Any(g => g.name == effName &&
                    (string.IsNullOrEmpty(effDomain) || g.domain == effDomain)))
            {
                if (s_SelectedGroupName == effName)
                {
                    s_SelectedGroupName = null;
                    s_SelectedGroupSource = null;
                    s_SelectedGroupDomain = null;
                }
                if (s_HoverGroupName == effName)
                    ClearGroupHover();
            }

            var rows = new List<(string name, string domain, string source, int? count, bool pinned)>();
            void AddRows(List<PcgNodeInspector.AvailableGroup> groups, string source, bool fromOutput)
            {
                foreach (var g in groups)
                {
                    if (!GroupNameMatchesFilter(g.name, s_GroupListFilter))
                        continue;
                    var stats = FindGroupStats(g.name, g.domain, fromOutput);
                    var domain = !string.IsNullOrEmpty(stats?.domain) ? stats.domain : g.domain;
                    rows.Add((g.name, domain, source, stats?.count,
                        s_SelectedGroupName == g.name &&
                        s_SelectedGroupSource == source &&
                        s_SelectedGroupDomain == domain));
                }
            }
            AddRows(outputGroups, "output", true);
            AddRows(inputGroups, "input", false);

            // Stable order: face → edge → point, then name.
            int DomainRank(string d) => d switch
            {
                "face" => 0,
                "edge" => 1,
                "point" => 2,
                _ => 3,
            };
            rows.Sort((a, b) =>
            {
                var c = DomainRank(a.domain).CompareTo(DomainRank(b.domain));
                return c != 0 ? c : string.CompareOrdinal(a.name, b.name);
            });

            const float panelW = 260f;
            const float rowH = 20f;
            const float headerH = 52f;
            // Unity Scene View orientation gizmo sits in the top-right; keep clear of it.
            const float navGizmoClearanceY = 118f;
            const float rightStripW = 32f;
            float maxListH = Mathf.Min(280f, Mathf.Max(60f, rows.Count * rowH + 8f));
            float panelH = headerH + maxListH + 8f;
            float viewW = sceneView.position.width;
            float viewH = sceneView.position.height;
            // Sit left of the right strip, below the nav gizmo (Houdini-style mid-right).
            float panelX = viewW - panelW - rightStripW - 6f;
            float panelY = Mathf.Min(
                Mathf.Max(navGizmoClearanceY, (viewH - panelH) * 0.28f),
                Mathf.Max(navGizmoClearanceY, viewH - panelH - 24f));
            var area = new Rect(panelX, panelY, panelW, panelH);

            var mouse = Event.current.mousePosition;
            bool mouseInPanel = area.Contains(mouse);
            if (!mouseInPanel && Event.current.type == EventType.MouseMove && !string.IsNullOrEmpty(s_HoverGroupName))
            {
                ClearGroupHover();
                sceneView.Repaint();
            }

            Handles.BeginGUI();
            try
            {
                GUI.Box(area, GUIContent.none, EditorStyles.helpBox);
                GUILayout.BeginArea(area);

                GUILayout.Space(4f);
                GUILayout.BeginHorizontal();
                GUILayout.Label("Groups", EditorStyles.boldLabel);
                GUILayout.FlexibleSpace();
                GUILayout.Label("all", EditorStyles.miniLabel);
                GUILayout.EndHorizontal();

                EditorGUI.BeginChangeCheck();
                s_GroupListFilter = EditorGUILayout.TextField(s_GroupListFilter ?? "*", EditorStyles.toolbarSearchField);
                if (EditorGUI.EndChangeCheck())
                    sceneView.Repaint();

                s_GroupListScroll = GUILayout.BeginScrollView(s_GroupListScroll, GUILayout.Height(maxListH));

                if (rows.Count == 0)
                {
                    GUILayout.Label(
                        allGroups.Count == 0
                            ? "No groups on this node."
                            : "No groups match filter.",
                        EditorStyles.miniLabel);
                }
                else
                {
                    for (var i = 0; i < rows.Count; i++)
                    {
                        var row = rows[i];
                        var rowRect = GUILayoutUtility.GetRect(
                            GUIContent.none, GUI.skin.label,
                            GUILayout.ExpandWidth(true), GUILayout.Height(rowH));

                        bool hovered = rowRect.Contains(Event.current.mousePosition);
                        bool isActive = (s_HoverGroupName == row.name && s_HoverGroupSource == row.source &&
                                         s_HoverGroupDomain == row.domain)
                            || (string.IsNullOrEmpty(s_HoverGroupName) && row.pinned);

                        if (Event.current.type == EventType.Repaint)
                        {
                            if (isActive)
                                EditorGUI.DrawRect(rowRect, new Color(0.25f, 0.45f, 0.7f, 0.45f));
                            else if (hovered)
                                EditorGUI.DrawRect(rowRect, new Color(0.35f, 0.35f, 0.35f, 0.35f));

                            var swatch = new Rect(rowRect.x + 4f, rowRect.y + 4f, 12f, 12f);
                            EditorGUI.DrawRect(swatch, DomainSwatchColor(row.domain));
                            var labelRect = new Rect(rowRect.x + 22f, rowRect.y, rowRect.width - 28f, rowRect.height);
                            var countSuffix = row.count.HasValue ? $" ({row.count.Value})" : "";
                            var prefix = row.source == "input" ? "← " : "";
                            GUI.Label(labelRect, $"{prefix}{row.name} [{row.domain}]{countSuffix}", EditorStyles.miniLabel);
                        }

                        if (hovered && Event.current.type == EventType.MouseMove)
                        {
                            if (s_HoverGroupName != row.name || s_HoverGroupSource != row.source ||
                                s_HoverGroupDomain != row.domain)
                            {
                                s_HoverGroupName = row.name;
                                s_HoverGroupSource = row.source;
                                s_HoverGroupDomain = row.domain;
                                sceneView.Repaint();
                            }
                        }

                        if (Event.current.type == EventType.MouseDown &&
                            Event.current.button == 0 &&
                            rowRect.Contains(Event.current.mousePosition))
                        {
                            if (row.pinned)
                            {
                                s_SelectedGroupName = null;
                                s_SelectedGroupSource = null;
                                s_SelectedGroupDomain = null;
                            }
                            else
                            {
                                s_SelectedGroupName = row.name;
                                s_SelectedGroupSource = row.source;
                                s_SelectedGroupDomain = row.domain;
                            }
                            Event.current.Use();
                            sceneView.Repaint();
                        }
                    }
                }

                GUILayout.EndScrollView();
                GUILayout.EndArea();
            }
            finally
            {
                Handles.EndGUI();
            }
        }

        private static void DrawPolygonWireOverlay(SceneView sceneView, PcgGraphEditorWindow window)
        {
            if (Event.current.type != EventType.Repaint)
                return;

            if (sceneView == null || sceneView.camera == null)
                return;

            // Nothing to draw when both Houdini-style marker toggles are off.
            if (!s_DisplayEdges && !s_DisplayPoints)
                return;

            var anchor = FindPreviewAnchor(window);
            if (anchor == null)
                return;

            var component = anchor.GetComponent<PcgGraphComponent>();
            if (component == null)
                return;

            // Only n-gon geometry_binary. Never draw MeshFilter triangles here — that shows
            // fan diagonals and looks like "preview forced triangulation".
            var preview = component.PolygonPreview;
            if (preview == null ||
                preview.Points == null || preview.FaceOffsets == null || preview.FaceIndices == null)
                return;

            if (preview.Points.Length == 0)
                return;

            PcgScenePreviewRenderer.DrawPolygon(
                component,
                anchor,
                preview,
                Camera.current != null ? Camera.current : sceneView.camera,
                s_DisplayEdges,
                s_DisplayPoints);
        }

        private static void DrawFacePolygonsHighlight(Matrix4x4 localToWorld, float[] packed)
        {
            var cursor = 0;
            while (cursor < packed.Length)
            {
                var vertCount = Mathf.RoundToInt(packed[cursor++]);
                if (vertCount < 3)
                    break;

                var floatsNeeded = vertCount * 3;
                if (cursor + floatsNeeded > packed.Length)
                    break;

                var world = new Vector3[vertCount];
                for (var i = 0; i < vertCount; ++i)
                {
                    world[i] = localToWorld.MultiplyPoint(new Vector3(
                        packed[cursor], packed[cursor + 1], packed[cursor + 2]));
                    cursor += 3;
                }

                Handles.color = s_GroupFaceFill;
                Handles.DrawAAConvexPolygon(world);
                Handles.color = s_GroupFaceOutline;
                // Close the ring for the outline.
                var outline = new Vector3[vertCount + 1];
                for (var i = 0; i < vertCount; ++i)
                    outline[i] = world[i];
                outline[vertCount] = world[0];
                Handles.DrawAAPolyLine(3f, outline);
            }
        }

        private static void DrawNodeGroupHighlight(SceneView sceneView, PcgGraphEditorWindow window)
        {
            if (Event.current.type != EventType.Repaint)
                return;

            GetEffectiveGroupSelection(out var groupName, out var groupSource, out var domainStr);
            if (string.IsNullOrEmpty(groupName))
                return;

            var ctx = window.GraphView.SceneEditContext;
            if (string.IsNullOrEmpty(domainStr))
                domainStr = DomainToString(ctx.Domain);

            var anchor = FindPreviewAnchor(window);
            if (anchor == null)
                return;

            var component = anchor.GetComponent<PcgGraphComponent>();
            if (component == null)
                return;

            var l2w = anchor.localToWorldMatrix;
            var domain = StringToDomain(domainStr);

            // Find the selected node and its per-node group stats
            var graphView = window.GraphView;
            var selectedNode = graphView.selection.OfType<PcgManifestNodeView>().FirstOrDefault();
            if (selectedNode == null)
                return;

            List<NodeGroupEntry> nodeGroups = null;
            var sourceNodeId = selectedNode.NodeId;

            // If source is "input", look up the upstream source node's groups
            if (groupSource == "input")
            {
                var inspector = graphView.Inspector;
                if (inspector != null)
                {
                    var upstream = inspector.ResolveUpstreamGroups(selectedNode.NodeId);
                    var matchIdx = upstream.FindIndex(g => g.name == groupName && g.domain == domainStr);
                    if (matchIdx >= 0)
                        sourceNodeId = upstream[matchIdx].sourceNodeId;
                }
            }

            if (!graphView.TryGetNodeGroups(sourceNodeId, out nodeGroups))
                return;

            var group = nodeGroups.FirstOrDefault(g => g.name == groupName && g.domain == domainStr);
            if (group == null)
                return;

            // Face groups can render via facePolygons without depending on MeshFilter.
            bool hasEdgeEndpoints = group.edgeEndpoints != null && group.edgeEndpoints.Length >= 6;
            bool hasFacePolygons = group.facePolygons != null && group.facePolygons.Length >= 4;
            bool hasPointPositions = group.pointPositions != null && group.pointPositions.Length >= 3;
            if (!hasEdgeEndpoints && !hasFacePolygons && !hasPointPositions &&
                (group.members == null || group.members.Length == 0))
                return;

            if (domain == SceneEditDomain.Edge)
            {
                PcgScenePreviewRenderer.DrawGroup(
                    component,
                    anchor,
                    group,
                    sourceNodeId,
                    domain,
                    Camera.current != null ? Camera.current : sceneView.camera);
            }
            else if (domain == SceneEditDomain.Face)
            {
                var prevZTest = Handles.zTest;
                Handles.zTest = UnityEngine.Rendering.CompareFunction.Always;

                // Prefer packed polygons from the source node — MeshFilter is usually the
                // final merge and must not be indexed with intermediate-node face/tri ids.
                if (group.facePolygons != null && group.facePolygons.Length >= 4)
                {
                    DrawFacePolygonsHighlight(l2w, group.facePolygons);
                }
                else
                {
                    var mf = anchor.GetComponent<MeshFilter>();
                    if (mf == null || mf.sharedMesh == null)
                    {
                        Handles.zTest = prevZTest;
                        return;
                    }
                    var mesh = mf.sharedMesh;
                    var vertices = mesh.vertices;
                    var triangles = mesh.triangles;

                    Handles.color = s_GroupFaceFill;
                    foreach (var faceIdx in group.members)
                    {
                        int baseIdx = (int)faceIdx * 3;
                        if (triangles == null || baseIdx + 2 >= triangles.Length)
                            continue;
                        int v0 = triangles[baseIdx];
                        int v1 = triangles[baseIdx + 1];
                        int v2 = triangles[baseIdx + 2];
                        if (v0 >= vertices.Length || v1 >= vertices.Length || v2 >= vertices.Length)
                            continue;
                        var p0 = l2w.MultiplyPoint(vertices[v0]);
                        var p1 = l2w.MultiplyPoint(vertices[v1]);
                        var p2 = l2w.MultiplyPoint(vertices[v2]);
                        Handles.DrawAAConvexPolygon(p0, p1, p2);
                    }
                    Handles.color = s_GroupFaceOutline;
                    foreach (var faceIdx in group.members)
                    {
                        int baseIdx = (int)faceIdx * 3;
                        if (triangles == null || baseIdx + 2 >= triangles.Length)
                            continue;
                        int v0 = triangles[baseIdx];
                        int v1 = triangles[baseIdx + 1];
                        int v2 = triangles[baseIdx + 2];
                        if (v0 >= vertices.Length || v1 >= vertices.Length || v2 >= vertices.Length)
                            continue;
                        var p0 = l2w.MultiplyPoint(vertices[v0]);
                        var p1 = l2w.MultiplyPoint(vertices[v1]);
                        var p2 = l2w.MultiplyPoint(vertices[v2]);
                        Handles.DrawAAPolyLine(2f, p0, p1, p2, p0);
                    }
                }

                Handles.zTest = prevZTest;
            }
            else if (domain == SceneEditDomain.Vertex)
            {
                PcgScenePreviewRenderer.DrawGroup(
                    component,
                    anchor,
                    group,
                    sourceNodeId,
                    domain,
                    Camera.current != null ? Camera.current : sceneView.camera);
            }
        }
    }
}
#endif
