using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Low-level P/Invoke bindings to PcgCore native library.
    /// Editor mode loads PcgCore.dll; IL2CPP Player uses __Internal static linking.
    /// </summary>
    public static class PcgNative
    {
#if UNITY_EDITOR
        private const string Lib = "PcgCore";
#elif UNITY_ANDROID
        private const string Lib = "PcgCore";
#else
        private const string Lib = "__Internal";
#endif

        public const uint MeshBinaryMagic = 0x4D474350u;
        public const int MeshBinaryHeaderSize = 16;
        public const int MeshBinaryV2HeaderSize = 20;
        public const uint MeshBinaryVersion2 = 2u;
        public const uint MeshBinaryFlagHasNormals = 0x1u;
        public const uint PointBinaryMagic = 0x50544750u;
        public const int PointBinaryHeaderSize = 16;

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern IntPtr pcg_get_version();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_validate_graph(string json, StringBuilder errBuf, int errBufSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v2(
            string json,
            int seed,
            out int outKind,
            byte[] outJson,
            int outJsonSize,
            byte[] outMeshBuf,
            int outMeshBufSize,
            out int outVertexCount,
            out int outIndexCount,
            StringBuilder errBuf,
            int errBufSize);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct NativeTextureSlot
        {
            [MarshalAs(UnmanagedType.LPStr)]
            public string slot_id;
            public int width;
            public int height;
            public IntPtr rgba;
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v3(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            out int outKind,
            byte[] outJson,
            int outJsonSize,
            byte[] outMeshBuf,
            int outMeshBufSize,
            out int outVertexCount,
            out int outIndexCount,
            StringBuilder errBuf,
            int errBufSize);

        public const int ErrBufSize = 1024;
        // Large point/spline payloads can exceed 256KB in full-quality runs.
        // Keep a larger static buffer to avoid false execution failures.
        public const int OutJsonBufSize = 8 * 1024 * 1024;
        public const int OutMeshBufSize = 8 * 1024 * 1024;
        public const int OutPointsBufSize = 8 * 1024 * 1024;
        public const int OutPerfBufSize = 64 * 1024;

        public static string GetVersion()
        {
            return Marshal.PtrToStringAnsi(pcg_get_version());
        }

        public static (PcgResultCode code, string error) ValidateGraph(string json)
        {
            var errBuf = new StringBuilder(ErrBufSize);
            var rc = (PcgResultCode)pcg_validate_graph(json, errBuf, ErrBufSize);
            return (rc, errBuf.ToString());
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(string json, int seed)
        {
            return ExecuteGraph(json, seed, null);
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v4(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            NativeMeshSlot[] meshes,
            int mesh_count,
            out int outKind,
            byte[] outJson,
            int outJsonSize,
            byte[] outMeshBuf,
            int outMeshBufSize,
            out int outVertexCount,
            out int outIndexCount,
            StringBuilder errBuf,
            int errBufSize);

        [StructLayout(LayoutKind.Sequential)]
        private struct NativeCookStats
        {
            public int nodes_executed;
            public int nodes_skipped;
            public double graph_execute_ms;
            public double binary_write_ms;
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v5(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            NativeMeshSlot[] meshes,
            int mesh_count,
            out int outKind,
            byte[] outJson,
            int outJsonSize,
            byte[] outMeshBuf,
            int outMeshBufSize,
            out int outVertexCount,
            out int outIndexCount,
            out NativeCookStats outStats,
            StringBuilder errBuf,
            int errBufSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v6(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            NativeMeshSlot[] meshes,
            int mesh_count,
            out int outKind,
            byte[] outJson,
            int outJsonSize,
            byte[] outMeshBuf,
            int outMeshBufSize,
            byte[] outPointsBuf,
            int outPointsBufSize,
            out int outPointCount,
            out uint outPointAttrFlags,
            out int outVertexCount,
            out int outIndexCount,
            out NativeCookStats outStats,
            byte[] outPerfJson,
            int outPerfJsonSize,
            StringBuilder errBuf,
            int errBufSize);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct NativeSplineSlot
        {
            [MarshalAs(UnmanagedType.LPStr)]
            public string slot_id;
            public int spline_count;
            public IntPtr spline_point_counts;
            public IntPtr positions;
            public IntPtr closed;
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v7(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            NativeMeshSlot[] meshes,
            int mesh_count,
            NativeSplineSlot[] splines,
            int spline_count,
            out int outKind,
            byte[] outJson,
            int outJsonSize,
            byte[] outMeshBuf,
            int outMeshBufSize,
            byte[] outPointsBuf,
            int outPointsBufSize,
            out int outPointCount,
            out uint outPointAttrFlags,
            out int outVertexCount,
            out int outIndexCount,
            out NativeCookStats outStats,
            byte[] outPerfJson,
            int outPerfJsonSize,
            StringBuilder errBuf,
            int errBufSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void pcg_cook_cache_clear();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void pcg_request_cancel();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern void pcg_clear_cancel();

        public static void ClearCookCache() => pcg_cook_cache_clear();
        public static void RequestCancel()
        {
            try
            {
                pcg_request_cancel();
            }
            catch (EntryPointNotFoundException)
            {
                // Older native binaries may not expose cancellation APIs yet.
            }
        }

        public static void ClearCancel()
        {
            try
            {
                pcg_clear_cancel();
            }
            catch (EntryPointNotFoundException)
            {
                // Older native binaries may not expose cancellation APIs yet.
            }
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct NativeMeshSlot
        {
            [MarshalAs(UnmanagedType.LPStr)]
            public string slot_id;
            public int vertex_count;
            public int index_count;
            public IntPtr positions;
            public IntPtr indices;
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json, int seed, IReadOnlyList<PcgTextureUpload> textures)
        {
            return ExecuteGraph(json, seed, textures, null);
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json, int seed, IReadOnlyList<PcgTextureUpload> textures, IReadOnlyList<PcgMeshUpload> meshes)
        {
            return ExecuteGraph(json, seed, textures, meshes, null);
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines)
        {
            ClearCancel();
            var errBuf = new StringBuilder(ErrBufSize);
            var jsonBuf = new byte[OutJsonBufSize];
            var meshBuf = new byte[OutMeshBufSize];
            var pointsBuf = new byte[OutPointsBufSize];
            var perfBuf = new byte[OutPerfBufSize];

            var hasTextures = textures != null && textures.Count > 0;
            var hasMeshes = meshes != null && meshes.Count > 0;
            var hasSplines = splines != null && splines.Count > 0;

            var textureHandles = new List<GCHandle>();
            var meshHandles = new List<GCHandle>();
            var splineHandles = new List<GCHandle>();
            var nativeSw = System.Diagnostics.Stopwatch.StartNew();

            PcgResultCode rc;
            int kind;
            int pointCount;
            uint pointAttrFlags;
            int vertexCount;
            int indexCount;
            NativeCookStats cookStats;

            try
            {
                NativeTextureSlot[] nativeTextures = null;
                if (hasTextures)
                {
                    nativeTextures = new NativeTextureSlot[textures.Count];
                    for (var i = 0; i < textures.Count; i++)
                    {
                        var upload = textures[i];
                        var pin = GCHandle.Alloc(upload.Rgba, GCHandleType.Pinned);
                        textureHandles.Add(pin);
                        nativeTextures[i] = new NativeTextureSlot
                        {
                            slot_id = upload.SlotId,
                            width = upload.Width,
                            height = upload.Height,
                            rgba = pin.AddrOfPinnedObject(),
                        };
                    }
                }

                NativeMeshSlot[] nativeMeshes = null;
                if (hasMeshes)
                {
                    nativeMeshes = new NativeMeshSlot[meshes.Count];
                    for (var i = 0; i < meshes.Count; i++)
                    {
                        var upload = meshes[i];
                        var posPin = GCHandle.Alloc(upload.Positions, GCHandleType.Pinned);
                        var idxPin = GCHandle.Alloc(upload.Indices, GCHandleType.Pinned);
                        meshHandles.Add(posPin);
                        meshHandles.Add(idxPin);
                        nativeMeshes[i] = new NativeMeshSlot
                        {
                            slot_id = upload.SlotId,
                            vertex_count = upload.VertexCount,
                            index_count = upload.IndexCount,
                            positions = posPin.AddrOfPinnedObject(),
                            indices = idxPin.AddrOfPinnedObject(),
                        };
                    }
                }

                NativeSplineSlot[] nativeSplines = null;
                if (hasSplines)
                {
                    nativeSplines = new NativeSplineSlot[splines.Count];
                    for (var i = 0; i < splines.Count; i++)
                    {
                        var upload = splines[i];
                        var countsPin = GCHandle.Alloc(upload.SplinePointCounts, GCHandleType.Pinned);
                        var posPin = GCHandle.Alloc(upload.Positions, GCHandleType.Pinned);
                        var closedPin = GCHandle.Alloc(upload.Closed, GCHandleType.Pinned);
                        splineHandles.Add(countsPin);
                        splineHandles.Add(posPin);
                        splineHandles.Add(closedPin);
                        nativeSplines[i] = new NativeSplineSlot
                        {
                            slot_id = upload.SlotId,
                            spline_count = upload.SplineCount,
                            spline_point_counts = countsPin.AddrOfPinnedObject(),
                            positions = posPin.AddrOfPinnedObject(),
                            closed = closedPin.AddrOfPinnedObject(),
                        };
                    }
                }

                rc = (PcgResultCode)pcg_execute_graph_v7(
                    json,
                    seed,
                    nativeTextures,
                    nativeTextures?.Length ?? 0,
                    nativeMeshes,
                    nativeMeshes?.Length ?? 0,
                    nativeSplines,
                    nativeSplines?.Length ?? 0,
                    out kind,
                    jsonBuf,
                    jsonBuf.Length,
                    meshBuf,
                    meshBuf.Length,
                    pointsBuf,
                    pointsBuf.Length,
                    out pointCount,
                    out pointAttrFlags,
                    out vertexCount,
                    out indexCount,
                    out cookStats,
                    perfBuf,
                    perfBuf.Length,
                    errBuf,
                    ErrBufSize);
            }
            finally
            {
                foreach (var handle in textureHandles)
                    handle.Free();
                foreach (var handle in meshHandles)
                    handle.Free();
                foreach (var handle in splineHandles)
                    handle.Free();
            }

            nativeSw.Stop();
            var perf = BuildPerfReport(cookStats, perfBuf, nativeSw.Elapsed.TotalMilliseconds);

            if (rc != PcgResultCode.Ok)
            {
                return (rc, new PcgGraphExecuteResult
                {
                    Error = string.IsNullOrEmpty(errBuf.ToString())
                        ? rc.ToString()
                        : errBuf.ToString(),
                    Perf = perf,
                });
            }

            return BuildSuccessResult(
                rc,
                (PcgExecuteKind)kind,
                jsonBuf,
                meshBuf,
                pointsBuf,
                pointCount,
                pointAttrFlags,
                vertexCount,
                indexCount,
                cookStats,
                perf);
        }

        private static PcgCookPerfReport BuildPerfReport(
            NativeCookStats cookStats, byte[] perfBuf, double nativeCallMs)
        {
            var perfJson = ReadNullTerminatedUtf8(perfBuf);
            return new PcgCookPerfReport
            {
                NativeCallMs = nativeCallMs,
                GraphExecuteMs = cookStats.graph_execute_ms,
                BinaryWriteMs = cookStats.binary_write_ms,
                CookNodesExecuted = cookStats.nodes_executed,
                CookNodesSkipped = cookStats.nodes_skipped,
                NodeEntries = PcgCookPerfJson.TryParse(perfJson),
            };
        }

        private static (PcgResultCode code, PcgGraphExecuteResult result) BuildSuccessResult(
            PcgResultCode rc,
            PcgExecuteKind executeKind,
            byte[] jsonBuf,
            byte[] meshBuf,
            byte[] pointsBuf,
            int pointCount,
            uint pointAttrFlags,
            int vertexCount,
            int indexCount,
            NativeCookStats cookStats,
            PcgCookPerfReport perf)
        {
            var copySw = System.Diagnostics.Stopwatch.StartNew();
            PcgGraphExecuteResult result;

            if (executeKind == PcgExecuteKind.Mesh)
            {
                // Determine actual payload size from the binary's version/flags header.
                int headerSize = MeshBinaryHeaderSize;
                int normalSize = 0;
                if (meshBuf.Length >= MeshBinaryV2HeaderSize)
                {
                    var version = BitConverter.ToUInt32(meshBuf, 4);
                    if (version == MeshBinaryVersion2)
                    {
                        headerSize = MeshBinaryV2HeaderSize;
                        var flags = BitConverter.ToUInt32(meshBuf, 16);
                        if ((flags & MeshBinaryFlagHasNormals) != 0)
                            normalSize = vertexCount * 12;
                    }
                }
                var required = headerSize + vertexCount * 12 + indexCount * 4 + normalSize;
                var meshBinary = new byte[required];
                Buffer.BlockCopy(meshBuf, 0, meshBinary, 0, required);
                result = new PcgGraphExecuteResult
                {
                    Kind = executeKind,
                    MeshBinary = meshBinary,
                    Json = ReadNullTerminatedUtf8(jsonBuf),
                    VertexCount = vertexCount,
                    IndexCount = indexCount,
                    CookNodesExecuted = cookStats.nodes_executed,
                    CookNodesSkipped = cookStats.nodes_skipped,
                };
            }
            else if (executeKind == PcgExecuteKind.Points)
            {
                var required = PointBinaryHeaderSize + pointCount * 12;
                var flags = (PcgPointAttrFlags)pointAttrFlags;
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

                var pointBinary = new byte[required];
                Buffer.BlockCopy(pointsBuf, 0, pointBinary, 0, required);
                byte[] meshBinary = null;
                if (vertexCount > 0 && indexCount > 0)
                {
                    int meshHdr = MeshBinaryHeaderSize;
                    int meshNorm = 0;
                    if (meshBuf.Length >= MeshBinaryV2HeaderSize)
                    {
                        var mv = BitConverter.ToUInt32(meshBuf, 4);
                        if (mv == MeshBinaryVersion2)
                        {
                            meshHdr = MeshBinaryV2HeaderSize;
                            var mf = BitConverter.ToUInt32(meshBuf, 16);
                            if ((mf & MeshBinaryFlagHasNormals) != 0)
                                meshNorm = vertexCount * 12;
                        }
                    }
                    var meshRequired = meshHdr + vertexCount * 12 + indexCount * 4 + meshNorm;
                    meshBinary = new byte[meshRequired];
                    Buffer.BlockCopy(meshBuf, 0, meshBinary, 0, meshRequired);
                }

                result = new PcgGraphExecuteResult
                {
                    Kind = executeKind,
                    PointBinary = pointBinary,
                    PointCount = pointCount,
                    PointAttrFlags = pointAttrFlags,
                    MeshBinary = meshBinary,
                    VertexCount = vertexCount,
                    IndexCount = indexCount,
                    CookNodesExecuted = cookStats.nodes_executed,
                    CookNodesSkipped = cookStats.nodes_skipped,
                };
            }
            else
            {
                var zero = Array.IndexOf(jsonBuf, (byte)0);
                var length = zero >= 0 ? zero : jsonBuf.Length;
                result = new PcgGraphExecuteResult
                {
                    Kind = executeKind,
                    Json = Encoding.UTF8.GetString(jsonBuf, 0, length),
                    CookNodesExecuted = cookStats.nodes_executed,
                    CookNodesSkipped = cookStats.nodes_skipped,
                };
            }

            copySw.Stop();
            perf.BufferCopyMs = copySw.Elapsed.TotalMilliseconds;
            result.Perf = perf;
            return (rc, result);
        }

        private static string ReadNullTerminatedUtf8(byte[] buffer)
        {
            if (buffer == null || buffer.Length == 0 || buffer[0] == 0)
                return string.Empty;

            var zero = Array.IndexOf(buffer, (byte)0);
            var length = zero >= 0 ? zero : buffer.Length;
            return Encoding.UTF8.GetString(buffer, 0, length);
        }
    }

    public enum PcgResultCode
    {
        Ok = 0,
        InvalidJson = 1,
        CycleDetected = 2,
        UnknownNode = 3,
        Execution = 4,
    }

    public enum PcgExecuteKind
    {
        None = 0,
        Json = 1,
        Mesh = 2,
        Points = 3,
    }

    [Flags]
    public enum PcgPointAttrFlags : uint
    {
        None = 0,
        Normal = 1 << 0,
        Uv = 1 << 1,
        TriIndex = 1 << 2,
        Scale = 1 << 3,
        Rotation = 1 << 4,
    }

    public sealed class PcgGraphExecuteResult
    {
        public PcgExecuteKind Kind = PcgExecuteKind.None;
        public string Json;
        public byte[] MeshBinary;
        public byte[] PointBinary;
        public int PointCount;
        public uint PointAttrFlags;
        public int VertexCount;
        public int IndexCount;
        public string Error;
        public int CookNodesExecuted;
        public int CookNodesSkipped;
        public PcgCookPerfReport Perf;
    }
}
