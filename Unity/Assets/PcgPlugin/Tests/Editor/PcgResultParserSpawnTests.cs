using System;
using System.Collections.Generic;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgResultParserSpawnTests
    {
        [Test]
        public void PcmsSixPrototypes_PreservesOrderCountsAndMaterials()
        {
            var pointCounts = new[] { 3, 0, 5, 1, 4, 2 };
            var meshes = new byte[6][];
            for (var i = 0; i < meshes.Length; i++)
                meshes[i] = BuildTriangleMesh(i + 1f, $"variant_{i}");

            var bytes = new List<byte>();
            WriteU32(bytes, PcgNative.MultiSpawnMagic);
            WriteU32(bytes, 1u);
            WriteI32(bytes, meshes.Length);
            foreach (var count in pointCounts)
                WriteI32(bytes, count);
            foreach (var mesh in meshes)
                WriteI32(bytes, mesh.Length);
            foreach (var mesh in meshes)
                bytes.AddRange(mesh);

            Assert.IsTrue(PcgResultParser.TryParseSpawnMeshBinary(
                bytes.ToArray(), out var prototypes, out var error), error);
            Assert.AreEqual(6, prototypes.Count);
            for (var i = 0; i < prototypes.Count; i++)
            {
                Assert.AreEqual(pointCounts[i], prototypes[i].PointCount, $"point count {i}");
                CollectionAssert.AreEqual(new[] { $"variant_{i}" }, prototypes[i].MaterialNames);
                Assert.AreEqual(i + 1f, prototypes[i].Mesh.bounds.max.x, 0.0001f, $"mesh order {i}");
                UnityEngine.Object.DestroyImmediate(prototypes[i].Mesh);
            }
        }

        private static byte[] BuildTriangleMesh(float width, string materialName)
        {
            var materialBytes = new List<byte>();
            WriteU32(materialBytes, 1u);
            WriteUtf8(materialBytes, materialName);
            WriteU32(materialBytes, 0u);

            var bytes = new List<byte>();
            WriteU32(bytes, PcgNative.MeshBinaryMagic);
            WriteU32(bytes, PcgNative.MeshBinaryVersion3);
            WriteU32(bytes, 3u);
            WriteU32(bytes, 3u);
            WriteU32(bytes, PcgNative.MeshBinaryFlagHasMaterials);
            WriteU32(bytes, (uint)materialBytes.Count);
            foreach (var point in new[]
                     {
                         Vector3.zero, new Vector3(width, 0f, 0f), new Vector3(0f, 1f, 0f)
                     })
            {
                WriteF32(bytes, point.x);
                WriteF32(bytes, point.y);
                WriteF32(bytes, point.z);
            }
            WriteU32(bytes, 0u);
            WriteU32(bytes, 1u);
            WriteU32(bytes, 2u);
            bytes.AddRange(materialBytes);
            return bytes.ToArray();
        }

        private static void WriteUtf8(List<byte> bytes, string value)
        {
            var encoded = System.Text.Encoding.UTF8.GetBytes(value);
            WriteU32(bytes, (uint)encoded.Length);
            bytes.AddRange(encoded);
        }

        private static void WriteU32(List<byte> bytes, uint value) =>
            bytes.AddRange(BitConverter.GetBytes(value));

        private static void WriteI32(List<byte> bytes, int value) =>
            bytes.AddRange(BitConverter.GetBytes(value));

        private static void WriteF32(List<byte> bytes, float value) =>
            bytes.AddRange(BitConverter.GetBytes(value));
    }
}
