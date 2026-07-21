using System;
using System.Buffers;
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
        public const uint MultiSpawnMagic = 0x534D4350u; // 'PCMS'
        public const int MeshBinaryHeaderSize = 16;
        public const int MeshBinaryV2HeaderSize = 20;
        public const int MeshBinaryV3HeaderSize = 24;
        public const uint MeshBinaryVersion2 = 2u;
        public const uint MeshBinaryVersion3 = 3u;
        public const uint MeshBinaryFlagHasNormals = 0x1u;
        public const uint MeshBinaryFlagHasColors  = 0x2u;
        public const uint MeshBinaryFlagHasUVs     = 0x4u;
        public const uint MeshBinaryFlagHasMaterials = 0x8u;
        public const uint PointBinaryMagic = 0x50544750u;
        public const int PointBinaryHeaderSize = 16;
        public const uint HeightFieldBinaryMagic = 0x48474350u;
        public const uint HeightFieldBinaryVersion = 1u;
        public const int HeightFieldBinaryHeaderSize = 72;

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
        // Large city / facade cooks routinely exceed the old 8MB mesh ceiling
        // (lot-city @ iterations=8 needs ~15–18MB). Rent a larger default, pass
        // the *actual* rented length to native, and grow+retry when native
        // reports "buffer too small (need N bytes...)".
        public const int OutJsonBufSize = 8 * 1024 * 1024;
        public const int OutMeshBufSize = 32 * 1024 * 1024;
        public const int OutPointsBufSize = 16 * 1024 * 1024;
        public const int OutGeometryBufSize = 16 * 1024 * 1024;
        public const int OutHeightFieldBufSize = 8 * 1024 * 1024;
        public const int OutPerfBufSize = 64 * 1024;
        public const int MaxOutBinaryBufSize = 256 * 1024 * 1024;
        private const int MaxBufferGrowRetries = 3;

        private sealed class RentedOutputBuffers : IDisposable
        {
            public byte[] Json { get; private set; }
            public byte[] Mesh { get; private set; }
            public byte[] Points { get; private set; }
            public byte[] Geometry { get; private set; }
            public byte[] HeightField { get; private set; }
            public byte[] Perf { get; private set; }

            public RentedOutputBuffers()
            {
                Json = ArrayPool<byte>.Shared.Rent(OutJsonBufSize);
                Mesh = ArrayPool<byte>.Shared.Rent(OutMeshBufSize);
                Points = ArrayPool<byte>.Shared.Rent(OutPointsBufSize);
                Geometry = ArrayPool<byte>.Shared.Rent(OutGeometryBufSize);
                HeightField = ArrayPool<byte>.Shared.Rent(OutHeightFieldBufSize);
                Perf = ArrayPool<byte>.Shared.Rent(OutPerfBufSize);
                // Pool contents are undefined. Native writers null-terminate successful
                // payloads, while these sentinels keep early-error perf reads empty.
                Json[0] = 0;
                Mesh[0] = 0;
                Points[0] = 0;
                Geometry[0] = 0;
                HeightField[0] = 0;
                Perf[0] = 0;
            }

            public bool TryEnsureMesh(int requiredBytes) => TryEnsure(b => Mesh = b, Mesh, requiredBytes);
            public bool TryEnsurePoints(int requiredBytes) => TryEnsure(b => Points = b, Points, requiredBytes);
            public bool TryEnsureGeometry(int requiredBytes) => TryEnsure(b => Geometry = b, Geometry, requiredBytes);
            public bool TryEnsureJson(int requiredBytes) => TryEnsure(b => Json = b, Json, requiredBytes);
            public bool TryEnsureHeightField(int requiredBytes) =>
                TryEnsure(b => HeightField = b, HeightField, requiredBytes);

            private static bool TryEnsure(Action<byte[]> assign, byte[] current, int requiredBytes)
            {
                if (requiredBytes <= 0 || current.Length >= requiredBytes)
                    return true;
                if (requiredBytes > MaxOutBinaryBufSize)
                    return false;

                ArrayPool<byte>.Shared.Return(current);
                var next = ArrayPool<byte>.Shared.Rent(requiredBytes);
                next[0] = 0;
                assign(next);
                return true;
            }

            public void Dispose()
            {
                ArrayPool<byte>.Shared.Return(Json);
                ArrayPool<byte>.Shared.Return(Mesh);
                ArrayPool<byte>.Shared.Return(Points);
                ArrayPool<byte>.Shared.Return(Geometry);
                ArrayPool<byte>.Shared.Return(HeightField);
                ArrayPool<byte>.Shared.Return(Perf);
            }
        }

        private static bool TryParseBinaryBufferNeed(string error, out string kind, out int needBytes)
        {
            kind = null;
            needBytes = 0;
            if (string.IsNullOrEmpty(error))
                return false;

            // Native messages:
            //   "Mesh binary buffer too small (need N bytes, got M)"
            //   "Point binary buffer too small (need N bytes, got M)"
            //   "Spawn mesh binary buffer too small (need N bytes, got M)"
            const string needToken = "need ";
            const string bytesToken = " bytes";
            var needIdx = error.IndexOf(needToken, StringComparison.OrdinalIgnoreCase);
            if (needIdx < 0)
                return false;
            var numStart = needIdx + needToken.Length;
            var bytesIdx = error.IndexOf(bytesToken, numStart, StringComparison.OrdinalIgnoreCase);
            if (bytesIdx <= numStart)
                return false;
            var numberText = error.Substring(numStart, bytesIdx - numStart);
            if (!int.TryParse(numberText, out needBytes) || needBytes <= 0)
                return false;

            if (error.IndexOf("Point binary", StringComparison.OrdinalIgnoreCase) >= 0)
                kind = "points";
            else
                kind = "mesh";
            return true;
        }

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

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v8(
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
            byte[] outGeometryBuf,
            int outGeometryBufSize,
            out int outGeometryBytesWritten,
            StringBuilder errBuf,
            int errBufSize);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct NativeHeightFieldSlot
        {
            [MarshalAs(UnmanagedType.LPStr)]
            public string slot_id;
            public int resolution_x;
            public int resolution_z;
            public double size_x;
            public double size_z;
            public double center_x;
            public double center_y;
            public double center_z;
            public int sampling;
            public int orientation;
            public IntPtr height;
            public IntPtr mask;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct NativeHeightFieldSlotV10
        {
            [MarshalAs(UnmanagedType.LPStr)]
            public string slot_id;
            public int resolution_x;
            public int resolution_z;
            public double size_x;
            public double size_z;
            public double center_x;
            public double center_y;
            public double center_z;
            public int sampling;
            public int orientation;
            public int height_count;
            public int mask_count;
            public IntPtr height;
            public IntPtr mask;
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v9(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            NativeMeshSlot[] meshes,
            int mesh_count,
            NativeSplineSlot[] splines,
            int spline_count,
            NativeHeightFieldSlot[] heightfields,
            int heightfield_count,
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
            byte[] outGeometryBuf,
            int outGeometryBufSize,
            out int outGeometryBytesWritten,
            byte[] outHeightFieldBuf,
            int outHeightFieldBufSize,
            out int outHeightFieldBytesWritten,
            StringBuilder errBuf,
            int errBufSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph_v10(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            int texture_count,
            NativeMeshSlot[] meshes,
            int mesh_count,
            NativeSplineSlot[] splines,
            int spline_count,
            NativeHeightFieldSlotV10[] heightfields,
            int heightfield_count,
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
            byte[] outGeometryBuf,
            int outGeometryBufSize,
            out int outGeometryBytesWritten,
            byte[] outHeightFieldBuf,
            int outHeightFieldBufSize,
            out int outHeightFieldBytesWritten,
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

        private static bool TryValidateHeightFieldUploads(
            IReadOnlyList<PcgHeightFieldUpload> heightfields,
            out string error)
        {
            error = null;
            if (heightfields == null)
                return true;

            for (var i = 0; i < heightfields.Count; i++)
            {
                var upload = heightfields[i];
                if (upload == null)
                {
                    error = $"HeightField upload {i} is null.";
                    return false;
                }
                if (upload.ResolutionX < 2 || upload.ResolutionZ < 2)
                {
                    error = $"HeightField upload {i} has an invalid resolution.";
                    return false;
                }

                int sampleCount;
                try
                {
                    sampleCount = checked(upload.ResolutionX * upload.ResolutionZ);
                }
                catch (OverflowException)
                {
                    error = $"HeightField upload {i} resolution exceeds the supported sample count.";
                    return false;
                }

                if (upload.Heights == null || upload.Heights.Length != sampleCount)
                {
                    error = $"HeightField upload {i} requires exactly {sampleCount} height samples.";
                    return false;
                }
                if (upload.Mask != null && upload.Mask.Length != sampleCount)
                {
                    error = $"HeightField upload {i} requires exactly {sampleCount} mask samples when a mask is provided.";
                    return false;
                }
            }
            return true;
        }

        private static int ExecuteGraphNative(
            string json,
            int seed,
            NativeTextureSlot[] textures,
            NativeMeshSlot[] meshes,
            NativeSplineSlot[] splines,
            NativeHeightFieldSlot[] heightfieldsV9,
            NativeHeightFieldSlotV10[] heightfieldsV10,
            out int outKind,
            byte[] outJson,
            int outJsonBufSize,
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
            byte[] outGeometryBuf,
            int outGeometryBufSize,
            out int outGeometryBytesWritten,
            byte[] outHeightFieldBuf,
            int outHeightFieldBufSize,
            out int outHeightFieldBytesWritten,
            StringBuilder errBuf)
        {
            try
            {
                return pcg_execute_graph_v10(
                    json, seed,
                    textures, textures?.Length ?? 0,
                    meshes, meshes?.Length ?? 0,
                    splines, splines?.Length ?? 0,
                    heightfieldsV10, heightfieldsV10?.Length ?? 0,
                    out outKind,
                    outJson, outJsonBufSize,
                    outMeshBuf, outMeshBufSize,
                    outPointsBuf, outPointsBufSize,
                    out outPointCount, out outPointAttrFlags,
                    out outVertexCount, out outIndexCount,
                    out outStats,
                    outPerfJson, OutPerfBufSize,
                    outGeometryBuf, outGeometryBufSize, out outGeometryBytesWritten,
                    outHeightFieldBuf, outHeightFieldBufSize, out outHeightFieldBytesWritten,
                    errBuf, ErrBufSize);
            }
            catch (EntryPointNotFoundException)
            {
                // Managed length validation makes the legacy v9 fallback safe for
                // platforms whose bundled native plugin has not been upgraded yet.
                return pcg_execute_graph_v9(
                    json, seed,
                    textures, textures?.Length ?? 0,
                    meshes, meshes?.Length ?? 0,
                    splines, splines?.Length ?? 0,
                    heightfieldsV9, heightfieldsV9?.Length ?? 0,
                    out outKind,
                    outJson, outJsonBufSize,
                    outMeshBuf, outMeshBufSize,
                    outPointsBuf, outPointsBufSize,
                    out outPointCount, out outPointAttrFlags,
                    out outVertexCount, out outIndexCount,
                    out outStats,
                    outPerfJson, OutPerfBufSize,
                    outGeometryBuf, outGeometryBufSize, out outGeometryBytesWritten,
                    outHeightFieldBuf, outHeightFieldBufSize, out outHeightFieldBytesWritten,
                    errBuf, ErrBufSize);
            }
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
            return ExecuteGraph(json, seed, textures, meshes, splines, null);
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines,
            IReadOnlyList<PcgHeightFieldUpload> heightfields)
        {
            if (!TryValidateHeightFieldUploads(heightfields, out var heightFieldError))
            {
                return (PcgResultCode.InvalidArgument, new PcgGraphExecuteResult
                {
                    Error = heightFieldError,
                });
            }

            ClearCancel();
            var errBuf = new StringBuilder(ErrBufSize);
            using var outputBuffers = new RentedOutputBuffers();
            var jsonBuf = outputBuffers.Json;
            var meshBuf = outputBuffers.Mesh;
            var pointsBuf = outputBuffers.Points;
            var geometryBuf = outputBuffers.Geometry;
            var heightfieldBuf = outputBuffers.HeightField;
            var perfBuf = outputBuffers.Perf;

            var hasTextures = textures != null && textures.Count > 0;
            var hasMeshes = meshes != null && meshes.Count > 0;
            var hasSplines = splines != null && splines.Count > 0;
            var hasHeightFields = heightfields != null && heightfields.Count > 0;

            var textureHandles = new List<GCHandle>();
            var meshHandles = new List<GCHandle>();
            var splineHandles = new List<GCHandle>();
            var heightfieldHandles = new List<GCHandle>();
            var nativeSw = System.Diagnostics.Stopwatch.StartNew();

            // Definite assignment: try may throw before the cook loop assigns these.
            PcgResultCode rc = PcgResultCode.Execution;
            int kind = 0;
            int pointCount = 0;
            uint pointAttrFlags = 0;
            int vertexCount = 0;
            int indexCount = 0;
            int geometryBytesWritten = 0;
            int heightfieldBytesWritten = 0;
            NativeCookStats cookStats = default;

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

                NativeHeightFieldSlot[] nativeHeightFields = null;
                NativeHeightFieldSlotV10[] nativeHeightFieldsV10 = null;
                if (hasHeightFields)
                {
                    nativeHeightFields = new NativeHeightFieldSlot[heightfields.Count];
                    nativeHeightFieldsV10 = new NativeHeightFieldSlotV10[heightfields.Count];
                    for (var i = 0; i < heightfields.Count; i++)
                    {
                        var upload = heightfields[i];
                        var heightPin = GCHandle.Alloc(upload.Heights, GCHandleType.Pinned);
                        heightfieldHandles.Add(heightPin);
                        var maskPointer = IntPtr.Zero;
                        if (upload.Mask != null && upload.Mask.Length > 0)
                        {
                            var maskPin = GCHandle.Alloc(upload.Mask, GCHandleType.Pinned);
                            heightfieldHandles.Add(maskPin);
                            maskPointer = maskPin.AddrOfPinnedObject();
                        }
                        nativeHeightFields[i] = new NativeHeightFieldSlot
                        {
                            slot_id = upload.SlotId,
                            resolution_x = upload.ResolutionX,
                            resolution_z = upload.ResolutionZ,
                            size_x = upload.SizeX,
                            size_z = upload.SizeZ,
                            center_x = upload.CenterX,
                            center_y = upload.CenterY,
                            center_z = upload.CenterZ,
                            sampling = upload.Sampling,
                            orientation = upload.Orientation,
                            height = heightPin.AddrOfPinnedObject(),
                            mask = maskPointer,
                        };
                        nativeHeightFieldsV10[i] = new NativeHeightFieldSlotV10
                        {
                            slot_id = upload.SlotId,
                            resolution_x = upload.ResolutionX,
                            resolution_z = upload.ResolutionZ,
                            size_x = upload.SizeX,
                            size_z = upload.SizeZ,
                            center_x = upload.CenterX,
                            center_y = upload.CenterY,
                            center_z = upload.CenterZ,
                            sampling = upload.Sampling,
                            orientation = upload.Orientation,
                            height_count = upload.Heights.Length,
                            mask_count = upload.Mask?.Length ?? 0,
                            height = heightPin.AddrOfPinnedObject(),
                            mask = maskPointer,
                        };
                    }
                }

                NativeCookStats primaryCookStats = default;
                byte[] primaryPerf = null;

                for (var attempt = 0; attempt <= MaxBufferGrowRetries; attempt++)
                {
                    jsonBuf = outputBuffers.Json;
                    meshBuf = outputBuffers.Mesh;
                    pointsBuf = outputBuffers.Points;
                    geometryBuf = outputBuffers.Geometry;
                    heightfieldBuf = outputBuffers.HeightField;
                    errBuf.Clear();

                    rc = (PcgResultCode)ExecuteGraphNative(
                        json, seed,
                        nativeTextures, nativeMeshes, nativeSplines,
                        nativeHeightFields, nativeHeightFieldsV10,
                        out kind,
                        jsonBuf, jsonBuf.Length,
                        meshBuf, meshBuf.Length,
                        pointsBuf, pointsBuf.Length,
                        out pointCount,
                        out pointAttrFlags,
                        out vertexCount,
                        out indexCount,
                        out cookStats,
                        perfBuf,
                        geometryBuf, geometryBuf.Length,
                        out geometryBytesWritten,
                        heightfieldBuf,
                        heightfieldBuf.Length,
                        out heightfieldBytesWritten,
                        errBuf);

                    if (rc == PcgResultCode.Ok &&
                        heightfieldBytesWritten > heightfieldBuf.Length)
                    {
                        if (primaryPerf == null)
                        {
                            primaryCookStats = cookStats;
                            primaryPerf = new byte[OutPerfBufSize];
                            Buffer.BlockCopy(perfBuf, 0, primaryPerf, 0, primaryPerf.Length);
                        }

                        if (!outputBuffers.TryEnsureHeightField(heightfieldBytesWritten))
                            break;

                        continue;
                    }

                    if (rc == PcgResultCode.Ok)
                        break;

                    var errorText = errBuf.ToString();
                    if (!TryParseBinaryBufferNeed(errorText, out var bufferKind, out var needBytes))
                        break;
                    if (attempt == MaxBufferGrowRetries)
                        break;

                    var grew = bufferKind == "points"
                        ? outputBuffers.TryEnsurePoints(needBytes)
                        : outputBuffers.TryEnsureMesh(needBytes);
                    if (!grew)
                        break;
                }

                if (primaryPerf != null && rc == PcgResultCode.Ok)
                {
                    // Heightfield transport recovery: report the first successful
                    // cook's node timings (retry is serialize-capacity only).
                    cookStats = primaryCookStats;
                    Buffer.BlockCopy(primaryPerf, 0, perfBuf, 0, primaryPerf.Length);
                }

                // Refresh locals in case grow replaced pooled arrays.
                jsonBuf = outputBuffers.Json;
                meshBuf = outputBuffers.Mesh;
                pointsBuf = outputBuffers.Points;
                geometryBuf = outputBuffers.Geometry;
                heightfieldBuf = outputBuffers.HeightField;
            }
            finally
            {
                foreach (var handle in textureHandles)
                    handle.Free();
                foreach (var handle in meshHandles)
                    handle.Free();
                foreach (var handle in splineHandles)
                    handle.Free();
                foreach (var handle in heightfieldHandles)
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

#if UNITY_EDITOR
            if (geometryBytesWritten <= 0 && kind == (int)PcgExecuteKind.Mesh && vertexCount > 0)
            {
                Debug.Log(
                    $"[PcgNative] Mesh cook geometry_bytes=0 (verts={vertexCount}, idx={indexCount}, " +
                    $"previewSink={(json != null && json.Contains("__pcg_preview_sink__"))}).");
            }
#endif

            return BuildSuccessResult(
                rc,
                (PcgExecuteKind)kind,
                jsonBuf,
                meshBuf,
                pointsBuf,
                geometryBuf,
                geometryBytesWritten,
                heightfieldBuf,
                heightfieldBytesWritten,
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

        /// <summary>
        /// Computes the actual byte size of a mesh binary payload by reading
        /// the version/flags header. Used by both Mesh and Points(spawnMesh) paths.
        /// </summary>
        private static int ComputeMeshBinaryPayloadSize(byte[] meshBuf, int vertexCount, int indexCount)
        {
            if (meshBuf != null && meshBuf.Length >= 12 &&
                BitConverter.ToUInt32(meshBuf, 0) == MultiSpawnMagic)
                return ComputeMultiSpawnPayloadSize(meshBuf);

            int headerSize = MeshBinaryHeaderSize;
            int normalSize = 0;
            int colorSize = 0;
            int uvSize = 0;
            int materialSize = 0;

            if (meshBuf.Length >= MeshBinaryV2HeaderSize)
            {
                var version = BitConverter.ToUInt32(meshBuf, 4);
                if (version == MeshBinaryVersion2 || version == MeshBinaryVersion3)
                {
                    headerSize = version == MeshBinaryVersion3
                        ? MeshBinaryV3HeaderSize
                        : MeshBinaryV2HeaderSize;
                    var flags = BitConverter.ToUInt32(meshBuf, 16);
                    if ((flags & MeshBinaryFlagHasNormals) != 0)
                        normalSize = vertexCount * 12;
                    if ((flags & MeshBinaryFlagHasColors) != 0)
                        colorSize = vertexCount * 16;
                    if ((flags & MeshBinaryFlagHasUVs) != 0)
                        uvSize = vertexCount * 8;
                    if (version == MeshBinaryVersion3)
                        materialSize = BitConverter.ToInt32(meshBuf, 20);
                }
            }

            return headerSize + vertexCount * 12 + indexCount * 4 + normalSize + colorSize + uvSize + materialSize;
        }

        private static int ComputeMultiSpawnPayloadSize(byte[] meshBuf)
        {
            var count = BitConverter.ToInt32(meshBuf, 8);
            if (count <= 0)
                return 12;
            var header = 12 + count * 8;
            if (meshBuf.Length < header)
                return meshBuf.Length;
            var total = header;
            var sizeOffset = 12 + count * 4;
            for (var i = 0; i < count; i++)
                total += BitConverter.ToInt32(meshBuf, sizeOffset + i * 4);
            return total;
        }

        private static (PcgResultCode code, PcgGraphExecuteResult result) BuildSuccessResult(
            PcgResultCode rc,
            PcgExecuteKind executeKind,
            byte[] jsonBuf,
            byte[] meshBuf,
            byte[] pointsBuf,
            byte[] geometryBuf,
            int geometryBytesWritten,
            byte[] heightfieldBuf,
            int heightfieldBytesWritten,
            int pointCount,
            uint pointAttrFlags,
            int vertexCount,
            int indexCount,
            NativeCookStats cookStats,
            PcgCookPerfReport perf)
        {
            var copySw = System.Diagnostics.Stopwatch.StartNew();
            PcgGraphExecuteResult result;
            byte[] geometryBinary = null;
            if (geometryBytesWritten > 0)
            {
                if (geometryBuf != null && geometryBytesWritten <= geometryBuf.Length)
                {
                    geometryBinary = new byte[geometryBytesWritten];
                    Buffer.BlockCopy(geometryBuf, 0, geometryBinary, 0, geometryBytesWritten);
                }
                else
                {
                    Debug.LogWarning(
                        $"[PcgNative] Ignoring invalid geometryBytesWritten={geometryBytesWritten} (bufferLength={geometryBuf?.Length ?? 0})");
                }
            }
            byte[] heightfieldBinary = null;
            if (heightfieldBytesWritten > 0 && heightfieldBuf != null &&
                heightfieldBytesWritten <= heightfieldBuf.Length)
            {
                heightfieldBinary = new byte[heightfieldBytesWritten];
                Buffer.BlockCopy(heightfieldBuf, 0, heightfieldBinary, 0, heightfieldBytesWritten);
            }

            if (executeKind == PcgExecuteKind.Mesh)
            {
                var required = ComputeMeshBinaryPayloadSize(meshBuf, vertexCount, indexCount);
                var meshBinary = new byte[required];
                Buffer.BlockCopy(meshBuf, 0, meshBinary, 0, required);
                result = new PcgGraphExecuteResult
                {
                    Kind = executeKind,
                    MeshBinary = meshBinary,
                    GeometryBinary = geometryBinary,
                    HeightFieldBinary = heightfieldBinary,
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
                    var meshRequired = ComputeMeshBinaryPayloadSize(meshBuf, vertexCount, indexCount);
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
                    GeometryBinary = geometryBinary,
                    HeightFieldBinary = heightfieldBinary,
                    VertexCount = vertexCount,
                    IndexCount = indexCount,
                    Json = ReadNullTerminatedUtf8(jsonBuf),
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
                    GeometryBinary = geometryBinary,
                    HeightFieldBinary = heightfieldBinary,
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
        InvalidArgument = 5,
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
        public byte[] GeometryBinary;
        public byte[] HeightFieldBinary;
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
