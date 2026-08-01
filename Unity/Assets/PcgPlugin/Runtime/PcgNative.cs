using System;
using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Binary-format constants and HTTP cook facade.
    /// All C++ execution goes through localhost pcg-server (no DllImport / dylib).
    /// </summary>
    public static class PcgNative
    {
        public const uint MeshBinaryMagic = 0x4D474350u;
        public const uint MultiSpawnMagic = 0x534D4350u; // 'PCMS'
        public const int MeshBinaryHeaderSize = 16;
        public const int MeshBinaryV2HeaderSize = 20;
        public const int MeshBinaryV3HeaderSize = 24;
        public const uint MeshBinaryVersion2 = 2u;
        public const uint MeshBinaryVersion3 = 3u;
        public const uint MeshBinaryFlagHasNormals = 0x1u;
        public const uint MeshBinaryFlagHasColors = 0x2u;
        public const uint MeshBinaryFlagHasUVs = 0x4u;
        public const uint MeshBinaryFlagHasMaterials = 0x8u;
        public const uint PointBinaryMagic = 0x50544750u;
        public const int PointBinaryHeaderSize = 16;
        public const uint HeightFieldBinaryMagic = 0x48474350u;
        public const uint HeightFieldBinaryVersion = 1u;
        public const int HeightFieldBinaryHeaderSize = 72;

        public const int ErrBufSize = 1024;
        public const int OutJsonBufSize = 8 * 1024 * 1024;
        public const int OutMeshBufSize = 32 * 1024 * 1024;
        public const int OutPointsBufSize = 16 * 1024 * 1024;
        public const int OutGeometryBufSize = 16 * 1024 * 1024;
        public const int OutHeightFieldBufSize = 8 * 1024 * 1024;
        public const int OutPerfBufSize = 64 * 1024;
        public const int MaxOutBinaryBufSize = 256 * 1024 * 1024;

        public static string GetVersion() => PcgCookClient.GetVersion();

        public static (PcgResultCode code, string error) ValidateGraph(string json) =>
            PcgCookClient.ValidateGraph(json);

        public static void ClearCookCache() => PcgCookClient.ClearCookCache();

        public static void RequestCancel() => PcgCookClient.RequestCancel();

        public static void ClearCancel()
        {
            // Cancel flag lives in pcg-server; clear happens automatically at cook start.
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(string json, int seed) =>
            ExecuteGraph(json, seed, null, null, null, null);

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json, int seed, IReadOnlyList<PcgTextureUpload> textures) =>
            ExecuteGraph(json, seed, textures, null, null, null);

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes) =>
            ExecuteGraph(json, seed, textures, meshes, null, null);

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines) =>
            ExecuteGraph(json, seed, textures, meshes, splines, null);

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

            return PcgCookClient.ExecuteGraph(json, seed, textures, meshes, splines, heightfields);
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
