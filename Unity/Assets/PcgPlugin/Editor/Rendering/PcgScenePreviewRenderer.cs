using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;
using Unity.Profiling;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Rendering
{
    /// <summary>
    /// Shared Editor renderer for static point/line preview geometry.
    /// Geometry is cached by owner and revision; camera state is supplied only at draw time.
    /// </summary>
    internal static class PcgScenePreviewRenderer
    {
        internal const int LegacyFallbackThreshold = 512;
        internal const float PreviewPointSizePixels = 8f;
        internal const float PreviewLineWidthPixels = 2f;
        internal const float GroupLineWidthPixels = 4f;

        private const string LineShaderName = "Hidden/PcgPolygonWireOverlay";
        private const string PointShaderName = "Hidden/PcgPolygonPointOverlay";

        private static readonly Dictionary<CacheKey, CacheEntry> s_Cache = new();
        private static readonly HashSet<int> s_WarnedFallbacks = new();
        private static readonly List<CacheKey> s_StaleKeys = new();
        private static readonly ProfilerMarker s_BuildMarker = new("PCG.ScenePreview.Build");
        private static readonly ProfilerMarker s_UploadMarker = new("PCG.ScenePreview.Upload");
        private static readonly ProfilerMarker s_DrawMarker = new("PCG.ScenePreview.Draw");

        private static readonly int s_ColorId = Shader.PropertyToID("_Color");
        private static readonly int s_LineWidthId = Shader.PropertyToID("_LineWidth");
        private static readonly int s_PointSizeId = Shader.PropertyToID("_PointSize");
        private static readonly int s_UseWorldSizeId = Shader.PropertyToID("_UseWorldSize");
        private static readonly int s_ViewportId = Shader.PropertyToID("_Viewport");
        private static readonly int s_ZTestId = Shader.PropertyToID("_ZTest");
        private static readonly int s_ZWriteId = Shader.PropertyToID("_ZWrite");

        private static Material s_LineMaterial;
        private static Material s_PointMaterial;
        private static bool s_LineShaderWarningIssued;
        private static bool s_PointShaderWarningIssued;
        private static double s_LastPruneTime;
        private static int s_RebuildCount;
        private static int s_UploadCount;
        private static int s_DrawCount;
        private static int s_FallbackCount;

        internal readonly struct RendererCounters
        {
            public readonly int CacheEntryCount;
            public readonly int RebuildCount;
            public readonly int UploadCount;
            public readonly int DrawCount;
            public readonly int FallbackCount;

            public RendererCounters(
                int cacheEntryCount,
                int rebuildCount,
                int uploadCount,
                int drawCount,
                int fallbackCount)
            {
                CacheEntryCount = cacheEntryCount;
                RebuildCount = rebuildCount;
                UploadCount = uploadCount;
                DrawCount = drawCount;
                FallbackCount = fallbackCount;
            }
        }

        private readonly struct DrawStyle
        {
            public readonly Color Color;
            public readonly float LineWidthPixels;
            public readonly float PointSizePixels;
            public readonly bool AlwaysOnTop;
            public readonly bool WorldPointSize;

            public DrawStyle(
                Color color,
                float lineWidthPixels,
                float pointSizePixels,
                bool alwaysOnTop,
                bool worldPointSize)
            {
                Color = color;
                LineWidthPixels = lineWidthPixels;
                PointSizePixels = pointSizePixels;
                AlwaysOnTop = alwaysOnTop;
                WorldPointSize = worldPointSize;
            }
        }

        private readonly struct CacheKey : IEquatable<CacheKey>
        {
            private readonly int m_OwnerId;
            private readonly int m_Kind;
            private readonly int m_GeometryKey;

            public CacheKey(int ownerId, int kind, int geometryKey)
            {
                m_OwnerId = ownerId;
                m_Kind = kind;
                m_GeometryKey = geometryKey;
            }

            public int OwnerId => m_OwnerId;
            public int Kind => m_Kind;

            public bool Equals(CacheKey other)
            {
                return m_OwnerId == other.m_OwnerId &&
                       m_Kind == other.m_Kind &&
                       m_GeometryKey == other.m_GeometryKey;
            }

            public override bool Equals(object obj) => obj is CacheKey other && Equals(other);

            public override int GetHashCode()
            {
                unchecked
                {
                    var hash = m_OwnerId;
                    hash = hash * 397 ^ m_Kind;
                    return hash * 397 ^ m_GeometryKey;
                }
            }
        }

        private sealed class CacheEntry
        {
            public UnityEngine.Object Owner;
            public object Source;
            public PcgScenePreviewMeshBuilder.MeshData EdgeData;
            public PcgScenePreviewMeshBuilder.MeshData PointData;
            public Mesh EdgeMesh;
            public Mesh PointMesh;
            public double LastUsed;
        }

        internal static RendererCounters GetCounters()
        {
            return new RendererCounters(
                s_Cache.Count,
                s_RebuildCount,
                s_UploadCount,
                s_DrawCount,
                s_FallbackCount);
        }

        internal static void ResetForTests()
        {
            ReleaseAll();
            s_WarnedFallbacks.Clear();
            s_LineShaderWarningIssued = false;
            s_PointShaderWarningIssued = false;
            s_RebuildCount = 0;
            s_UploadCount = 0;
            s_DrawCount = 0;
            s_FallbackCount = 0;
        }

        internal static void ReleaseOwner(UnityEngine.Object owner)
        {
            if (owner == null)
                return;

            var ownerId = owner.GetInstanceID();
            var staleKeys = new List<CacheKey>();
            foreach (var pair in s_Cache)
            {
                if (pair.Value.Owner == owner || pair.Key.OwnerId == ownerId)
                    staleKeys.Add(pair.Key);
            }

            foreach (var key in staleKeys)
            {
                if (s_Cache.TryGetValue(key, out var entry))
                {
                    DestroyEntry(entry);
                    s_Cache.Remove(key);
                }
            }
        }

        internal static void ReleaseAll()
        {
            foreach (var pair in s_Cache)
                DestroyEntry(pair.Value);
            s_Cache.Clear();

            DestroyMaterial(ref s_LineMaterial);
            DestroyMaterial(ref s_PointMaterial);
        }

        internal static bool DrawPolygon(
            PcgGraphComponent owner,
            Transform anchor,
            PcgPolygonPreviewData preview,
            Camera camera,
            bool drawEdges,
            bool drawPoints)
        {
            if (owner == null || anchor == null || preview == null || camera == null)
                return false;

            var entry = GetPolygonEntry(owner, preview);
            var handled = true;
            if (drawEdges)
            {
                handled &= DrawEdges(
                    entry,
                    anchor.localToWorldMatrix,
                    camera,
                    new DrawStyle(Color.black, PreviewLineWidthPixels, 0f, false, false));
            }

            if (drawPoints)
            {
                handled &= DrawPoints(
                    entry,
                    anchor.localToWorldMatrix,
                    camera,
                    new DrawStyle(
                        new Color(0.25f, 0.55f, 1f, 1f),
                        0f,
                        PreviewPointSizePixels,
                        false,
                        false));
            }

            return handled;
        }

        internal static bool DrawPreview(PcgPreview preview, Camera camera)
        {
            if (preview == null || camera == null)
                return false;

            var entry = GetPreviewEntry(preview);
            if (entry.EdgeData == null && entry.PointData == null)
                return true;

            var matrix = preview.transform.localToWorldMatrix;
            if (entry.EdgeData != null)
            {
                DrawEdges(
                    entry,
                    matrix,
                    camera,
                    new DrawStyle(
                        preview.SplineColor,
                        PreviewLineWidthPixels,
                        0f,
                        false,
                        false));
            }

            if (entry.PointData != null)
            {
                DrawPoints(
                    entry,
                    matrix,
                    camera,
                    new DrawStyle(Color.white, 0f, 0f, false, true));
            }

            return true;
        }

        internal static bool DrawGroup(
            PcgGraphComponent owner,
            Transform anchor,
            NodeGroupEntry group,
            string sourceNodeId,
            SceneEditDomain domain,
            Camera camera)
        {
            if (owner == null || anchor == null || group == null || camera == null)
                return false;

            var entry = GetGroupEntry(owner, anchor, group, domain, sourceNodeId);
            if (domain == SceneEditDomain.Edge)
            {
                return DrawEdges(
                    entry,
                    anchor.localToWorldMatrix,
                    camera,
                    new DrawStyle(
                        new Color(0f, 1f, 0.8f, 0.9f),
                        GroupLineWidthPixels,
                        0f,
                        true,
                        false));
            }

            if (domain == SceneEditDomain.Vertex)
            {
                return DrawPoints(
                    entry,
                    anchor.localToWorldMatrix,
                    camera,
                    new DrawStyle(
                        new Color(1f, 0.3f, 0.3f, 0.9f),
                        0f,
                        PreviewPointSizePixels,
                        true,
                        false));
            }

            return true;
        }

        internal static void PreparePreviewForTests(PcgPreview preview)
        {
            if (preview != null)
                GetPreviewEntry(preview);
        }

        internal static CompareFunction ResolveDepthCompare(
            bool alwaysOnTop,
            bool usesReversedZBuffer)
        {
            if (alwaysOnTop)
                return CompareFunction.Always;

            return usesReversedZBuffer
                ? CompareFunction.GreaterEqual
                : CompareFunction.LessEqual;
        }

        private static CompareFunction ResolveDepthCompare(bool alwaysOnTop)
        {
            return ResolveDepthCompare(alwaysOnTop, SystemInfo.usesReversedZBuffer);
        }

        private static CacheEntry GetPolygonEntry(
            PcgGraphComponent owner,
            PcgPolygonPreviewData preview)
        {
            var key = new CacheKey(
                owner.GetInstanceID(),
                1,
                RuntimeHelpers.GetHashCode(preview));
            if (TryGetEntry(key, preview, out var cached))
                return cached;

            PcgScenePreviewMeshBuilder.MeshData edgeData;
            PcgScenePreviewMeshBuilder.MeshData pointData;
            using (s_BuildMarker.Auto())
            {
                edgeData = PcgScenePreviewMeshBuilder.BuildEdges(preview);
                pointData = PcgScenePreviewMeshBuilder.BuildPoints(preview.Points);
            }

            return StoreEntry(
                key,
                owner,
                preview,
                edgeData,
                pointData,
                "PcgPolygonPreviewEdges",
                "PcgPolygonPreviewPoints");
        }

        private static CacheEntry GetPreviewEntry(PcgPreview preview)
        {
            var key = new CacheKey(preview.GetInstanceID(), 2, preview.Revision);
            if (TryGetEntry(key, preview, out var cached))
                return cached;

            var positions = new List<Vector3>();
            var colors = new List<Color32>();
            var sizes = new List<float>();
            var edgeEndpoints = new List<Vector3>();
            var pointColor = (Color32)preview.GizmoColor;
            var splineColor = (Color32)preview.SplineColor;
            var pointDiameter = Mathf.Max(0.001f, preview.GizmoSize * 2f);
            var splineDiameter = Mathf.Max(0.001f, Mathf.Max(0.02f, preview.GizmoSize * 0.35f) * 2f);

            foreach (var point in preview.Points)
            {
                positions.Add(point);
                colors.Add(pointColor);
                sizes.Add(pointDiameter);
            }

            foreach (var spline in preview.Splines)
            {
                if (spline == null || spline.Count == 0)
                    continue;

                for (var i = 0; i < spline.Count; i++)
                {
                    positions.Add(spline[i]);
                    colors.Add(splineColor);
                    sizes.Add(splineDiameter);
                    if (i > 0)
                    {
                        edgeEndpoints.Add(spline[i - 1]);
                        edgeEndpoints.Add(spline[i]);
                    }
                }
            }

            PcgScenePreviewMeshBuilder.MeshData edgeData;
            PcgScenePreviewMeshBuilder.MeshData pointData;
            using (s_BuildMarker.Auto())
            {
                edgeData = PcgScenePreviewMeshBuilder.BuildEdgeEndpoints(edgeEndpoints);
                pointData = PcgScenePreviewMeshBuilder.BuildPoints(
                    positions,
                    colors,
                    sizes,
                    new Color32(255, 255, 255, 255),
                    1f);
            }

            return StoreEntry(
                key,
                preview,
                preview,
                edgeData,
                pointData,
                "PcgPreviewSplineEdges",
                "PcgPreviewPoints");
        }

        private static CacheEntry GetGroupEntry(
            PcgGraphComponent owner,
            Transform anchor,
            NodeGroupEntry group,
            SceneEditDomain domain,
            string sourceNodeId)
        {
            var source = GetGroupSource(group, domain);
            var geometryKey = RuntimeHelpers.GetHashCode(source ?? group);
            geometryKey = Combine(geometryKey, sourceNodeId?.GetHashCode() ?? 0);
            geometryKey = Combine(geometryKey, (int)domain);
            var key = new CacheKey(owner.GetInstanceID(), 3, geometryKey);
            if (TryGetEntry(key, group, out var cached))
                return cached;

            PcgScenePreviewMeshBuilder.MeshData edgeData = null;
            PcgScenePreviewMeshBuilder.MeshData pointData = null;
            using (s_BuildMarker.Auto())
            {
                if (domain == SceneEditDomain.Edge)
                    edgeData = PcgScenePreviewMeshBuilder.BuildEdgeEndpoints(
                        ResolveGroupEdgeEndpoints(anchor, group));
                else if (domain == SceneEditDomain.Vertex)
                    pointData = PcgScenePreviewMeshBuilder.BuildPoints(
                        ResolveGroupPointPositions(owner, anchor, group));
            }

            return StoreEntry(
                key,
                owner,
                group,
                edgeData,
                pointData,
                "PcgGroupEdges",
                "PcgGroupPoints");
        }

        private static CacheEntry StoreEntry(
            CacheKey key,
            UnityEngine.Object owner,
            object source,
            PcgScenePreviewMeshBuilder.MeshData edgeData,
            PcgScenePreviewMeshBuilder.MeshData pointData,
            string edgeName,
            string pointName)
        {
            var supersededKeys = new List<CacheKey>();
            foreach (var pair in s_Cache)
            {
                if (pair.Key.OwnerId != key.OwnerId ||
                    pair.Key.Kind != key.Kind ||
                    pair.Key.Equals(key))
                    continue;

                // Polygon and Runtime Preview owners have a single live geometry
                // generation. Group entries may have several domains, so only
                // replace an entry when it belongs to the same source group object.
                if (key.Kind != 3 || ReferenceEquals(pair.Value.Source, source))
                    supersededKeys.Add(pair.Key);
            }

            foreach (var supersededKey in supersededKeys)
            {
                if (s_Cache.TryGetValue(supersededKey, out var supersededEntry))
                {
                    DestroyEntry(supersededEntry);
                    s_Cache.Remove(supersededKey);
                }
            }

            if (s_Cache.TryGetValue(key, out var oldEntry))
            {
                DestroyEntry(oldEntry);
                s_Cache.Remove(key);
            }

            var entry = new CacheEntry
            {
                Owner = owner,
                Source = source,
                EdgeData = edgeData?.IsEmpty == false ? edgeData : null,
                PointData = pointData?.IsEmpty == false ? pointData : null,
                LastUsed = EditorApplication.timeSinceStartup,
            };

            using (s_UploadMarker.Auto())
            {
                if (entry.EdgeData != null)
                    entry.EdgeMesh = entry.EdgeData.CreateMesh(edgeName);
                if (entry.PointData != null)
                    entry.PointMesh = entry.PointData.CreateMesh(pointName);
            }

            s_RebuildCount++;
            if (entry.EdgeMesh != null)
                s_UploadCount++;
            if (entry.PointMesh != null)
                s_UploadCount++;
            s_Cache[key] = entry;
            PruneStaleEntries();
            return entry;
        }

        private static bool TryGetEntry(CacheKey key, object source, out CacheEntry entry)
        {
            if (s_Cache.TryGetValue(key, out entry) && ReferenceEquals(entry.Source, source))
            {
                entry.LastUsed = EditorApplication.timeSinceStartup;
                PruneStaleEntries();
                return true;
            }

            if (entry != null)
            {
                DestroyEntry(entry);
                s_Cache.Remove(key);
            }

            entry = null;
            return false;
        }

        private static bool DrawEdges(
            CacheEntry entry,
            Matrix4x4 localToWorld,
            Camera camera,
            DrawStyle style)
        {
            if (entry?.EdgeData == null || entry.EdgeMesh == null)
                return true;

            using (s_DrawMarker.Auto())
            {
                if (!EnsureLineMaterial())
                {
                    return DrawEdgeFallback(entry, localToWorld, style);
                }

                var ppp = Mathf.Max(0.01f, EditorGUIUtility.pixelsPerPoint);
                s_LineMaterial.SetColor(s_ColorId, style.Color);
                s_LineMaterial.SetFloat(s_LineWidthId, Mathf.Max(0.01f, style.LineWidthPixels * ppp));
                s_LineMaterial.SetVector(
                    s_ViewportId,
                    new Vector4(Mathf.Max(1, camera.pixelWidth), Mathf.Max(1, camera.pixelHeight), 0f, 0f));
                s_LineMaterial.SetInt(
                    s_ZTestId,
                    (int)ResolveDepthCompare(style.AlwaysOnTop));
                s_LineMaterial.SetInt(s_ZWriteId, style.AlwaysOnTop ? 0 : 1);
                if (!s_LineMaterial.SetPass(0))
                {
                    return DrawEdgeFallback(entry, localToWorld, style);
                }

                Graphics.DrawMeshNow(entry.EdgeMesh, localToWorld);
                s_DrawCount++;
                return true;
            }
        }

        private static bool DrawPoints(
            CacheEntry entry,
            Matrix4x4 localToWorld,
            Camera camera,
            DrawStyle style)
        {
            if (entry?.PointData == null || entry.PointMesh == null)
                return true;

            using (s_DrawMarker.Auto())
            {
                if (!EnsurePointMaterial())
                {
                    return DrawPointFallback(entry, localToWorld, style);
                }

                var ppp = Mathf.Max(0.01f, EditorGUIUtility.pixelsPerPoint);
                s_PointMaterial.SetColor(s_ColorId, style.Color);
                s_PointMaterial.SetFloat(
                    s_PointSizeId,
                    Mathf.Max(0.01f, style.PointSizePixels * ppp));
                s_PointMaterial.SetFloat(s_UseWorldSizeId, style.WorldPointSize ? 1f : 0f);
                s_PointMaterial.SetVector(
                    s_ViewportId,
                    new Vector4(Mathf.Max(1, camera.pixelWidth), Mathf.Max(1, camera.pixelHeight), 0f, 0f));
                s_PointMaterial.SetInt(
                    s_ZTestId,
                    (int)ResolveDepthCompare(style.AlwaysOnTop));
                if (!s_PointMaterial.SetPass(0))
                {
                    return DrawPointFallback(entry, localToWorld, style);
                }

                Graphics.DrawMeshNow(entry.PointMesh, localToWorld);
                s_DrawCount++;
                return true;
            }
        }

        private static bool DrawEdgeFallback(
            CacheEntry entry,
            Matrix4x4 localToWorld,
            DrawStyle style)
        {
            s_FallbackCount++;
            var endpoints = entry.EdgeData.SourceEdgeEndpoints;
            if (endpoints == null || endpoints.Length == 0)
                return true;
            if (entry.EdgeData.PrimitiveCount > LegacyFallbackThreshold)
            {
                WarnFallback(entry.Owner, "edge");
                return true;
            }

            var world = new Vector3[endpoints.Length];
            for (var i = 0; i < endpoints.Length; i++)
                world[i] = localToWorld.MultiplyPoint(endpoints[i]);

            var previousColor = Handles.color;
            var previousZTest = Handles.zTest;
            try
            {
                Handles.color = style.Color;
                Handles.zTest = ResolveDepthCompare(style.AlwaysOnTop);
                Handles.DrawLines(world);
            }
            finally
            {
                Handles.color = previousColor;
                Handles.zTest = previousZTest;
            }

            return true;
        }

        private static bool DrawPointFallback(
            CacheEntry entry,
            Matrix4x4 localToWorld,
            DrawStyle style)
        {
            s_FallbackCount++;
            var centers = entry.PointData.SourcePointCenters;
            if (centers == null || centers.Length == 0)
                return true;
            if (entry.PointData.PrimitiveCount > LegacyFallbackThreshold)
            {
                WarnFallback(entry.Owner, "point");
                return true;
            }

            var colors = entry.PointData.SourcePointColors;
            var sizes = entry.PointData.SourcePointSizes;
            var previousColor = Handles.color;
            var previousZTest = Handles.zTest;
            try
            {
                Handles.zTest = ResolveDepthCompare(style.AlwaysOnTop);
                for (var i = 0; i < centers.Length; i++)
                {
                    var world = localToWorld.MultiplyPoint(centers[i]);
                    var color = colors != null && i < colors.Length
                        ? (Color)colors[i]
                        : Color.white;
                    Handles.color = color * style.Color;
                    var size = style.WorldPointSize && sizes != null && i < sizes.Length
                        ? sizes[i]
                        : HandleUtility.GetHandleSize(world) * 0.07f;
                    Handles.SphereHandleCap(0, world, Quaternion.identity, size, EventType.Repaint);
                }
            }
            finally
            {
                Handles.color = previousColor;
                Handles.zTest = previousZTest;
            }

            return true;
        }

        private static bool EnsureLineMaterial()
        {
            if (s_LineMaterial != null)
                return true;

            var shader = Shader.Find(LineShaderName);
            if (shader == null)
            {
                if (!s_LineShaderWarningIssued)
                {
                    Debug.LogWarning($"[PCG] Scene preview line shader unavailable: {LineShaderName}");
                    s_LineShaderWarningIssued = true;
                }
                return false;
            }

            s_LineMaterial = new Material(shader)
            {
                name = "PcgScenePreviewLineMaterial",
                hideFlags = HideFlags.HideAndDontSave,
            };
            return true;
        }

        private static bool EnsurePointMaterial()
        {
            if (s_PointMaterial != null)
                return true;

            var shader = Shader.Find(PointShaderName);
            if (shader == null)
            {
                if (!s_PointShaderWarningIssued)
                {
                    Debug.LogWarning($"[PCG] Scene preview point shader unavailable: {PointShaderName}");
                    s_PointShaderWarningIssued = true;
                }
                return false;
            }

            s_PointMaterial = new Material(shader)
            {
                name = "PcgScenePreviewPointMaterial",
                hideFlags = HideFlags.HideAndDontSave,
            };
            return true;
        }

        private static Vector3[] ResolveGroupEdgeEndpoints(
            Transform anchor,
            NodeGroupEntry group)
        {
            if (group.edgeEndpoints != null && group.edgeEndpoints.Length >= 6)
            {
                var result = new Vector3[group.edgeEndpoints.Length / 3];
                for (var i = 0; i + 2 < group.edgeEndpoints.Length; i += 3)
                {
                    result[i / 3] = new Vector3(
                        group.edgeEndpoints[i],
                        group.edgeEndpoints[i + 1],
                        group.edgeEndpoints[i + 2]);
                }
                return result;
            }

            var vertices = ResolveMeshVertices(anchor);
            if (vertices == null || group.members == null)
                return Array.Empty<Vector3>();

            var endpoints = new List<Vector3>(group.members.Length * 2);
            foreach (var key in group.members)
            {
                var a = key / 1000000;
                var b = key % 1000000;
                if (a < 0 || b < 0 || a >= vertices.Length || b >= vertices.Length)
                    continue;
                endpoints.Add(vertices[a]);
                endpoints.Add(vertices[b]);
            }
            return endpoints.ToArray();
        }

        private static Vector3[] ResolveGroupPointPositions(
            PcgGraphComponent owner,
            Transform anchor,
            NodeGroupEntry group)
        {
            if (group.pointPositions != null && group.pointPositions.Length >= 3)
            {
                var result = new Vector3[group.pointPositions.Length / 3];
                for (var i = 0; i + 2 < group.pointPositions.Length; i += 3)
                {
                    result[i / 3] = new Vector3(
                        group.pointPositions[i],
                        group.pointPositions[i + 1],
                        group.pointPositions[i + 2]);
                }
                return result;
            }

            var previewPoints = owner.PolygonPreview?.Points;
            if (previewPoints != null && group.members != null)
            {
                var points = new List<Vector3>(group.members.Length);
                foreach (var index in group.members)
                {
                    if (index >= 0 && index < previewPoints.Length)
                        points.Add(previewPoints[index]);
                }
                if (points.Count > 0)
                    return points.ToArray();
            }

            var vertices = ResolveMeshVertices(anchor);
            if (vertices == null || group.members == null)
                return Array.Empty<Vector3>();

            var resultFromMesh = new List<Vector3>(group.members.Length);
            foreach (var index in group.members)
            {
                if (index >= 0 && index < vertices.Length)
                    resultFromMesh.Add(vertices[index]);
            }
            return resultFromMesh.ToArray();
        }

        private static Vector3[] ResolveMeshVertices(Transform anchor)
        {
            var meshFilter = anchor != null ? anchor.GetComponent<MeshFilter>() : null;
            return meshFilter != null && meshFilter.sharedMesh != null
                ? meshFilter.sharedMesh.vertices
                : null;
        }

        private static object GetGroupSource(NodeGroupEntry group, SceneEditDomain domain)
        {
            if (domain == SceneEditDomain.Edge)
                return group.edgeEndpoints ?? (object)group.members;
            if (domain == SceneEditDomain.Vertex)
                return group.pointPositions ?? (object)group.members;
            return group.facePolygons ?? (object)group.members;
        }

        private static int Combine(int left, int right)
        {
            unchecked
            {
                return left * 397 ^ right;
            }
        }

        private static void PruneStaleEntries()
        {
            var now = EditorApplication.timeSinceStartup;
            if (now - s_LastPruneTime < 1.0)
                return;
            s_LastPruneTime = now;

            s_StaleKeys.Clear();
            foreach (var pair in s_Cache)
            {
                if (pair.Value.Owner == null || now - pair.Value.LastUsed > 30.0)
                    s_StaleKeys.Add(pair.Key);
            }

            foreach (var key in s_StaleKeys)
            {
                if (s_Cache.TryGetValue(key, out var entry))
                {
                    DestroyEntry(entry);
                    s_Cache.Remove(key);
                }
            }
            s_StaleKeys.Clear();
        }

        private static void DestroyEntry(CacheEntry entry)
        {
            if (entry == null)
                return;
            DestroyMesh(ref entry.EdgeMesh);
            DestroyMesh(ref entry.PointMesh);
            entry.EdgeData = null;
            entry.PointData = null;
            entry.Owner = null;
            entry.Source = null;
        }

        private static void DestroyMesh(ref Mesh mesh)
        {
            if (mesh == null)
                return;
            UnityEngine.Object.DestroyImmediate(mesh);
            mesh = null;
        }

        private static void DestroyMaterial(ref Material material)
        {
            if (material == null)
                return;
            UnityEngine.Object.DestroyImmediate(material);
            material = null;
        }

        private static void WarnFallback(UnityEngine.Object owner, string primitive)
        {
            var key = Combine(owner != null ? owner.GetInstanceID() : 0, primitive.GetHashCode());
            if (!s_WarnedFallbacks.Add(key))
                return;

            Debug.LogWarning(
                $"[PCG] Scene preview {primitive} shader unavailable for a large payload; " +
                "preview drawing is disabled to keep SceneView responsive.",
                owner);
        }
    }
}
