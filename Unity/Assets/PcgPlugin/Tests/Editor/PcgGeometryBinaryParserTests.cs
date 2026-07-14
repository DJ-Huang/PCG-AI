using System;
using System.Collections.Generic;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgGeometryBinaryParserTests
    {
        private const uint ChunkPoints = 1u;
        private const uint ChunkFaceOffsets = 2u;
        private const uint ChunkFaceIndices = 3u;
        private const uint ChunkTriangulation = 6u;

        [Test]
        public void U1_ValidOneQuad_WithIgnorableTriangulation_Parses()
        {
            var data = BuildOneQuadBinary(includeTriangulation: true);
            Assert.IsTrue(
                PcgResultParser.TryParseGeometryBinary(data, out var preview, out var error),
                error);
            Assert.AreEqual(4, preview.Points.Length);
            Assert.AreEqual(1, preview.FaceCount);
            Assert.AreEqual(0, preview.FaceOffsets[0]);
            Assert.AreEqual(4, preview.FaceIndices.Length);
            CollectionAssert.AreEqual(new[] { 0, 1, 2, 3 }, preview.FaceIndices);
        }

        [Test]
        public void U2_BadMagic_FailsWithMagicError()
        {
            var data = BuildOneQuadBinary(includeTriangulation: false);
            data[0] = 0x00;
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out var error));
            Assert.IsNull(preview);
            StringAssert.Contains("magic", error.ToLowerInvariant());
        }

        [Test]
        public void U2_BadVersion_FailsWithVersionError()
        {
            var data = BuildOneQuadBinary(includeTriangulation: false);
            // version at offset 4
            data[4] = 0x09;
            data[5] = 0x00;
            data[6] = 0x00;
            data[7] = 0x00;
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out var error));
            Assert.IsNull(preview);
            StringAssert.Contains("version", error.ToLowerInvariant());
        }

        [Test]
        public void U3_TruncatedHeader_FailsWithoutThrowing()
        {
            var data = new byte[8];
            Assert.DoesNotThrow(() =>
            {
                Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out var error));
                Assert.IsNull(preview);
                Assert.IsFalse(string.IsNullOrEmpty(error));
            });
        }

        [Test]
        public void U3_ChunkSizeOverflow_FailsWithoutThrowing()
        {
            var data = BuildOneQuadBinary(includeTriangulation: false);
            // Corrupt POINTS chunk size (first chunk after 16-byte header) to exceed buffer.
            var sizeOffset = 16 + 4;
            BitConverter.GetBytes(0x7FFFFFF0u).CopyTo(data, sizeOffset);
            Assert.DoesNotThrow(() =>
            {
                Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out _));
                Assert.IsNull(preview);
            });
        }

        [TestCase(ChunkPoints, "POINTS")]
        [TestCase(ChunkFaceOffsets, "FACE_OFFSETS")]
        [TestCase(ChunkFaceIndices, "FACE_INDICES")]
        public void U4_MissingRequiredChunk_Fails(uint omitChunk, string expectedToken)
        {
            var data = BuildOneQuadBinary(includeTriangulation: false, omitChunkId: omitChunk);
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out var error));
            Assert.IsNull(preview);
            StringAssert.Contains(expectedToken, error);
        }

        [Test]
        public void U5_NonZeroFirstOffset_Fails()
        {
            var data = BuildCustomBinary(
                pointCount: 4,
                faceCount: 1,
                offsets: new[] { 1u },
                indices: new uint[] { 0, 1, 2, 3 });
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out _));
            Assert.IsNull(preview);
        }

        [Test]
        public void U5_NonIncreasingOffsets_Fails()
        {
            var data = BuildCustomBinary(
                pointCount: 6,
                faceCount: 2,
                offsets: new[] { 0u, 0u },
                indices: new uint[] { 0, 1, 2, 3, 4, 5 });
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out _));
            Assert.IsNull(preview);
        }

        [Test]
        public void U5_FaceFewerThanThree_Fails()
        {
            var data = BuildCustomBinary(
                pointCount: 2,
                faceCount: 1,
                offsets: new[] { 0u },
                indices: new uint[] { 0, 1 });
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out _));
            Assert.IsNull(preview);
        }

        [Test]
        public void U6_FaceIndexOutOfRange_Fails()
        {
            var data = BuildCustomBinary(
                pointCount: 3,
                faceCount: 1,
                offsets: new[] { 0u },
                indices: new uint[] { 0, 1, 3 });
            Assert.IsFalse(PcgResultParser.TryParseGeometryBinary(data, out var preview, out _));
            Assert.IsNull(preview);
        }

        private static byte[] BuildOneQuadBinary(bool includeTriangulation, uint? omitChunkId = null)
        {
            return BuildCustomBinary(
                pointCount: 4,
                faceCount: 1,
                offsets: new[] { 0u },
                indices: new uint[] { 0, 1, 2, 3 },
                includeTriangulation: includeTriangulation,
                omitChunkId: omitChunkId);
        }

        private static byte[] BuildCustomBinary(
            int pointCount,
            int faceCount,
            uint[] offsets,
            uint[] indices,
            bool includeTriangulation = false,
            uint? omitChunkId = null)
        {
            var bytes = new List<byte>(128);
            void WriteU32(uint v) => bytes.AddRange(BitConverter.GetBytes(v));
            void WriteF32(float v) => bytes.AddRange(BitConverter.GetBytes(v));
            void WriteChunk(uint id, Action writePayload)
            {
                if (omitChunkId.HasValue && omitChunkId.Value == id)
                    return;
                WriteU32(id);
                var sizeIndex = bytes.Count;
                WriteU32(0);
                var payloadStart = bytes.Count;
                writePayload();
                var size = (uint)(bytes.Count - payloadStart);
                var sizeBytes = BitConverter.GetBytes(size);
                for (var i = 0; i < 4; i++)
                    bytes[sizeIndex + i] = sizeBytes[i];
            }

            WriteU32(PcgResultParser.GeometryBinaryMagic);
            WriteU32(PcgResultParser.GeometryBinaryVersion);
            WriteU32((uint)pointCount);
            WriteU32((uint)faceCount);

            WriteChunk(ChunkPoints, () =>
            {
                for (var i = 0; i < pointCount; i++)
                {
                    WriteF32(i);
                    WriteF32(0f);
                    WriteF32(0f);
                }
            });

            WriteChunk(ChunkFaceOffsets, () =>
            {
                foreach (var o in offsets)
                    WriteU32(o);
            });

            WriteChunk(ChunkFaceIndices, () =>
            {
                foreach (var idx in indices)
                    WriteU32(idx);
            });

            if (includeTriangulation)
            {
                WriteChunk(ChunkTriangulation, () =>
                {
                    WriteU32(0);
                    WriteU32(1);
                    WriteU32(2);
                    WriteU32(0);
                    WriteU32(2);
                    WriteU32(3);
                });
            }

            return bytes.ToArray();
        }
    }

    public sealed class PcgGraphCookCacheGeometryTests
    {
        [Test]
        public void U7_StoreTryGet_ClonesGeometryBinaryIndependently()
        {
            PcgGraphCookCache.Clear();
            var key = "geometry-clone-test";
            var source = new PcgGraphExecuteResult
            {
                Kind = PcgExecuteKind.Mesh,
                GeometryBinary = new byte[] { 1, 2, 3, 4, 5 },
                MeshBinary = new byte[] { 9, 9 },
            };

            PcgGraphCookCache.Store(key, source);
            Assert.IsTrue(PcgGraphCookCache.TryGet(key, out var first));
            Assert.IsTrue(PcgGraphCookCache.TryGet(key, out var second));
            Assert.AreNotSame(first.GeometryBinary, second.GeometryBinary);
            CollectionAssert.AreEqual(source.GeometryBinary, first.GeometryBinary);

            first.GeometryBinary[0] = 99;
            Assert.IsTrue(PcgGraphCookCache.TryGet(key, out var third));
            Assert.AreEqual(1, third.GeometryBinary[0]);
            PcgGraphCookCache.Clear();
        }
    }
}
