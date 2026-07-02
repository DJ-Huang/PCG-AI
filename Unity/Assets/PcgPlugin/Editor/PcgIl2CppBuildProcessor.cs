using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Injects PcgCore.lib into IL2CPP link.exe via --linker-flags.
    /// Unity 2022+ ignores --additional-libraries for Windows IL2CPP.
    /// Copies lib to a space-free temp path because il2cpp splits linker-flags on spaces.
    /// </summary>
    public sealed class PcgIl2CppBuildProcessor : IPreprocessBuildWithReport, IPostprocessBuildWithReport
    {
        private const string LibFileName = "PcgCore.lib";
        private const string TempLinkDirName = "PcgCoreIl2CppLink";
        private static readonly Regex LinkerFlagPattern = new(
            @"--linker-flags=""[^""]*" + LibFileName + @"[^""]*""\s*",
            RegexOptions.Compiled);

        public int callbackOrder => 0;

        public void OnPreprocessBuild(BuildReport report)
        {
            if (report.summary.platform != BuildTarget.StandaloneWindows64)
                return;

            if (PlayerSettings.GetScriptingBackend(BuildTargetGroup.Standalone) != ScriptingImplementation.IL2CPP)
                return;

            var sourceLib = Path.GetFullPath(Path.Combine(
                Application.dataPath, "PcgPlugin", "Plugins", "x86_64", LibFileName));

            if (!File.Exists(sourceLib))
            {
                throw new BuildFailedException(
                    $"[PCG] {LibFileName} not found at:\n{sourceLib}\n" +
                    "Run scripts/build-pcg-core.ps1 -CopyToUnity from repo root.");
            }

            var linkLib = PrepareTempLibCopy(sourceLib);
            var existing = StripPcgLinkerFlags(PlayerSettings.GetAdditionalIl2CppArgs());
            var flag = $"--linker-flags=\"{linkLib}\"";
            var merged = string.IsNullOrWhiteSpace(existing) ? flag : $"{existing.TrimEnd()} {flag}";
            PlayerSettings.SetAdditionalIl2CppArgs(merged);

            Debug.Log($"[PCG] IL2CPP linker flag set: {flag}");
        }

        public void OnPostprocessBuild(BuildReport report)
        {
            if (report.summary.platform != BuildTarget.StandaloneWindows64)
                return;

            var cleaned = StripPcgLinkerFlags(PlayerSettings.GetAdditionalIl2CppArgs()).Trim();
            PlayerSettings.SetAdditionalIl2CppArgs(cleaned);
            CleanupTempLibCopy();
        }

        private static string PrepareTempLibCopy(string sourceLib)
        {
            var tempDir = Path.Combine(Path.GetTempPath(), TempLinkDirName);
            Directory.CreateDirectory(tempDir);

            var tempLib = Path.Combine(tempDir, LibFileName);
            File.Copy(sourceLib, tempLib, overwrite: true);
            return tempLib;
        }

        private static void CleanupTempLibCopy()
        {
            try
            {
                var tempLib = Path.Combine(Path.GetTempPath(), TempLinkDirName, LibFileName);
                if (File.Exists(tempLib))
                    File.Delete(tempLib);
            }
            catch (IOException ex)
            {
                Debug.LogWarning($"[PCG] Failed to delete temp link lib: {ex.Message}");
            }
        }

        private static string StripPcgLinkerFlags(string args)
        {
            if (string.IsNullOrEmpty(args))
                return string.Empty;

            return LinkerFlagPattern.Replace(args, "").Trim();
        }
    }
}
