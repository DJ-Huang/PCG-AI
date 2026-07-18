using System.IO;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgHeightFieldIntegrationTests
    {
        [Test]
        public void TerrainDemo_CooksHighResolutionMeshWithNormalsAndUv()
        {
            var json = ReadExample("terrain-demo.pcg");
            var result = PcgGraphLoader.Execute(json, 42);

            Assert.That(result, Is.Not.Null);
            Assert.That(result.Kind, Is.EqualTo(PcgExecuteKind.Mesh));
            Assert.That(result.VertexCount, Is.EqualTo(257 * 257));
            Assert.That(result.IndexCount, Is.EqualTo(256 * 256 * 6));
            Assert.That(
                PcgResultParser.TryParseMeshBinary(
                    result.MeshBinary, out var mesh, out var error),
                Is.True,
                error);
            Assert.That(mesh.vertexCount, Is.EqualTo(257 * 257));
            Assert.That(mesh.indexFormat, Is.EqualTo(IndexFormat.UInt32));
            Assert.That(mesh.normals.Length, Is.EqualTo(mesh.vertexCount));
            Assert.That(mesh.uv.Length, Is.EqualTo(mesh.vertexCount));
            Object.DestroyImmediate(mesh);
        }

        [Test]
        public void ProjectScatterDemo_CooksTypedPointsWithSurfaceNormals()
        {
            var json = ReadExample("terrain-project-scatter-demo.pcg");
            var result = PcgGraphLoader.Execute(json, 42);

            Assert.That(result, Is.Not.Null);
            Assert.That(result.Kind, Is.EqualTo(PcgExecuteKind.Points));
            Assert.That(result.PointCount, Is.EqualTo(256));
            Assert.That(
                PcgResultParser.TryParsePointBinary(
                    result.PointBinary, out var points, out var error),
                Is.True,
                error);
            Assert.That(points, Has.Count.EqualTo(256));
            Assert.That(points.TrueForAll(point => point.HasNormal), Is.True);
            Assert.That(points.Exists(point => point.Position.y >= 13.99f), Is.True);
        }

        private static string ReadExample(string fileName)
        {
            var path = Path.Combine(
                Application.dataPath, "PcgPlugin", "Examples", "PCGDemo", fileName);
            Assert.That(File.Exists(path), Is.True, path);
            return File.ReadAllText(path);
        }
    }
}

