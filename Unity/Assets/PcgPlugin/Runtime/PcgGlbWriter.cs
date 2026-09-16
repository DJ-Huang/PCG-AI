using System;
using System.IO;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Minimal glTF-binary (GLB) writer for PCG polygon geometry: one mesh, one
    /// triangulated primitive, POSITION only. Faces are fan-triangulated.
    /// Used by Meshy mesh-op / retexture Generate to upload the upstream cook result.
    /// </summary>
    public static class PcgGlbWriter
    {
        public static byte[] WriteFromGeometryBinary(byte[] geometryBinary, out string error)
        {
            error = null;
            if (!PcgResultParser.TryParseGeometryBinary(geometryBinary, out var polygon, out var parseError))
            {
                error = parseError ?? "Geometry binary parse failed.";
                return null;
            }
            if (polygon.Points.Length == 0 || polygon.FaceCount == 0)
            {
                error = "Geometry binary has no polygon geometry.";
                return null;
            }
            return Write(polygon.Points, polygon.FaceOffsets, polygon.FaceIndices);
        }

        public static byte[] Write(Vector3[] points, int[] faceOffsets, int[] faceIndices)
        {
            var triangleIndices = Triangulate(faceOffsets, faceIndices);
            var bin = BuildBin(points, triangleIndices, out var positionByteLength, out var min, out var max);
            var json = BuildJson(points.Length, triangleIndices.Length, positionByteLength, min, max);
            return Pack(json, bin);
        }

        private static int[] Triangulate(int[] faceOffsets, int[] faceIndices)
        {
            var triangleCount = 0;
            for (var face = 0; face < faceOffsets.Length; face++)
            {
                var start = faceOffsets[face];
                var end = face + 1 < faceOffsets.Length ? faceOffsets[face + 1] : faceIndices.Length;
                var cornerCount = Math.Max(0, end - start);
                if (cornerCount >= 3)
                    triangleCount += cornerCount - 2;
            }

            var indices = new int[triangleCount * 3];
            var cursor = 0;
            for (var face = 0; face < faceOffsets.Length; face++)
            {
                var start = faceOffsets[face];
                var end = face + 1 < faceOffsets.Length ? faceOffsets[face + 1] : faceIndices.Length;
                for (var corner = start + 2; corner < end; corner++)
                {
                    indices[cursor++] = faceIndices[start];
                    indices[cursor++] = faceIndices[corner - 1];
                    indices[cursor++] = faceIndices[corner];
                }
            }
            return indices;
        }

        private static byte[] BuildBin(
            Vector3[] points, int[] triangleIndices,
            out int positionByteLength, out Vector3 min, out Vector3 max)
        {
            positionByteLength = points.Length * 3 * sizeof(float);
            var indexByteOffset = Align4(positionByteLength);
            var total = indexByteOffset + triangleIndices.Length * sizeof(uint);
            var bin = new byte[total];

            min = new Vector3(float.MaxValue, float.MaxValue, float.MaxValue);
            max = new Vector3(float.MinValue, float.MinValue, float.MinValue);
            var offset = 0;
            foreach (var p in points)
            {
                min = Vector3.Min(min, p);
                max = Vector3.Max(max, p);
                WriteFloat(bin, ref offset, p.x);
                WriteFloat(bin, ref offset, p.y);
                WriteFloat(bin, ref offset, p.z);
            }

            offset = indexByteOffset;
            foreach (var index in triangleIndices)
                WriteUInt(bin, ref offset, checked((uint)index));
            return bin;
        }

        private static string BuildJson(
            int vertexCount, int indexCount, int positionByteLength, Vector3 min, Vector3 max)
        {
            var indexByteOffset = Align4(positionByteLength);
            var totalBinLength = indexByteOffset + indexCount * 4;
            return "{"
                   + "\"asset\":{\"version\":\"2.0\",\"generator\":\"PICG PcgGlbWriter\"},"
                   + "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
                   + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"mode\":4}]}],"
                   + "\"bufferViews\":["
                   + $"{{\"buffer\":0,\"byteOffset\":0,\"byteLength\":{positionByteLength},\"target\":34962}},"
                   + $"{{\"buffer\":0,\"byteOffset\":{indexByteOffset},\"byteLength\":{indexCount * 4},\"target\":34963}}"
                   + "],"
                   + "\"accessors\":["
                   + $"{{\"bufferView\":0,\"componentType\":5126,\"count\":{vertexCount},\"type\":\"VEC3\","
                   + $"\"min\":[{F(min.x)},{F(min.y)},{F(min.z)}],\"max\":[{F(max.x)},{F(max.y)},{F(max.z)}]}},"
                   + $"{{\"bufferView\":1,\"componentType\":5125,\"count\":{indexCount},\"type\":\"SCALAR\"}}"
                   + "],"
                   + $"\"buffers\":[{{\"byteLength\":{totalBinLength}}}]"
                   + "}";
        }

        private static string F(float value) =>
            value.ToString("R", System.Globalization.CultureInfo.InvariantCulture);

        private static byte[] Pack(string json, byte[] bin)
        {
            var jsonBytes = Encoding.UTF8.GetBytes(json);
            var jsonPadded = Align4(jsonBytes.Length);
            var binPadded = Align4(bin.Length);
            var total = 12 + 8 + jsonPadded + 8 + binPadded;

            var glb = new byte[total];
            var offset = 0;
            WriteUInt(glb, ref offset, 0x46546C67); // "glTF"
            WriteUInt(glb, ref offset, 2u);
            WriteUInt(glb, ref offset, (uint)total);
            WriteUInt(glb, ref offset, (uint)jsonPadded);
            WriteUInt(glb, ref offset, 0x4E4F534A); // "JSON"
            Buffer.BlockCopy(jsonBytes, 0, glb, offset, jsonBytes.Length);
            offset += jsonPadded;
            WriteUInt(glb, ref offset, (uint)binPadded);
            WriteUInt(glb, ref offset, 0x004E4942); // "BIN\0"
            Buffer.BlockCopy(bin, 0, glb, offset, bin.Length);
            return glb;
        }

        private static int Align4(int value) => (value + 3) & ~3;

        private static void WriteFloat(byte[] buffer, ref int offset, float value)
        {
            var bytes = BitConverter.GetBytes(value);
            if (!BitConverter.IsLittleEndian)
                Array.Reverse(bytes);
            Buffer.BlockCopy(bytes, 0, buffer, offset, 4);
            offset += 4;
        }

        private static void WriteUInt(byte[] buffer, ref int offset, uint value)
        {
            var bytes = BitConverter.GetBytes(value);
            if (!BitConverter.IsLittleEndian)
                Array.Reverse(bytes);
            Buffer.BlockCopy(bytes, 0, buffer, offset, 4);
            offset += 4;
        }
    }
}
