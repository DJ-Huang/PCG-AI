#if UNITY_EDITOR
using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Scene View stamp volume overlay + TransformMesh gizmo for Terrain Host workflows.
    /// Overlay matrix updates immediately; HeightField recook follows StampLiveCookMode.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgStampOverlaySceneHandles
    {
        private static readonly Color s_IdleWire = new(0.2f, 0.85f, 1f, 0.55f);
        private static readonly Color s_ActiveWire = new(1f, 0.55f, 0.15f, 0.95f);

        private static bool s_Dragging;
        private static bool s_DragHadHotControl;
        private static string s_DragTransformNodeId;
        private static PcgGraphComponent s_DragComponent;
        private static PcgGraphEditorWindow s_DragWindow;

        static PcgStampOverlaySceneHandles()
        {
            SceneView.duringSceneGui -= OnSceneGui;
            SceneView.duringSceneGui += OnSceneGui;
            EditorApplication.update -= OnEditorUpdate;
            EditorApplication.update += OnEditorUpdate;
        }

        private static void OnEditorUpdate()
        {
            // PositionHandle often ends without a MouseUp that our SceneGUI still sees.
            // Detect drag end via hotControl clearing, then cook + force editor tick.
            if (!s_Dragging || s_DragComponent == null)
                return;

            if (GUIUtility.hotControl != 0)
            {
                s_DragHadHotControl = true;
                return;
            }

            if (!s_DragHadHotControl)
                return;

            FinishStampDragCook();
        }

        private static void OnSceneGui(SceneView sceneView)
        {
            if (Application.isPlaying || sceneView == null)
                return;

            var component = Selection.activeGameObject != null
                ? Selection.activeGameObject.GetComponent<PcgGraphComponent>()
                : null;

            PcgGraphEditorWindow window = null;
            if (component != null && component.GraphAsset != null)
                window = FindWindowForComponent(component);

            // Also allow editing when a graph window is focused and its component is in the scene.
            if (component == null)
            {
                window = EditorWindow.focusedWindow as PcgGraphEditorWindow;
                if (window == null || !window.HasLoadedGraph)
                    return;
                component = FindComponentForWindow(window);
            }

            if (component == null || !component.ShowStampOverlays)
                return;

            if (!TryResolveDocument(component, window, out var doc) || doc == null)
                return;

            var specs = PcgStampOverlayCollector.Collect(doc);
            if (specs.Count == 0)
                return;

            var selectedNodeIds = CollectSelectedNodeIds(window);
            var activeSpec = PickActiveSpec(specs, selectedNodeIds);
            var space = ResolveStampSpace(component, doc);

            for (var i = 0; i < specs.Count; i++)
            {
                var spec = specs[i];
                var isActive = activeSpec != null &&
                    spec.MaskNodeId == activeSpec.MaskNodeId;
                DrawStamp(space, spec, isActive);
            }

            if (activeSpec != null && activeSpec.HasTransform)
                EditStampTransform(sceneView, component, window, space, activeSpec);

            HandleDragCookRelease(component, window);
        }

        private static void FinishStampDragCook()
        {
            var component = s_DragComponent;
            var window = s_DragWindow;
            s_Dragging = false;
            s_DragHadHotControl = false;
            s_DragTransformNodeId = null;
            s_DragComponent = null;
            s_DragWindow = null;

            if (component == null)
                return;

            if (component.StampLiveCookMode == PcgStampLiveCookMode.OnRelease ||
                component.StampLiveCookMode == PcgStampLiveCookMode.WhileDragging)
            {
                RequestCook(component, window, immediate: true);
            }

            EditorApplication.QueuePlayerLoopUpdate();
            SceneView.RepaintAll();
        }

        private struct StampSpace
        {
            public Transform GraphRoot;
            public Terrain Terrain;
            public float CenterX;
            public float CenterY;
            public float CenterZ;
            public float SizeX;
            public float SizeZ;
            public bool UseTerrainUv;
        }

        /// <summary>
        /// Host Terrain export maps HF parametric UV → TerrainData UV (only Y uses graphToWorld).
        /// Stamp overlays must use the same XZ mapping so gizmos sit on the raised footprint.
        /// </summary>
        private static StampSpace ResolveStampSpace(PcgGraphComponent component, PcgGraphDocument doc)
        {
            var space = new StampSpace
            {
                GraphRoot = component != null ? component.transform : null,
                CenterX = 0f,
                CenterY = 0f,
                CenterZ = 0f,
                SizeX = 256f,
                SizeZ = 256f,
            };

            if (doc?.nodes != null)
            {
                foreach (var node in doc.nodes)
                {
                    if (node == null || node.type != "HeightField")
                        continue;
                    space.CenterX = PcgStampOverlayCollector.ReadFloat(node.data, "centerX", 0f);
                    space.CenterY = PcgStampOverlayCollector.ReadFloat(node.data, "centerY", 0f);
                    space.CenterZ = PcgStampOverlayCollector.ReadFloat(node.data, "centerZ", 0f);
                    space.SizeX = Mathf.Max(
                        0.0001f, PcgStampOverlayCollector.ReadFloat(node.data, "sizeX", 256f));
                    space.SizeZ = Mathf.Max(
                        0.0001f, PcgStampOverlayCollector.ReadFloat(node.data, "sizeZ", 256f));
                    break;
                }
            }

            space.Terrain = component != null ? component.GetComponent<Terrain>() : null;
            space.UseTerrainUv = space.Terrain != null &&
                space.Terrain.terrainData != null &&
                component.HostOutputMode == PcgHostOutputMode.Terrain;
            return space;
        }

        private static Vector3 GraphToWorld(StampSpace space, Vector3 graphPoint)
        {
            if (!space.UseTerrainUv)
            {
                return space.GraphRoot != null
                    ? space.GraphRoot.TransformPoint(graphPoint)
                    : graphPoint;
            }

            var data = space.Terrain.terrainData;
            var u = (graphPoint.x - (space.CenterX - space.SizeX * 0.5f)) / space.SizeX;
            var v = (graphPoint.z - (space.CenterZ - space.SizeZ * 0.5f)) / space.SizeZ;
            var local = new Vector3(
                u * data.size.x,
                graphPoint.y - space.CenterY,
                v * data.size.z);
            return space.Terrain.transform.TransformPoint(local);
        }

        private static Vector3 WorldToGraph(StampSpace space, Vector3 worldPoint)
        {
            if (!space.UseTerrainUv)
            {
                return space.GraphRoot != null
                    ? space.GraphRoot.InverseTransformPoint(worldPoint)
                    : worldPoint;
            }

            var data = space.Terrain.terrainData;
            var local = space.Terrain.transform.InverseTransformPoint(worldPoint);
            var u = data.size.x > 0.0001f ? local.x / data.size.x : 0.5f;
            var v = data.size.z > 0.0001f ? local.z / data.size.z : 0.5f;
            return new Vector3(
                (space.CenterX - space.SizeX * 0.5f) + u * space.SizeX,
                local.y + space.CenterY,
                (space.CenterZ - space.SizeZ * 0.5f) + v * space.SizeZ);
        }

        private static Quaternion GraphToWorldRotation(StampSpace space, Vector3 localEuler)
        {
            var local = Quaternion.Euler(localEuler);
            var root = space.Terrain != null ? space.Terrain.transform : space.GraphRoot;
            return root != null ? root.rotation * local : local;
        }

        private static Vector3 WorldToGraphEuler(StampSpace space, Quaternion worldRot)
        {
            var root = space.Terrain != null ? space.Terrain.transform : space.GraphRoot;
            var local = root != null
                ? Quaternion.Inverse(root.rotation) * worldRot
                : worldRot;
            return local.eulerAngles;
        }

        private static void DrawStamp(StampSpace space, PcgStampOverlaySpec spec, bool active)
        {
            var worldPos = GraphToWorld(space, spec.LocalPosition);
            var worldRot = GraphToWorldRotation(space, spec.LocalEuler);
            var scale = spec.LocalScale;
            if (scale.x < 0.0001f) scale.x = 0.0001f;
            if (scale.y < 0.0001f) scale.y = 0.0001f;
            if (scale.z < 0.0001f) scale.z = 0.0001f;

            var prev = Handles.matrix;
            Handles.matrix = Matrix4x4.TRS(worldPos, worldRot, scale);

            Handles.color = active ? s_ActiveWire : s_IdleWire;
            Handles.DrawWireCube(Vector3.zero, spec.BoxSize);

            Handles.matrix = prev;

            var labelPos = GraphToWorld(
                space,
                spec.LocalPosition + Vector3.up * (spec.BoxSize.y * 0.5f * scale.y + 1f));
            Handles.Label(labelPos, string.IsNullOrEmpty(spec.Title) ? "Stamp" : spec.Title);
        }

        private static void EditStampTransform(
            SceneView sceneView,
            PcgGraphComponent component,
            PcgGraphEditorWindow window,
            StampSpace space,
            PcgStampOverlaySpec spec)
        {
            var worldPos = GraphToWorld(space, spec.LocalPosition);
            var worldRot = GraphToWorldRotation(space, spec.LocalEuler);

            EditorGUI.BeginChangeCheck();
            var newWorldPos = Handles.PositionHandle(worldPos, worldRot);
            var newWorldRot = Handles.RotationHandle(worldRot, newWorldPos);
            if (!EditorGUI.EndChangeCheck())
                return;

            var graphPos = WorldToGraph(space, newWorldPos);
            var graphEuler = WorldToGraphEuler(space, newWorldRot);

            BeginDrag(component, window, spec.TransformNodeId);
            WriteTransform(component, window, spec.TransformNodeId, graphPos, graphEuler, spec.LocalScale);

            if (component.StampLiveCookMode == PcgStampLiveCookMode.WhileDragging)
                RequestCook(component, window, immediate: false);

            sceneView.Repaint();
        }

        private static void HandleDragCookRelease(
            PcgGraphComponent component,
            PcgGraphEditorWindow window)
        {
            if (!s_Dragging)
                return;

            if (GUIUtility.hotControl != 0)
                s_DragHadHotControl = true;

            var evt = Event.current;
            var mouseUp = evt != null &&
                (evt.type == EventType.MouseUp || evt.rawType == EventType.MouseUp);
            var hotReleased = s_DragHadHotControl && GUIUtility.hotControl == 0;
            if (!mouseUp && !hotReleased)
                return;

            FinishStampDragCook();
            if (mouseUp)
                evt.Use();
        }

        private static void BeginDrag(
            PcgGraphComponent component,
            PcgGraphEditorWindow window,
            string transformNodeId)
        {
            s_Dragging = true;
            s_DragHadHotControl = GUIUtility.hotControl != 0;
            s_DragComponent = component;
            s_DragWindow = window;
            s_DragTransformNodeId = transformNodeId;
        }

        private static void WriteTransform(
            PcgGraphComponent component,
            PcgGraphEditorWindow window,
            string transformNodeId,
            Vector3 localPos,
            Vector3 localEuler,
            Vector3 localScale)
        {
            if (window != null && window.GraphView != null)
            {
                foreach (var node in window.GraphView.nodes.OfType<PcgManifestNodeView>())
                {
                    if (node.NodeId != transformNodeId)
                        continue;

                    Undo.RecordObject(window, "Edit Stamp Transform");
                    node.SetPropertyValue("translateX", localPos.x);
                    node.SetPropertyValue("translateY", localPos.y);
                    node.SetPropertyValue("translateZ", localPos.z);
                    node.SetPropertyValue("rotationX", localEuler.x);
                    node.SetPropertyValue("rotationY", localEuler.y);
                    node.SetPropertyValue("rotationZ", localEuler.z);
                    node.SetPropertyValue("scaleX", localScale.x);
                    node.SetPropertyValue("scaleY", localScale.y);
                    node.SetPropertyValue("scaleZ", localScale.z);
                    window.GraphView.CommitState();
                    window.GraphView.RefreshInspector();
                    return;
                }
            }

            // Fallback: write component document + asset when Graph Editor is closed.
            component.RefreshDocument();
            var doc = component.Document;
            if (doc?.nodes == null)
                return;

            PcgGraphNodeRecord target = null;
            foreach (var node in doc.nodes)
            {
                if (node.id == transformNodeId)
                {
                    target = node;
                    break;
                }
            }

            if (target == null)
                return;

            Undo.RecordObject(component, "Edit Stamp Transform");
            if (component.GraphAsset != null)
                Undo.RecordObject(component.GraphAsset, "Edit Stamp Transform");

            target.data.SetRaw("translateX", localPos.x);
            target.data.SetRaw("translateY", localPos.y);
            target.data.SetRaw("translateZ", localPos.z);
            target.data.SetRaw("rotationX", localEuler.x);
            target.data.SetRaw("rotationY", localEuler.y);
            target.data.SetRaw("rotationZ", localEuler.z);
            target.data.SetRaw("scaleX", localScale.x);
            target.data.SetRaw("scaleY", localScale.y);
            target.data.SetRaw("scaleZ", localScale.z);

            if (component.GraphAsset != null)
            {
                component.GraphAsset.SetGraphJson(PcgGraphSerializer.ToJson(doc, pretty: true));
                EditorUtility.SetDirty(component.GraphAsset);
            }

            EditorUtility.SetDirty(component);
        }

        private static void RequestCook(
            PcgGraphComponent component,
            PcgGraphEditorWindow window,
            bool immediate)
        {
            // Stamp gizmo must cook the full HF→Output graph, not a Mesh node preview sink.
            if (window != null && !string.IsNullOrEmpty(window.PreviewNodeId))
            {
                window.ClearNodePreview(silent: true);
                EditorApplication.QueuePlayerLoopUpdate();
                SceneView.RepaintAll();
                return;
            }

            if (window != null)
            {
                PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate);
                EditorApplication.QueuePlayerLoopUpdate();
                SceneView.RepaintAll();
                return;
            }

            if (component != null && component.SupportsEditModePreview())
            {
                component.RequestPreviewCook(immediate);
                EditorApplication.QueuePlayerLoopUpdate();
                SceneView.RepaintAll();
            }
        }

        private static bool TryResolveDocument(
            PcgGraphComponent component,
            PcgGraphEditorWindow window,
            out PcgGraphDocument doc)
        {
            doc = null;
            if (window != null && window.HasLoadedGraph)
            {
                doc = window.ExportLiveDocument();
                if (doc != null)
                    return true;
            }

            component.RefreshDocument();
            doc = component.Document;
            return doc != null;
        }

        private static HashSet<string> CollectSelectedNodeIds(PcgGraphEditorWindow window)
        {
            var ids = new HashSet<string>();
            if (window?.GraphView == null)
                return ids;

            foreach (var node in window.GraphView.selection.OfType<PcgGraphNodeBase>())
            {
                if (!string.IsNullOrEmpty(node.NodeId))
                    ids.Add(node.NodeId);
            }

            return ids;
        }

        private static PcgStampOverlaySpec PickActiveSpec(
            List<PcgStampOverlaySpec> specs,
            HashSet<string> selectedNodeIds)
        {
            if (specs == null || specs.Count == 0)
                return null;

            foreach (var spec in specs)
            {
                if (selectedNodeIds.Contains(spec.MaskNodeId) ||
                    (!string.IsNullOrEmpty(spec.TransformNodeId) &&
                     selectedNodeIds.Contains(spec.TransformNodeId)) ||
                    (!string.IsNullOrEmpty(spec.BoxNodeId) &&
                     selectedNodeIds.Contains(spec.BoxNodeId)))
                    return spec;
            }

            // No graph selection: if only one stamp, keep it editable.
            return specs.Count == 1 ? specs[0] : null;
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
