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

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern IntPtr pcg_get_version();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_validate_graph(string json, StringBuilder errBuf, int errBufSize);

        // byte[] marshals as a writable char buffer — safer than StringBuilder for large mesh JSON on macOS.
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int pcg_execute_graph(string json, int seed, byte[] outJson, int outJsonSize);

        public const int ErrBufSize = 1024;
        // Bevel mesh JSON can exceed 64 KB (segments=8 ≈ 56 KB; subdiv+bevel worst case > 200 KB).
        public const int OutBufSize = 512 * 1024;

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

        public static (PcgResultCode code, string resultJson) ExecuteGraph(string json, int seed)
        {
            var outBytes = new byte[OutBufSize];
            var rc = (PcgResultCode)pcg_execute_graph(json, seed, outBytes, outBytes.Length);
            if (rc != PcgResultCode.Ok)
                return (rc, string.Empty);

            var zero = Array.IndexOf(outBytes, (byte)0);
            var length = zero >= 0 ? zero : outBytes.Length;
            return (rc, Encoding.UTF8.GetString(outBytes, 0, length));
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
}
