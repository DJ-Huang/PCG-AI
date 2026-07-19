using System;
using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    [Serializable]
    public class PcgPoint
    {
        public float x;
        public float y;
        public float z;
    }

    [Serializable]
    public class PcgSplinePoint
    {
        public float x;
        public float y;
        public float z;
    }

    [Serializable]
    public class PcgSplineEntry
    {
        public PcgSplinePoint[] points;
        public bool closed;
    }

    [Serializable]
    public class PcgSplinePayload
    {
        public PcgSplineEntry[] splines;
    }

    [Serializable]
    public class PcgMeshVertex
    {
        public float x;
        public float y;
        public float z;
    }

    [Serializable]
    public class PcgMeshPayload
    {
        public string dataType;
        public PcgMeshVertex[] vertices;
        public int[] triangles;
        public int vertexCount;
        public int triangleCount;
    }

    public enum PcgResultKind
    {
        Unknown,
        Points,
        Splines,
        Mesh,
        HeightField,
    }

    [Serializable]
    public class PcgExecutionResult
    {
        public string status;
        public string prefab;
        public float scale = 1f;
        public int pointCount;
        public PcgPoint[] points;
    }

    public struct PcgScatterPoint
    {
        public Vector3 Position;
        public Vector3 Normal;
        public bool HasNormal;
        public float Scale;
        public bool HasScale;
    }

    /// <summary>
    /// Parses result JSON produced by pcg_execute_graph.
    /// </summary>
    public static class PcgResultParser
    {
        public static PcgResultKind DetectKind(string resultJson)
        {
            if (string.IsNullOrWhiteSpace(resultJson))
                return PcgResultKind.Unknown;

            if (resultJson.Contains("\"dataType\":\"mesh\"") || resultJson.Contains("\"dataType\": \"mesh\""))
                return PcgResultKind.Mesh;
            if (resultJson.Contains("\"splines\""))
                return PcgResultKind.Splines;
            if (resultJson.Contains("\"points\""))
                return PcgResultKind.Points;
            if (resultJson.Contains("\"kind\":\"heightfield\"") ||
                resultJson.Contains("\"kind\": \"heightfield\""))
                return PcgResultKind.HeightField;

            return PcgResultKind.Unknown;
        }

        public static PcgResultKind DetectKind(PcgGraphExecuteResult result)
        {
            if (result == null)
                return PcgResultKind.Unknown;

            if (result.Kind == PcgExecuteKind.Mesh)
                return PcgResultKind.Mesh;
            if (result.Kind == PcgExecuteKind.Points)
                return PcgResultKind.Points;
            if (result.HeightFieldBinary != null && result.HeightFieldBinary.Length > 0 &&
                result.Kind == PcgExecuteKind.Json)
                return PcgResultKind.HeightField;

            return DetectKind(result.Json);
        }

        public static unsafe bool TryParseMeshBinary(byte[] data, out Mesh mesh, out string error)
        {
            return TryParseMeshBinary(data, out mesh, out _, out error);
        }

        public static unsafe bool TryParseMeshBinary(
            byte[] data, out Mesh mesh, out string[] materialNames, out string error)
        {
            mesh = null;
            materialNames = Array.Empty<string>();
            error = null;

            if (data == null || data.Length < PcgNative.MeshBinaryHeaderSize)
            {
                error = "Mesh binary payload is too small.";
                return false;
            }

            try
            {
                fixed (byte* ptr = data)
                {
                    var magic = *(uint*)ptr;
                    if (magic != PcgNative.MeshBinaryMagic)
                    {
                        error = $"Invalid mesh binary magic: 0x{magic:X8}";
                        return false;
                    }

                    var version = *(uint*)(ptr + 4);
                    var vertexCount = *(int*)(ptr + 8);
                    var indexCount = *(int*)(ptr + 12);

                    if (indexCount % 3 != 0)
                    {
                        error = $"Mesh binary index count not divisible by 3: {indexCount}";
                        return false;
                    }

                    int headerSize;
                    bool hasNormals = false;
                    bool hasColors = false;
                    bool hasUVs = false;
                    bool hasMaterials = false;
                    int materialSectionSize = 0;

                    if (version == PcgNative.MeshBinaryVersion2 ||
                        version == PcgNative.MeshBinaryVersion3)
                    {
                        headerSize = version == PcgNative.MeshBinaryVersion3
                            ? PcgNative.MeshBinaryV3HeaderSize
                            : PcgNative.MeshBinaryV2HeaderSize;
                        if (data.Length < headerSize)
                        {
                            error = "Mesh binary v2 header truncated.";
                            return false;
                        }
                        var flags = *(uint*)(ptr + 16);
                        hasNormals = (flags & PcgNative.MeshBinaryFlagHasNormals) != 0;
                        hasColors = (flags & PcgNative.MeshBinaryFlagHasColors) != 0;
                        hasUVs = (flags & PcgNative.MeshBinaryFlagHasUVs) != 0;
                        hasMaterials = version == PcgNative.MeshBinaryVersion3 &&
                            (flags & PcgNative.MeshBinaryFlagHasMaterials) != 0;
                        if (version == PcgNative.MeshBinaryVersion3)
                            materialSectionSize = *(int*)(ptr + 20);
                        if (materialSectionSize < 0)
                            throw new InvalidOperationException("Material section size is negative.");
                    }
                    else if (version == 1u)
                    {
                        headerSize = PcgNative.MeshBinaryHeaderSize;
                    }
                    else
                    {
                        error = $"Unsupported mesh binary version: {version}";
                        return false;
                    }

                    var required = headerSize + vertexCount * 12 + indexCount * 4;
                    if (hasNormals)
                        required += vertexCount * 12;
                    if (hasColors)
                        required += vertexCount * 16;
                    if (hasUVs)
                        required += vertexCount * 8;
                    required += materialSectionSize;
                    if (data.Length < required)
                    {
                        error = $"Mesh binary truncated (need {required} bytes, got {data.Length}).";
                        return false;
                    }

                    var vertices = new Vector3[vertexCount];
                    fixed (Vector3* dst = vertices)
                    {
                        Buffer.MemoryCopy(ptr + headerSize, dst, vertexCount * 12,
                            vertexCount * 12);
                    }

                    var triangles = new int[indexCount];
                    var indexOffset = headerSize + vertexCount * 12;
                    fixed (int* dst = triangles)
                    {
                        Buffer.MemoryCopy(ptr + indexOffset, dst, indexCount * 4, indexCount * 4);
                    }

                    mesh = new Mesh { name = "PCG Generated Mesh" };
                    if (vertices.Length > 65535)
                        mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
                    mesh.vertices = vertices;

                    var attributeEnd = indexOffset + indexCount * 4;
                    if (hasNormals)
                        attributeEnd += vertexCount * 12;
                    if (hasColors)
                        attributeEnd += vertexCount * 16;
                    if (hasUVs)
                        attributeEnd += vertexCount * 8;

                    if (hasMaterials)
                    {
                        var sectionEnd = attributeEnd + materialSectionSize;
                        var cursor = attributeEnd;
                        if (cursor + 4 > sectionEnd)
                            throw new InvalidOperationException("Material section is truncated.");
                        var slotCount = *(int*)(ptr + cursor);
                        cursor += 4;
                        if (slotCount <= 0 || slotCount > indexCount / 3 + 1)
                            throw new InvalidOperationException($"Invalid material slot count: {slotCount}.");
                        materialNames = new string[slotCount];
                        for (var slot = 0; slot < slotCount; slot++)
                        {
                            if (cursor + 4 > sectionEnd)
                                throw new InvalidOperationException("Material name length is truncated.");
                            var nameSize = *(int*)(ptr + cursor);
                            cursor += 4;
                            if (nameSize < 0 || cursor + nameSize > sectionEnd)
                                throw new InvalidOperationException("Material name is truncated.");
                            materialNames[slot] = nameSize == 0
                                ? string.Empty
                                : System.Text.Encoding.UTF8.GetString(data, cursor, nameSize);
                            cursor += nameSize;
                        }

                        var submeshTriangles = new List<int>[slotCount];
                        for (var slot = 0; slot < slotCount; slot++)
                            submeshTriangles[slot] = new List<int>();
                        for (var triangle = 0; triangle < indexCount / 3; triangle++)
                        {
                            if (cursor + 4 > sectionEnd)
                                throw new InvalidOperationException("Triangle material table is truncated.");
                            var slot = *(int*)(ptr + cursor);
                            cursor += 4;
                            if (slot < 0 || slot >= slotCount)
                                throw new InvalidOperationException($"Invalid triangle material slot: {slot}.");
                            var baseIndex = triangle * 3;
                            submeshTriangles[slot].Add(triangles[baseIndex]);
                            submeshTriangles[slot].Add(triangles[baseIndex + 1]);
                            submeshTriangles[slot].Add(triangles[baseIndex + 2]);
                        }
                        if (cursor != sectionEnd)
                            throw new InvalidOperationException("Material section has trailing bytes.");
                        mesh.subMeshCount = slotCount;
                        for (var slot = 0; slot < slotCount; slot++)
                            mesh.SetTriangles(submeshTriangles[slot], slot, false);
                    }
                    else
                    {
                        mesh.triangles = triangles;
                    }

                    if (hasNormals)
                    {
                        var normals = new Vector3[vertexCount];
                        var normalOffset = indexOffset + indexCount * 4;
                        fixed (Vector3* dst = normals)
                        {
                            Buffer.MemoryCopy(ptr + normalOffset, dst, vertexCount * 12,
                                vertexCount * 12);
                        }
                        mesh.normals = normals;
                    }
                    else
                    {
                        mesh.RecalculateNormals();
                    }

                    if (hasColors)
                    {
                        var colors = new Color[vertexCount];
                        var colorOffset = indexOffset + indexCount * 4;
                        if (hasNormals)
                            colorOffset += vertexCount * 12;
                        fixed (Color* dst = colors)
                        {
                            Buffer.MemoryCopy(ptr + colorOffset, dst, vertexCount * 16,
                                vertexCount * 16);
                        }
                        mesh.colors = colors;
                    }

                    if (hasUVs)
                    {
                        var uvs = new Vector2[vertexCount];
                        var uvOffset = indexOffset + indexCount * 4;
                        if (hasNormals)
                            uvOffset += vertexCount * 12;
                        if (hasColors)
                            uvOffset += vertexCount * 16;
                        fixed (Vector2* dst = uvs)
                        {
                            Buffer.MemoryCopy(ptr + uvOffset, dst, vertexCount * 8,
                                vertexCount * 8);
                        }
                        mesh.uv = uvs;
                    }

                    mesh.RecalculateTangents();
                    mesh.RecalculateBounds();
                    return true;
                }
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        public static unsafe bool TryParsePointBinary(byte[] data, out List<PcgScatterPoint> points, out string error)
        {
            points = new List<PcgScatterPoint>();
            error = null;

            if (data == null || data.Length < PcgNative.PointBinaryHeaderSize)
            {
                error = "Point binary payload is too small.";
                return false;
            }

            try
            {
                fixed (byte* ptr = data)
                {
                    var magic = *(uint*)ptr;
                    if (magic != PcgNative.PointBinaryMagic)
                    {
                        error = $"Invalid point binary magic: 0x{magic:X8}";
                        return false;
                    }

                    var pointCount = *(int*)(ptr + 8);
                    var flags = (PcgPointAttrFlags)(*(uint*)(ptr + 12));
                    var required = PcgNative.PointBinaryHeaderSize + pointCount * 12;
                    if (flags.HasFlag(PcgPointAttrFlags.Normal))
                        required += pointCount * 12;
                    if (flags.HasFlag(PcgPointAttrFlags.Uv))
                        required += pointCount * 8;
                    if (flags.HasFlag(PcgPointAttrFlags.TriIndex))
                        required += pointCount * 4;
                    if (flags.HasFlag(PcgPointAttrFlags.Scale))
                        required += pointCount * 4;
                    if (flags.HasFlag(PcgPointAttrFlags.Rotation))
                        required += pointCount * 16;

                    if (data.Length < required)
                    {
                        error = $"Point binary truncated (need {required} bytes, got {data.Length}).";
                        return false;
                    }

                    var parsed = new PcgScatterPoint[pointCount];
                    var offset = PcgNative.PointBinaryHeaderSize;
                    var src = (float*)(ptr + offset);
                    for (var i = 0; i < pointCount; i++)
                    {
                        var baseIndex = i * 3;
                        parsed[i] = new PcgScatterPoint
                        {
                            Position = new Vector3(src[baseIndex], src[baseIndex + 1], src[baseIndex + 2]),
                            Normal = Vector3.up,
                            HasNormal = false,
                            Scale = 1f,
                            HasScale = false
                        };
                    }

                    offset += pointCount * 12;
                    if (flags.HasFlag(PcgPointAttrFlags.Normal))
                    {
                        var normals = (float*)(ptr + offset);
                        for (var i = 0; i < pointCount; i++)
                        {
                            var baseIndex = i * 3;
                            var normal = new Vector3(normals[baseIndex], normals[baseIndex + 1],
                                normals[baseIndex + 2]);
                            parsed[i].Normal = normal;
                            parsed[i].HasNormal = normal.sqrMagnitude > 1e-8f;
                        }

                        offset += pointCount * 12;
                    }

                    if (flags.HasFlag(PcgPointAttrFlags.Uv))
                        offset += pointCount * 8;
                    if (flags.HasFlag(PcgPointAttrFlags.TriIndex))
                        offset += pointCount * 4;
                    if (flags.HasFlag(PcgPointAttrFlags.Scale))
                    {
                        var scales = (float*)(ptr + offset);
                        for (var i = 0; i < pointCount; i++)
                        {
                            parsed[i].Scale = scales[i];
                            parsed[i].HasScale = true;
                        }

                        offset += pointCount * 4;
                    }

                    if (flags.HasFlag(PcgPointAttrFlags.Rotation))
                        offset += pointCount * 16;

                    points = new List<PcgScatterPoint>(pointCount);
                    for (var i = 0; i < pointCount; i++)
                        points.Add(parsed[i]);
                    return true;
                }
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        public static bool TryParsePoints(string resultJson, out PcgExecutionResult result, out string error)
        {
            result = null;
            error = null;

            if (string.IsNullOrWhiteSpace(resultJson))
            {
                error = "Result JSON is empty.";
                return false;
            }

            try
            {
                result = JsonUtility.FromJson<PcgExecutionResult>(resultJson);
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }

            if (result == null)
            {
                error = "Failed to deserialize result JSON.";
                return false;
            }

            if (result.points == null)
                result.points = Array.Empty<PcgPoint>();

            return true;
        }

        public static bool TryParseSplines(string resultJson, out List<List<Vector3>> splines, out string error)
        {
            splines = new List<List<Vector3>>();
            error = null;

            if (string.IsNullOrWhiteSpace(resultJson))
            {
                error = "Result JSON is empty.";
                return false;
            }

            try
            {
                var payload = JsonUtility.FromJson<PcgSplinePayload>(resultJson);
                if (payload?.splines == null)
                {
                    error = "Missing splines array.";
                    return false;
                }

                foreach (var entry in payload.splines)
                {
                    var line = new List<Vector3>();
                    if (entry?.points != null)
                    {
                        foreach (var p in entry.points)
                            line.Add(new Vector3(p.x, p.y, p.z));
                    }
                    splines.Add(line);
                }

                return splines.Count > 0;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        public static bool TryParseMesh(string resultJson, out Mesh mesh, out string error)
        {
            mesh = null;
            error = null;

            if (string.IsNullOrWhiteSpace(resultJson))
            {
                error = "Result JSON is empty.";
                return false;
            }

            try
            {
                var payload = JsonUtility.FromJson<PcgMeshPayload>(resultJson);
                if (payload?.vertices == null || payload.vertices.Length == 0)
                {
                    error = "Missing mesh vertices.";
                    return false;
                }
                if (payload.triangles == null || payload.triangles.Length < 3)
                {
                    error = "Missing mesh triangles.";
                    return false;
                }

                var maxVertex = payload.vertices.Length - 1;
                for (var i = 0; i < payload.triangles.Length; i++)
                {
                    var idx = payload.triangles[i];
                    if (idx < 0 || idx > maxVertex)
                    {
                        error = $"Invalid triangle index {idx} (vertex count {payload.vertices.Length}).";
                        return false;
                    }
                }

                var vertices = new Vector3[payload.vertices.Length];
                for (var i = 0; i < payload.vertices.Length; i++)
                {
                    var v = payload.vertices[i];
                    vertices[i] = new Vector3(v.x, v.y, v.z);
                }

                mesh = new Mesh { name = "PCG Generated Mesh" };
                if (vertices.Length > 65535)
                    mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
                mesh.vertices = vertices;
                mesh.triangles = payload.triangles;
                mesh.RecalculateNormals();
                mesh.RecalculateTangents();
                mesh.RecalculateBounds();
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        public static List<Vector3> ToVector3List(PcgExecutionResult result)
        {
            var vectors = new List<Vector3>(result.points.Length);
            foreach (var point in result.points)
                vectors.Add(new Vector3(point.x, point.y, point.z));
            return vectors;
        }

        public static List<PcgScatterPoint> ToScatterPoints(List<Vector3> points)
        {
            var scatterPoints = new List<PcgScatterPoint>(points?.Count ?? 0);
            if (points == null)
                return scatterPoints;

            foreach (var point in points)
            {
                scatterPoints.Add(new PcgScatterPoint
                {
                    Position = point,
                    Normal = Vector3.up,
                    HasNormal = false,
                    Scale = 1f,
                    HasScale = false
                });
            }

            return scatterPoints;
        }

        public const uint GeometryBinaryMagic = 0x47475043u;
        public const uint GeometryBinaryVersion = 3u;
        public const uint GeometryBinaryPreviousVersion = 2u;
        public const int GeometryBinaryHeaderSize = 16;
        private const uint GeometryChunkPoints = 1u;
        private const uint GeometryChunkFaceOffsets = 2u;
        private const uint GeometryChunkFaceIndices = 3u;

        public static bool TryParseGeometryBinary(
            byte[] data, out PcgPolygonPreviewData preview, out string error)
        {
            preview = null;
            error = null;

            if (data == null || data.Length < GeometryBinaryHeaderSize)
            {
                error = "Geometry binary payload is too small.";
                return false;
            }

            try
            {
                var offset = 0;
                var magic = ReadUInt32(data, ref offset);
                if (magic != GeometryBinaryMagic)
                {
                    error = $"Invalid geometry binary magic: 0x{magic:X8}";
                    return false;
                }

                var version = ReadUInt32(data, ref offset);
                if (version != GeometryBinaryVersion && version != GeometryBinaryPreviousVersion)
                {
                    error = $"Unsupported geometry binary version: {version}";
                    return false;
                }

                var pointCountU = ReadUInt32(data, ref offset);
                var faceCountU = ReadUInt32(data, ref offset);
                if (pointCountU > int.MaxValue || faceCountU > int.MaxValue)
                {
                    error = "Geometry binary header counts overflow.";
                    return false;
                }

                var pointCount = checked((int)pointCountU);
                var faceCount = checked((int)faceCountU);
                Vector3[] points = null;
                int[] faceOffsets = null;
                int[] faceIndices = null;
                var sawPoints = false;
                var sawOffsets = false;
                var sawIndices = false;

                while (offset + 8 <= data.Length)
                {
                    var chunkId = ReadUInt32(data, ref offset);
                    var chunkSizeU = ReadUInt32(data, ref offset);
                    if (chunkSizeU > int.MaxValue)
                    {
                        error = "Geometry chunk size overflow.";
                        return false;
                    }

                    var chunkSize = checked((int)chunkSizeU);
                    var chunkEnd = checked(offset + chunkSize);
                    if (chunkEnd < offset || chunkEnd > data.Length)
                    {
                        error = "Geometry chunk extends past buffer.";
                        return false;
                    }

                    if (chunkId == GeometryChunkPoints)
                    {
                        if (sawPoints)
                        {
                            error = "Duplicate POINTS chunk.";
                            return false;
                        }

                        var expectedBytes = checked(pointCount * 12);
                        if (chunkSize != expectedBytes)
                        {
                            error = $"POINTS chunk bytes mismatch: {chunkSize} != {expectedBytes}";
                            return false;
                        }

                        points = new Vector3[pointCount];
                        for (var i = 0; i < pointCount; i++)
                        {
                            var x = ReadFloat(data, ref offset);
                            var y = ReadFloat(data, ref offset);
                            var z = ReadFloat(data, ref offset);
                            points[i] = new Vector3(x, y, z);
                        }

                        sawPoints = true;
                    }
                    else if (chunkId == GeometryChunkFaceOffsets)
                    {
                        if (sawOffsets)
                        {
                            error = "Duplicate FACE_OFFSETS chunk.";
                            return false;
                        }

                        var expectedBytes = checked(faceCount * 4);
                        if (chunkSize != expectedBytes)
                        {
                            error = $"FACE_OFFSETS chunk bytes mismatch: {chunkSize} != {expectedBytes}";
                            return false;
                        }

                        faceOffsets = new int[faceCount];
                        for (var i = 0; i < faceCount; i++)
                        {
                            var value = ReadUInt32(data, ref offset);
                            if (value > int.MaxValue)
                            {
                                error = "FACE_OFFSETS value overflow.";
                                return false;
                            }

                            faceOffsets[i] = (int)value;
                        }

                        sawOffsets = true;
                    }
                    else if (chunkId == GeometryChunkFaceIndices)
                    {
                        if (sawIndices)
                        {
                            error = "Duplicate FACE_INDICES chunk.";
                            return false;
                        }

                        if ((chunkSize & 3) != 0)
                        {
                            error = "FACE_INDICES chunk size is not a multiple of 4.";
                            return false;
                        }

                        var indexCount = chunkSize / 4;
                        faceIndices = new int[indexCount];
                        for (var i = 0; i < indexCount; i++)
                        {
                            var value = ReadUInt32(data, ref offset);
                            if (value > int.MaxValue)
                            {
                                error = "FACE_INDICES value overflow.";
                                return false;
                            }

                            faceIndices[i] = (int)value;
                        }

                        sawIndices = true;
                    }
                    else
                    {
                        // GROUPS / TRIANGULATION / unknown — skip by size; not used for wire.
                        offset = chunkEnd;
                    }

                    if (offset != chunkEnd)
                        offset = chunkEnd;
                }

                if (!sawPoints)
                {
                    error = "Missing POINTS chunk.";
                    return false;
                }

                if (!sawOffsets)
                {
                    error = "Missing FACE_OFFSETS chunk.";
                    return false;
                }

                if (!sawIndices)
                {
                    error = "Missing FACE_INDICES chunk.";
                    return false;
                }

                if (faceCount > 0 && faceOffsets[0] != 0)
                {
                    error = "FACE_OFFSETS[0] must be 0.";
                    return false;
                }

                for (var i = 0; i < faceCount; i++)
                {
                    var start = faceOffsets[i];
                    var end = i + 1 < faceCount ? faceOffsets[i + 1] : faceIndices.Length;
                    if (start < 0 || end < 0 || start > end || end > faceIndices.Length)
                    {
                        error = "FACE_OFFSETS are out of range.";
                        return false;
                    }

                    if (i + 1 < faceCount && faceOffsets[i + 1] <= start)
                    {
                        error = "FACE_OFFSETS must be strictly increasing.";
                        return false;
                    }

                    if (end - start < 3)
                    {
                        error = "Each face must have at least 3 indices.";
                        return false;
                    }

                    for (var j = start; j < end; j++)
                    {
                        var idx = faceIndices[j];
                        if (idx < 0 || idx >= pointCount)
                        {
                            error = $"Face index {idx} out of range for pointCount={pointCount}.";
                            return false;
                        }
                    }
                }

                preview = new PcgPolygonPreviewData(points, faceOffsets, faceIndices);
                return true;
            }
            catch (OverflowException)
            {
                error = "Geometry binary integer overflow.";
                preview = null;
                return false;
            }
            catch (Exception ex)
            {
                error = $"Geometry binary parse failed: {ex.GetType().Name}";
                preview = null;
                return false;
            }
        }

        private static uint ReadUInt32(byte[] data, ref int offset)
        {
            if (offset < 0 || checked(offset + 4) > data.Length)
                throw new IndexOutOfRangeException();
            var value = BitConverter.ToUInt32(data, offset);
            offset += 4;
            return value;
        }

        private static float ReadFloat(byte[] data, ref int offset)
        {
            if (offset < 0 || checked(offset + 4) > data.Length)
                throw new IndexOutOfRangeException();
            var value = BitConverter.ToSingle(data, offset);
            offset += 4;
            return value;
        }
    }

    /// <summary>
    /// Cached Sink polygon topology for Scene View wire overlay (pre-triangulation).
    /// </summary>
    public sealed class PcgPolygonPreviewData
    {
        public PcgPolygonPreviewData(Vector3[] points, int[] faceOffsets, int[] faceIndices)
        {
            Points = points ?? Array.Empty<Vector3>();
            FaceOffsets = faceOffsets ?? Array.Empty<int>();
            FaceIndices = faceIndices ?? Array.Empty<int>();
        }

        public Vector3[] Points { get; }
        public int[] FaceOffsets { get; }
        public int[] FaceIndices { get; }
        public int FaceCount => FaceOffsets.Length;
    }
}
