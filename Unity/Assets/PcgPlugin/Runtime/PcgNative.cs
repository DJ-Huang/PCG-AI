using System;
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

        public const int ErrBufSize = 1024;
        public const int OutJsonBufSize = 256 * 1024;
        public const int OutMeshBufSize = 8 * 1024 * 1024;

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
            var errBuf = new StringBuilder(ErrBufSize);
            var jsonBuf = new byte[OutJsonBufSize];
            var meshBuf = new byte[OutMeshBufSize];

            var rc = (PcgResultCode)pcg_execute_graph_v2(
                json,
                seed,
                out var kind,
                jsonBuf,
                jsonBuf.Length,
                meshBuf,
                meshBuf.Length,
                out var vertexCount,
                out var indexCount,
                errBuf,
                ErrBufSize);

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
                });
            }

            var zero = Array.IndexOf(jsonBuf, (byte)0);
            var length = zero >= 0 ? zero : jsonBuf.Length;
            return (rc, new PcgGraphExecuteResult
            {
                Kind = executeKind,
                Json = Encoding.UTF8.GetString(jsonBuf, 0, length),
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
    }

    public sealed class PcgGraphExecuteResult
    {
        public PcgExecuteKind Kind = PcgExecuteKind.None;
        public string Json;
        public byte[] MeshBinary;
        public int VertexCount;
        public int IndexCount;
        public string Error;
    }
}
