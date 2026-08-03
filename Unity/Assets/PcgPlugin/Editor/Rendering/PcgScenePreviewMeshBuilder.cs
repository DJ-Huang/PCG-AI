using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using DJTechRuntime.PCG;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Rendering
{
    /// <summary>
    /// Builds the static vertex/index payload used by the Scene View point/line overlay.
    /// Camera-dependent expansion is intentionally not part of this class.
    /// </summary>
    internal static class PcgScenePreviewMeshBuilder
    {
        internal enum MeshKind
        {
            Edges,
            Points,
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct EdgeVertex
        {
            public Vector3 Start;
            public Vector3 End;
            public Vector2 Corner;

            public EdgeVertex(Vector3 start, Vector3 end, Vector2 corner)
            {
                Start = start;
                End = end;
                Corner = corner;
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct PointVertex
        {
            public Vector3 Center;
            public Color32 Color;
            public Vector2 Corner;
            public float Size;

            public PointVertex(Vector3 center, Vector2 corner, Color32 color, float size)
            {
                Center = center;
                Color = color;
                Corner = corner;
                Size = size;
            }
        }

        internal readonly struct BuildDiagnostics
        {
            public readonly int InputPrimitiveCount;
            public readonly int AcceptedPrimitiveCount;
            public readonly int FilteredPrimitiveCount;
            public readonly int InvalidIndexCount;
            public readonly int NonFiniteCount;
            public readonly int DegenerateCount;

            public BuildDiagnostics(
                int inputPrimitiveCount,
                int acceptedPrimitiveCount,
                int filteredPrimitiveCount,
                int invalidIndexCount,
                int nonFiniteCount,
                int degenerateCount)
            {
                InputPrimitiveCount = inputPrimitiveCount;
                AcceptedPrimitiveCount = acceptedPrimitiveCount;
                FilteredPrimitiveCount = filteredPrimitiveCount;
                InvalidIndexCount = invalidIndexCount;
                NonFiniteCount = nonFiniteCount;
                DegenerateCount = degenerateCount;
            }
        }

        internal sealed class MeshData
        {
            public MeshKind Kind { get; }
            public EdgeVertex[] EdgeVertices { get; }
            public PointVertex[] PointVertices { get; }
            public int[] Indices { get; }
            public int[] EdgePairs { get; }
            public Vector3[] SourceEdgeEndpoints { get; }
            public Vector3[] SourcePointCenters { get; }
            public Color32[] SourcePointColors { get; }
            public float[] SourcePointSizes { get; }
            public Bounds Bounds { get; }
            public BuildDiagnostics Diagnostics { get; }

            public int PrimitiveCount { get; }
            public int VertexCount => Kind == MeshKind.Edges
                ? EdgeVertices?.Length ?? 0
                : PointVertices?.Length ?? 0;
            public int IndexCount => Indices?.Length ?? 0;
            public bool IsEmpty => PrimitiveCount == 0;

            private MeshData(
                MeshKind kind,
                EdgeVertex[] edgeVertices,
                PointVertex[] pointVertices,
                int[] indices,
                int[] edgePairs,
                Vector3[] sourceEdgeEndpoints,
                Vector3[] sourcePointCenters,
                Color32[] sourcePointColors,
                float[] sourcePointSizes,
                Bounds bounds,
                BuildDiagnostics diagnostics,
                int primitiveCount)
            {
                Kind = kind;
                EdgeVertices = edgeVertices;
                PointVertices = pointVertices;
                Indices = indices;
                EdgePairs = edgePairs;
                SourceEdgeEndpoints = sourceEdgeEndpoints;
                SourcePointCenters = sourcePointCenters;
                SourcePointColors = sourcePointColors;
                SourcePointSizes = sourcePointSizes;
                Bounds = bounds;
                Diagnostics = diagnostics;
                PrimitiveCount = primitiveCount;
            }

            internal static MeshData CreateEdges(
                EdgeVertex[] vertices,
                int[] indices,
                int[] edgePairs,
                Vector3[] sourceEndpoints,
                Bounds bounds,
                BuildDiagnostics diagnostics,
                int edgeCount)
            {
                return new MeshData(
                    MeshKind.Edges,
                    vertices,
                    null,
                    indices,
                    edgePairs,
                    sourceEndpoints,
                    null,
                    null,
                    null,
                    bounds,
                    diagnostics,
                    edgeCount);
            }

            internal static MeshData CreatePoints(
                PointVertex[] vertices,
                int[] indices,
                Vector3[] sourceCenters,
                Color32[] sourceColors,
                float[] sourceSizes,
                Bounds bounds,
                BuildDiagnostics diagnostics,
                int pointCount)
            {
                return new MeshData(
                    MeshKind.Points,
                    null,
                    vertices,
                    indices,
                    null,
                    null,
                    sourceCenters,
                    sourceColors,
                    sourceSizes,
                    bounds,
                    diagnostics,
                    pointCount);
            }

            internal Mesh CreateMesh(string name)
            {
                if (IsEmpty)
                    return null;

                var mesh = new Mesh
                {
                    name = name,
                    hideFlags = HideFlags.HideAndDontSave,
                    indexFormat = VertexCount > 65535 ? IndexFormat.UInt32 : IndexFormat.UInt16,
                };

                if (Kind == MeshKind.Edges)
                {
                    mesh.SetVertexBufferParams(
                        EdgeVertices.Length,
                        new VertexAttributeDescriptor(
                            VertexAttribute.Position, VertexAttributeFormat.Float32, 3, 0),
                        new VertexAttributeDescriptor(
                            VertexAttribute.TexCoord0, VertexAttributeFormat.Float32, 3, 0),
                        new VertexAttributeDescriptor(
                            VertexAttribute.TexCoord1, VertexAttributeFormat.Float32, 2, 0));
                    mesh.SetVertexBufferData(
                        EdgeVertices,
                        0,
                        0,
                        EdgeVertices.Length,
                        0,
                        MeshUpdateFlags.DontRecalculateBounds);
                }
                else
                {
                    mesh.SetVertexBufferParams(
                        PointVertices.Length,
                        new VertexAttributeDescriptor(
                            VertexAttribute.Position, VertexAttributeFormat.Float32, 3, 0),
                        new VertexAttributeDescriptor(
                            VertexAttribute.Color, VertexAttributeFormat.UNorm8, 4, 0),
                        new VertexAttributeDescriptor(
                            VertexAttribute.TexCoord0, VertexAttributeFormat.Float32, 2, 0),
                        new VertexAttributeDescriptor(
                            VertexAttribute.TexCoord1, VertexAttributeFormat.Float32, 1, 0));
                    mesh.SetVertexBufferData(
                        PointVertices,
                        0,
                        0,
                        PointVertices.Length,
                        0,
                        MeshUpdateFlags.DontRecalculateBounds);
                }

                mesh.SetIndices(Indices, MeshTopology.Triangles, 0, false);
                mesh.bounds = Bounds;
                return mesh;
            }
        }

        private static readonly Vector2 s_CornerStartLeft = new(-1f, -1f);
        private static readonly Vector2 s_CornerStartRight = new(-1f, 1f);
        private static readonly Vector2 s_CornerEndLeft = new(1f, -1f);
        private static readonly Vector2 s_CornerEndRight = new(1f, 1f);

        internal static MeshData BuildEdges(PcgPolygonPreviewData preview)
        {
            if (preview == null)
                return BuildEdges(Array.Empty<Vector3>(), Array.Empty<int>());

            var pairs = ExtractUniqueEdgePairs(preview, out var extractionDiagnostics);
            var result = BuildEdges(preview.Points, pairs);
            result = MergeEdgeDiagnostics(result, extractionDiagnostics);
            return result;
        }

        internal static int[] ExtractUniqueEdgePairs(
            PcgPolygonPreviewData preview,
            out BuildDiagnostics diagnostics)
        {
            if (preview == null)
            {
                diagnostics = new BuildDiagnostics(0, 0, 0, 0, 0, 0);
                return Array.Empty<int>();
            }

            var offsets = preview.FaceOffsets ?? Array.Empty<int>();
            var indices = preview.FaceIndices ?? Array.Empty<int>();
            var pointCount = preview.Points?.Length ?? 0;
            var drawn = new HashSet<ulong>();
            var pairs = new List<int>(Math.Max(0, offsets.Length * 4));
            var input = 0;
            var invalid = 0;
            var degenerate = 0;

            void AddEdge(int a, int b)
            {
                input++;
                if (a < 0 || b < 0 || a >= pointCount || b >= pointCount)
                {
                    invalid++;
                    return;
                }

                if (a == b)
                {
                    degenerate++;
                    return;
                }

                var lo = a < b ? a : b;
                var hi = a < b ? b : a;
                var key = ((ulong)(uint)lo << 32) | (uint)hi;
                if (!drawn.Add(key))
                    return;

                pairs.Add(lo);
                pairs.Add(hi);
            }

            for (var face = 0; face < offsets.Length; face++)
            {
                var start = offsets[face];
                var end = face + 1 < offsets.Length ? offsets[face + 1] : indices.Length;
                if (start < 0 || end < start || end > indices.Length)
                {
                    invalid++;
                    continue;
                }

                var count = end - start;
                if (count < 2)
                    continue;

                for (var i = start; i + 1 < end; i++)
                    AddEdge(indices[i], indices[i + 1]);

                if (count >= 3 &&
                    PcgPolygonPreviewData.IsSolidPolygonFace(preview.Points, indices, start, end))
                {
                    AddEdge(indices[end - 1], indices[start]);
                }
            }

            var accepted = pairs.Count / 2;
            diagnostics = new BuildDiagnostics(
                input,
                accepted,
                Math.Max(0, input - accepted),
                invalid,
                0,
                degenerate);
            return pairs.Count == 0 ? Array.Empty<int>() : pairs.ToArray();
        }

        internal static MeshData BuildEdges(
            IReadOnlyList<Vector3> points,
            IReadOnlyList<int> edgePairs)
        {
            points ??= Array.Empty<Vector3>();
            edgePairs ??= Array.Empty<int>();

            var inputCount = edgePairs.Count / 2;
            var vertices = new List<EdgeVertex>(inputCount * 4);
            var indices = new List<int>(inputCount * 6);
            var sourceEndpoints = new List<Vector3>(inputCount * 2);
            var validPairs = new List<int>(inputCount * 2);
            var invalid = 0;
            var nonFinite = 0;
            var degenerate = 0;
            var bounds = new Bounds(Vector3.zero, Vector3.zero);
            var hasBounds = false;

            for (var edge = 0; edge < inputCount; edge++)
            {
                var pairIndex = edge * 2;
                var aIndex = edgePairs[pairIndex];
                var bIndex = edgePairs[pairIndex + 1];
                if (aIndex < 0 || bIndex < 0 || aIndex >= points.Count || bIndex >= points.Count)
                {
                    invalid++;
                    continue;
                }

                var a = points[aIndex];
                var b = points[bIndex];
                if (!IsFinite(a) || !IsFinite(b))
                {
                    nonFinite++;
                    continue;
                }

                if ((a - b).sqrMagnitude <= 1e-12f)
                {
                    degenerate++;
                    continue;
                }

                var vertexIndex = vertices.Count;
                vertices.Add(new EdgeVertex(a, b, s_CornerStartLeft));
                vertices.Add(new EdgeVertex(a, b, s_CornerStartRight));
                vertices.Add(new EdgeVertex(a, b, s_CornerEndLeft));
                vertices.Add(new EdgeVertex(a, b, s_CornerEndRight));
                indices.Add(vertexIndex);
                indices.Add(vertexIndex + 1);
                indices.Add(vertexIndex + 2);
                indices.Add(vertexIndex + 1);
                indices.Add(vertexIndex + 3);
                indices.Add(vertexIndex + 2);
                validPairs.Add(aIndex);
                validPairs.Add(bIndex);
                sourceEndpoints.Add(a);
                sourceEndpoints.Add(b);
                IncludeBounds(ref bounds, ref hasBounds, a);
                IncludeBounds(ref bounds, ref hasBounds, b);
            }

            if (!hasBounds)
                bounds = new Bounds(Vector3.zero, Vector3.zero);

            var accepted = sourceEndpoints.Count / 2;
            var diagnostics = new BuildDiagnostics(
                inputCount,
                accepted,
                Math.Max(0, inputCount - accepted),
                invalid,
                nonFinite,
                degenerate);
            return MeshData.CreateEdges(
                vertices.ToArray(),
                indices.ToArray(),
                validPairs.ToArray(),
                sourceEndpoints.ToArray(),
                bounds,
                diagnostics,
                accepted);
        }

        internal static MeshData BuildEdgeEndpoints(IReadOnlyList<Vector3> endpoints)
        {
            endpoints ??= Array.Empty<Vector3>();
            var pairs = new int[endpoints.Count];
            for (var i = 0; i < pairs.Length; i++)
                pairs[i] = i;
            return BuildEdges(endpoints, pairs);
        }

        internal static MeshData BuildPoints(IReadOnlyList<Vector3> points)
        {
            return BuildPoints(points, null, null, new Color32(255, 255, 255, 255), 1f);
        }

        internal static MeshData BuildPoints(
            IReadOnlyList<Vector3> points,
            IReadOnlyList<Color32> colors,
            IReadOnlyList<float> sizes,
            Color32 defaultColor,
            float defaultSize)
        {
            points ??= Array.Empty<Vector3>();
            var vertices = new List<PointVertex>(points.Count * 4);
            var indices = new List<int>(points.Count * 6);
            var sourceCenters = new List<Vector3>(points.Count);
            var sourceColors = new List<Color32>(points.Count);
            var sourceSizes = new List<float>(points.Count);
            var nonFinite = 0;
            var bounds = new Bounds(Vector3.zero, Vector3.zero);
            var hasBounds = false;

            for (var point = 0; point < points.Count; point++)
            {
                var center = points[point];
                if (!IsFinite(center))
                {
                    nonFinite++;
                    continue;
                }

                var color = colors != null && point < colors.Count ? colors[point] : defaultColor;
                var size = sizes != null && point < sizes.Count ? sizes[point] : defaultSize;
                if (float.IsNaN(size) || float.IsInfinity(size) || size <= 0f)
                    size = defaultSize > 0f ? defaultSize : 1f;

                var vertexIndex = vertices.Count;
                vertices.Add(new PointVertex(center, new Vector2(-1f, -1f), color, size));
                vertices.Add(new PointVertex(center, new Vector2(-1f, 1f), color, size));
                vertices.Add(new PointVertex(center, new Vector2(1f, -1f), color, size));
                vertices.Add(new PointVertex(center, new Vector2(1f, 1f), color, size));
                indices.Add(vertexIndex);
                indices.Add(vertexIndex + 1);
                indices.Add(vertexIndex + 2);
                indices.Add(vertexIndex + 1);
                indices.Add(vertexIndex + 3);
                indices.Add(vertexIndex + 2);
                sourceCenters.Add(center);
                sourceColors.Add(color);
                sourceSizes.Add(size);
                IncludeBounds(ref bounds, ref hasBounds, center);
            }

            if (!hasBounds)
                bounds = new Bounds(Vector3.zero, Vector3.zero);

            var accepted = sourceCenters.Count;
            var diagnostics = new BuildDiagnostics(
                points.Count,
                accepted,
                Math.Max(0, points.Count - accepted),
                0,
                nonFinite,
                0);
            return MeshData.CreatePoints(
                vertices.ToArray(),
                indices.ToArray(),
                sourceCenters.ToArray(),
                sourceColors.ToArray(),
                sourceSizes.ToArray(),
                bounds,
                diagnostics,
                accepted);
        }

        private static MeshData MergeEdgeDiagnostics(
            MeshData result,
            BuildDiagnostics extraction)
        {
            var upload = result.Diagnostics;
            var merged = new BuildDiagnostics(
                extraction.InputPrimitiveCount,
                upload.AcceptedPrimitiveCount,
                Math.Max(0, extraction.InputPrimitiveCount - upload.AcceptedPrimitiveCount),
                extraction.InvalidIndexCount + upload.InvalidIndexCount,
                upload.NonFiniteCount,
                extraction.DegenerateCount + upload.DegenerateCount);
            return MeshData.CreateEdges(
                result.EdgeVertices,
                result.Indices,
                result.EdgePairs,
                result.SourceEdgeEndpoints,
                result.Bounds,
                merged,
                result.PrimitiveCount);
        }

        private static bool IsFinite(Vector3 value)
        {
            return !float.IsNaN(value.x) && !float.IsInfinity(value.x) &&
                   !float.IsNaN(value.y) && !float.IsInfinity(value.y) &&
                   !float.IsNaN(value.z) && !float.IsInfinity(value.z);
        }

        private static void IncludeBounds(ref Bounds bounds, ref bool hasBounds, Vector3 value)
        {
            if (!hasBounds)
            {
                bounds = new Bounds(value, Vector3.zero);
                hasBounds = true;
            }
            else
            {
                bounds.Encapsulate(value);
            }
        }
    }
}
