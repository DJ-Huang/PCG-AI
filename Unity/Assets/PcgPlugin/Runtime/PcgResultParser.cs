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

            return DetectKind(result.Json);
        }

        public static unsafe bool TryParseMeshBinary(byte[] data, out Mesh mesh, out string error)
        {
            mesh = null;
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

                    if (version == PcgNative.MeshBinaryVersion2)
                    {
                        headerSize = PcgNative.MeshBinaryV2HeaderSize;
                        if (data.Length < headerSize)
                        {
                            error = "Mesh binary v2 header truncated.";
                            return false;
                        }
                        var flags = *(uint*)(ptr + 16);
                        hasNormals = (flags & PcgNative.MeshBinaryFlagHasNormals) != 0;
                        hasColors = (flags & PcgNative.MeshBinaryFlagHasColors) != 0;
                        hasUVs = (flags & PcgNative.MeshBinaryFlagHasUVs) != 0;
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
                    mesh.triangles = triangles;

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
    }
}
