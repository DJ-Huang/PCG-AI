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
#else
        private const string Lib = "__Internal";
#endif

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr pcg_get_version();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int pcg_validate_graph(string json, StringBuilder errBuf, int errBufSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern int pcg_execute_graph(string json, int seed, StringBuilder outJson, int outJsonSize);

        public const int ErrBufSize = 1024;
        public const int OutBufSize = 65536;

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
            var outBuf = new StringBuilder(OutBufSize);
            var rc = (PcgResultCode)pcg_execute_graph(json, seed, outBuf, OutBufSize);
            return (rc, outBuf.ToString());
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
