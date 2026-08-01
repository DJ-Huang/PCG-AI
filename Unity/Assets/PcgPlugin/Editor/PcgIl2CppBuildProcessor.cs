using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Player builds no longer link PcgCore — cook is localhost HTTP only.
    /// </summary>
    public sealed class PcgIl2CppBuildProcessor : IPreprocessBuildWithReport
    {
        public int callbackOrder => 0;

        public void OnPreprocessBuild(BuildReport report)
        {
            Debug.Log("[PCG] No native PcgCore link step (HTTP pcg-server backend).");
        }
    }
}
