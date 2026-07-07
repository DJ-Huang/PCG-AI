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
            SceneView.duringSceneGui += OnSceneGui;
        }

        private static void Tick()
        {
            if (Application.isPlaying)
                return;

            PcgGraphComponent.TickAllEditModePreviewCooks();
        }

        private static void OnSceneGui(SceneView _)
        {
            if (Application.isPlaying)
                return;

            var evt = Event.current;
            if (evt == null || evt.type != EventType.KeyDown || evt.keyCode != KeyCode.Escape)
                return;

            if (PcgGraphComponent.CancelAllEditModeAsyncCooks())
                evt.Use();
        }
    }
}
#endif
