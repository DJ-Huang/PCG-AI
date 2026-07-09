#if UNITY_EDITOR
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Drives Edit Mode preview cooks via EditorApplication.update.
    /// MonoBehaviour.Update is unreliable while the Inspector IMGUI is capturing input.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgEditModeCookScheduler
    {
        private static bool s_DeferPreviewCookOnEnable;

        static PcgEditModeCookScheduler()
        {
            EditorApplication.update += Tick;
            SceneView.duringSceneGui += OnSceneGui;
            EditorSceneManager.sceneOpening += OnSceneOpening;
            EditorSceneManager.sceneOpened += OnSceneOpened;
            PcgGraphComponent.EditorShouldDeferPreviewCookOnEnable = () => s_DeferPreviewCookOnEnable;
        }

        private static void OnSceneOpening(string path, OpenSceneMode mode)
        {
            s_DeferPreviewCookOnEnable = true;
        }

        private static void OnSceneOpened(Scene scene, OpenSceneMode mode)
        {
            EditorApplication.delayCall += () =>
            {
                s_DeferPreviewCookOnEnable = false;
                PcgGraphComponent.FlushDeferredEnablePreviewCooks();
            };
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
