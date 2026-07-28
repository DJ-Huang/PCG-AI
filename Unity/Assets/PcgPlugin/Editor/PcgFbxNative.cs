using System.IO;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// FBX export via localhost pcg-server (no PcgFbxExporter dylib/dll in Unity).
    /// </summary>
    internal static class PcgFbxNative
    {
        public static string Version
        {
            get
            {
                var (ok, _, _) = PcgCookClient.TryHealthCheck();
                if (!ok)
                    return "unavailable";
                // Prefer last export header; fall back to health fbx_version if present.
                return s_LastFbxVersion ?? "pcg-server";
            }
        }

        private static string s_LastFbxVersion;

        public static bool Export(
            byte[] geometryBinary,
            string outputPath,
            float scale,
            bool generateNormals,
            out string error)
        {
            error = null;
            if (!PcgCookClient.TryExportFbx(
                    geometryBinary, scale, generateNormals,
                    out var fbxBytes, out var fbxVersion, out error))
            {
                return false;
            }

            s_LastFbxVersion = fbxVersion;
            try
            {
                var dir = Path.GetDirectoryName(outputPath);
                if (!string.IsNullOrEmpty(dir))
                    Directory.CreateDirectory(dir);
                File.WriteAllBytes(outputPath, fbxBytes);
                return true;
            }
            catch (IOException ex)
            {
                error = $"Failed to write FBX '{outputPath}': {ex.Message}";
                return false;
            }
        }
    }
}
