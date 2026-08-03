using System;
using System.Collections.Generic;
using DJTechEditor.PCG.Rendering;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Tests
{
    public class PcgScenePreviewMeshBuilderTests
    {
        [Test]
        public void SolidNgon_UsesOriginalRingWithoutTriangulationDiagonal()
        {
            var preview = new PcgPolygonPreviewData(
                new[]
                {
                    new Vector3(0f, 0f, 0f),
                    new Vector3(1f, 0f, 0f),
                    new Vector3(1f, 1f, 0f),
                    new Vector3(0f, 1f, 0f),
                },
                new[] { 0 },
                new[] { 0, 1, 2, 3 });

            var data = PcgScenePreviewMeshBuilder.BuildEdges(preview);
            var edges = ReadEdges(data.EdgePairs);

            Assert.That(data.PrimitiveCount, Is.EqualTo(4));
            Assert.That(data.VertexCount, Is.EqualTo(16));
            Assert.That(data.IndexCount, Is.EqualTo(24));
            Assert.That(edges, Does.Contain("0-1"));
            Assert.That(edges, Does.Contain("1-2"));
            Assert.That(edges, Does.Contain("2-3"));
            Assert.That(edges, Does.Contain("0-3"));
            Assert.That(edges, Does.Not.Contain("0-2"));
            Assert.That(edges, Does.Not.Contain("1-3"));
        }

        [Test]
        public void OpenAndClosedPolylines_DoNotAddAnExtraWrapEdge()
        {
            var points = new[]
            {
                new Vector3(0f, 0f, 0f),
                new Vector3(1f, 0f, 0f),
                new Vector3(2f, 0f, 0f),
                new Vector3(1f, 1f, 0f),
            };

            var open = new PcgPolygonPreviewData(points, new[] { 0 }, new[] { 0, 1, 2 });
            var closed = new PcgPolygonPreviewData(points, new[] { 0 }, new[] { 0, 1, 2, 0 });

            Assert.That(PcgScenePreviewMeshBuilder.BuildEdges(open).PrimitiveCount, Is.EqualTo(2));
            Assert.That(PcgScenePreviewMeshBuilder.BuildEdges(closed).PrimitiveCount, Is.EqualTo(3));
        }

        [Test]
        public void SharedEdges_AreEmittedOnce()
        {
            var preview = new PcgPolygonPreviewData(
                new[]
                {
                    new Vector3(0f, 0f, 0f),
                    new Vector3(1f, 0f, 0f),
                    new Vector3(1f, 1f, 0f),
                    new Vector3(0f, 1f, 0f),
                },
                new[] { 0, 3 },
                new[] { 0, 1, 2, 0, 2, 3 });

            var data = PcgScenePreviewMeshBuilder.BuildEdges(preview);

            Assert.That(data.PrimitiveCount, Is.EqualTo(5));
            Assert.That(data.Diagnostics.FilteredPrimitiveCount, Is.GreaterThan(0));
        }

        [Test]
        public void InvalidAndNonFiniteEdges_AreFilteredWithFiniteBounds()
        {
            var points = new[]
            {
                new Vector3(0f, 0f, 0f),
                new Vector3(1f, 0f, 0f),
                new Vector3(float.NaN, 0f, 0f),
            };
            var pairs = new[] { 0, 1, 1, 2, 0, 0, 7, 1 };

            var data = PcgScenePreviewMeshBuilder.BuildEdges(points, pairs);

            Assert.That(data.PrimitiveCount, Is.EqualTo(1));
            Assert.That(data.Diagnostics.InvalidIndexCount, Is.EqualTo(1));
            Assert.That(data.Diagnostics.NonFiniteCount, Is.EqualTo(1));
            Assert.That(data.Diagnostics.DegenerateCount, Is.EqualTo(1));
            Assert.That(IsFinite(data.Bounds.min), Is.True);
            Assert.That(IsFinite(data.Bounds.max), Is.True);
        }

        [Test]
        public void PointPayload_UsesFourVerticesAndSixIndicesPerPoint()
        {
            var data = PcgScenePreviewMeshBuilder.BuildPoints(
                new[]
                {
                    new Vector3(0f, 0f, 0f),
                    new Vector3(1f, 2f, 3f),
                });

            Assert.That(data.PrimitiveCount, Is.EqualTo(2));
            Assert.That(data.VertexCount, Is.EqualTo(8));
            Assert.That(data.IndexCount, Is.EqualTo(12));
            Assert.That(data.PointVertices[0].Corner, Is.EqualTo(new Vector2(-1f, -1f)));
            Assert.That(data.PointVertices[3].Corner, Is.EqualTo(new Vector2(1f, 1f)));
            foreach (var index in data.Indices)
                Assert.That(index, Is.InRange(0, data.VertexCount - 1));
        }

        [Test]
        public void PointMesh_SwitchesToUInt32IndicesAbove65535Vertices()
        {
            var points = new Vector3[16384];
            for (var i = 0; i < points.Length; i++)
                points[i] = new Vector3(i % 128, i / 128, 0f);

            var data = PcgScenePreviewMeshBuilder.BuildPoints(points);
            var mesh = data.CreateMesh("PcgScenePreviewMeshBuilderTests");
            try
            {
                Assert.That(mesh, Is.Not.Null);
                Assert.That(mesh.indexFormat, Is.EqualTo(UnityEngine.Rendering.IndexFormat.UInt32));
            }
            finally
            {
                if (mesh != null)
                    UnityEngine.Object.DestroyImmediate(mesh);
            }
        }

        [Test]
        public void PointMesh_UsesCanonicalVertexAttributeOrder()
        {
            var data = PcgScenePreviewMeshBuilder.BuildPoints(new[] { Vector3.zero });
            var mesh = data.CreateMesh("PcgScenePreviewMeshBuilderTests");
            try
            {
                var attributes = mesh.GetVertexAttributes();
                Assert.That(
                    attributes,
                    Has.Length.EqualTo(4));
                Assert.That(attributes[0].attribute, Is.EqualTo(VertexAttribute.Position));
                Assert.That(attributes[1].attribute, Is.EqualTo(VertexAttribute.Color));
                Assert.That(attributes[2].attribute, Is.EqualTo(VertexAttribute.TexCoord0));
                Assert.That(attributes[3].attribute, Is.EqualTo(VertexAttribute.TexCoord1));
            }
            finally
            {
                if (mesh != null)
                    UnityEngine.Object.DestroyImmediate(mesh);
            }
        }

        private static HashSet<string> ReadEdges(IReadOnlyList<int> pairs)
        {
            var result = new HashSet<string>();
            for (var i = 0; i + 1 < pairs.Count; i += 2)
                result.Add($"{pairs[i]}-{pairs[i + 1]}");
            return result;
        }

        private static bool IsFinite(Vector3 value)
        {
            return !float.IsNaN(value.x) && !float.IsInfinity(value.x) &&
                   !float.IsNaN(value.y) && !float.IsInfinity(value.y) &&
                   !float.IsNaN(value.z) && !float.IsInfinity(value.z);
        }
    }
}
