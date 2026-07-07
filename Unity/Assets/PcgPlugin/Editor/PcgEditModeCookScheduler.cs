#if UNITY_EDITOR
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Drives Edit Mode preview cooks via EditorApplication.update.
    /// MonoBehaviour.Update is unreliable while the Inspector IMGUI is capturing input.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgEditModeCookScheduler
    {
        static PcgEditModeCookScheduler()
        {
            EditorApplication.update += Tick;
        }

        private static void Tick()
        {
            if (Application.isPlaying)
                return;

            PcgGraphComponent.TickAllEditModePreviewCooks();
        }
    }
}
#endif
