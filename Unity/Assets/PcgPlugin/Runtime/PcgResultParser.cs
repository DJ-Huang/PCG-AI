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

            return DetectKind(result.Json);
        }

        public static bool TryParseMeshBinary(byte[] data, out Mesh mesh, out string error)
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
                var magic = BitConverter.ToUInt32(data, 0);
                if (magic != PcgNative.MeshBinaryMagic)
                {
                    error = $"Invalid mesh binary magic: 0x{magic:X8}";
                    return false;
                }

                var vertexCount = BitConverter.ToInt32(data, 8);
                var indexCount = BitConverter.ToInt32(data, 12);
                var required = PcgNative.MeshBinaryHeaderSize + vertexCount * 12 + indexCount * 4;
                if (data.Length < required)
                {
                    error = $"Mesh binary truncated (need {required} bytes, got {data.Length}).";
                    return false;
                }

                var vertices = new Vector3[vertexCount];
                var offset = PcgNative.MeshBinaryHeaderSize;
                for (var i = 0; i < vertexCount; i++)
                {
                    var x = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    var y = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    var z = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    vertices[i] = new Vector3(x, y, z);
                }

                var triangles = new int[indexCount];
                for (var i = 0; i < indexCount; i++)
                {
                    triangles[i] = (int)BitConverter.ToUInt32(data, offset);
                    offset += 4;
                }

                mesh = new Mesh { name = "PCG Generated Mesh" };
                if (vertices.Length > 65535)
                    mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
                mesh.vertices = vertices;
                mesh.triangles = triangles;
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
    }
}
