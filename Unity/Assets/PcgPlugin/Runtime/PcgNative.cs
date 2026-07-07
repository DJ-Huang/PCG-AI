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
            ClearCancel();
            var errBuf = new StringBuilder(ErrBufSize);
            var jsonBuf = new byte[OutJsonBufSize];
            var meshBuf = new byte[OutMeshBufSize];
            var pointsBuf = new byte[OutPointsBufSize];

            PcgResultCode rc;
            int kind;
            int pointCount;
            uint pointAttrFlags;
            int vertexCount;
            int indexCount;
            NativeCookStats cookStats;

            var hasTextures = textures != null && textures.Count > 0;
            var hasMeshes = meshes != null && meshes.Count > 0;

            if (hasMeshes)
            {
                var nativeTextures = hasTextures ? new NativeTextureSlot[textures.Count] : null;
                var textureHandles = new List<GCHandle>();
                var nativeMeshes = new NativeMeshSlot[meshes.Count];
                var meshHandles = new List<GCHandle>();
                try
                {
                    if (hasTextures)
                    {
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

                    rc = (PcgResultCode)pcg_execute_graph_v6(
                        json,
                        seed,
                        nativeTextures,
                        nativeTextures?.Length ?? 0,
                        nativeMeshes,
                        nativeMeshes.Length,
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
                        errBuf,
                        ErrBufSize);
                }
                finally
                {
                    foreach (var handle in textureHandles)
                        handle.Free();
                    foreach (var handle in meshHandles)
                        handle.Free();
                }
            }
            else if (hasTextures)
            {
                var nativeSlots = new NativeTextureSlot[textures.Count];
                var handles = new List<GCHandle>(textures.Count);
                try
                {
                    for (var i = 0; i < textures.Count; i++)
                    {
                        var upload = textures[i];
                        var pin = GCHandle.Alloc(upload.Rgba, GCHandleType.Pinned);
                        handles.Add(pin);
                        nativeSlots[i] = new NativeTextureSlot
                        {
                            slot_id = upload.SlotId,
                            width = upload.Width,
                            height = upload.Height,
                            rgba = pin.AddrOfPinnedObject(),
                        };
                    }

                    rc = (PcgResultCode)pcg_execute_graph_v6(
                        json,
                        seed,
                        nativeSlots,
                        nativeSlots.Length,
                        null,
                        0,
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
                        errBuf,
                        ErrBufSize);
                }
                finally
                {
                    foreach (var handle in handles)
                        handle.Free();
                }
            }
            else
            {
                rc = (PcgResultCode)pcg_execute_graph_v6(
                    json,
                    seed,
                    null,
                    0,
                    null,
                    0,
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
                    errBuf,
                    ErrBufSize);
            }

            if (rc != PcgResultCode.Ok)
            {
                return (rc, new PcgGraphExecuteResult
                {
                    Error = string.IsNullOrEmpty(errBuf.ToString())
                        ? rc.ToString()
                        : errBuf.ToString(),
                });
            }

            var executeKind = (PcgExecuteKind)kind;
            if (executeKind == PcgExecuteKind.Mesh)
            {
                var required = MeshBinaryHeaderSize + vertexCount * 12 + indexCount * 4;
                var meshBinary = new byte[required];
                Buffer.BlockCopy(meshBuf, 0, meshBinary, 0, required);
                return (rc, new PcgGraphExecuteResult
                {
                    Kind = executeKind,
                    MeshBinary = meshBinary,
                    VertexCount = vertexCount,
                    IndexCount = indexCount,
                    CookNodesExecuted = cookStats.nodes_executed,
                    CookNodesSkipped = cookStats.nodes_skipped,
                });
            }

            if (executeKind == PcgExecuteKind.Points)
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
                return (rc, new PcgGraphExecuteResult
                {
                    Kind = executeKind,
                    PointBinary = pointBinary,
                    PointCount = pointCount,
                    PointAttrFlags = pointAttrFlags,
                    CookNodesExecuted = cookStats.nodes_executed,
                    CookNodesSkipped = cookStats.nodes_skipped,
                });
            }

            var zero = Array.IndexOf(jsonBuf, (byte)0);
            var length = zero >= 0 ? zero : jsonBuf.Length;
            return (rc, new PcgGraphExecuteResult
            {
                Kind = executeKind,
                Json = Encoding.UTF8.GetString(jsonBuf, 0, length),
                CookNodesExecuted = cookStats.nodes_executed,
                CookNodesSkipped = cookStats.nodes_skipped,
            });
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
    }
}
