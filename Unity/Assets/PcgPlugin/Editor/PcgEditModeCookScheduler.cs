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
        // Initialize as deferred: on a cold project open the scheduler can be created
        // after sceneOpening has already fired, while scene component OnEnable calls are
        // still pending.
        private static bool s_DeferPreviewCookOnEnable = true;

        static PcgEditModeCookScheduler()
        {
            EditorApplication.update += Tick;
            SceneView.duringSceneGui += OnSceneGui;
            EditorSceneManager.sceneOpening += OnSceneOpening;
            EditorSceneManager.sceneOpened += OnSceneOpened;
            PcgGraphComponent.EditorShouldDeferPreviewCookOnEnable = () => s_DeferPreviewCookOnEnable;
            EditorApplication.delayCall += FinishSceneLoadDeferral;
        }

        private static void OnSceneOpening(string path, OpenSceneMode mode)
        {
            s_DeferPreviewCookOnEnable = true;
        }

        private static void OnSceneOpened(Scene scene, OpenSceneMode mode)
        {
            EditorApplication.delayCall += FinishSceneLoadDeferral;
        }

        private static void FinishSceneLoadDeferral()
        {
            if (EditorApplication.isCompiling || EditorApplication.isUpdating)
            {
                EditorApplication.delayCall += FinishSceneLoadDeferral;
                return;
            }

            s_DeferPreviewCookOnEnable = false;
            PcgGraphComponent.FlushDeferredEnablePreviewCooks();
        }

        private static void Tick()
        {
            if (Application.isPlaying || !PcgGraphComponent.HasPendingEditModePreviewCooks())
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
