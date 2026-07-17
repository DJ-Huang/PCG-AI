using System;
using System.Runtime.InteropServices;
using System.Text;

namespace DJTechEditor.PCG
{
    internal static class PcgFbxNative
    {
        private const string Library = "PcgFbxExporter";
        private const int ErrorBufferSize = 2048;

        [StructLayout(LayoutKind.Sequential)]
        private struct ExportOptions
        {
            public uint StructSize;
            public float Scale;
            public int GenerateNormals;
        }

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        private static extern int pcg_fbx_export_v1(
            byte[] geometryBinary,
            int geometryBinarySize,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath,
            ref ExportOptions options,
            StringBuilder errorBuffer,
            int errorBufferSize);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr pcg_fbx_get_version();

        public static string Version => Marshal.PtrToStringAnsi(pcg_fbx_get_version());

        public static bool Export(
            byte[] geometryBinary,
            string outputPath,
            float scale,
            bool generateNormals,
            out string error)
        {
            error = null;
            if (geometryBinary == null || geometryBinary.Length == 0)
            {
                error = "Cook did not return polygon geometry.";
                return false;
            }

            var options = new ExportOptions
            {
                StructSize = (uint)Marshal.SizeOf<ExportOptions>(),
                Scale = scale,
                GenerateNormals = generateNormals ? 1 : 0,
            };
            var errorBuffer = new StringBuilder(ErrorBufferSize);
            try
            {
                var result = pcg_fbx_export_v1(
                    geometryBinary,
                    geometryBinary.Length,
                    outputPath,
                    ref options,
                    errorBuffer,
                    ErrorBufferSize);
                if (result == 0)
                    return true;

                error = string.IsNullOrWhiteSpace(errorBuffer.ToString())
                    ? $"Native FBX exporter failed with code {result}."
                    : errorBuffer.ToString();
                return false;
            }
            catch (DllNotFoundException)
            {
                error = "PcgFbxExporter native library is missing. Run " +
                        "scripts/build-pcg-fbx-exporter.sh --copy-to-unity.";
                return false;
            }
            catch (EntryPointNotFoundException)
            {
                error = "PcgFbxExporter has an incompatible ABI. Rebuild the native exporter.";
                return false;
            }
        }
    }
}
