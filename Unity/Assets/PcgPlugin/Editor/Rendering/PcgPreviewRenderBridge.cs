using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Rendering
{
    /// <summary>
    /// Connects the Runtime PcgPreview callback to the Editor batch renderer.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgPreviewRenderBridge
    {
        static PcgPreviewRenderBridge()
        {
            Register();

            AssemblyReloadEvents.beforeAssemblyReload -= OnBeforeAssemblyReload;
            AssemblyReloadEvents.beforeAssemblyReload += OnBeforeAssemblyReload;
            EditorApplication.playModeStateChanged -= OnPlayModeStateChanged;
            EditorApplication.playModeStateChanged += OnPlayModeStateChanged;
        }

        private static void Register()
        {
            PcgPreview.EditorDrawPreview -= DrawPreview;
            PcgPreview.EditorDrawPreview += DrawPreview;
        }

        private static bool DrawPreview(PcgPreview preview, Camera camera)
        {
            if (preview == null || camera == null || Application.isPlaying)
                return false;

            // PCG Mode owns the component's point/edge display toggles. The same
            // PcgPreview is also attached to that component, so suppress its legacy
            // callback to avoid drawing the geometry twice.
            var activeComponent = PcgCreateSplineSceneHandles.ActivePcgModeComponent;
            if (activeComponent != null && preview.gameObject == activeComponent.gameObject)
                return true;

            return PcgScenePreviewRenderer.DrawPreview(preview, camera);
        }

        private static void OnBeforeAssemblyReload()
        {
            PcgPreview.EditorDrawPreview -= DrawPreview;
            PcgScenePreviewRenderer.ReleaseAll();
        }

        private static void OnPlayModeStateChanged(PlayModeStateChange state)
        {
            if (state == PlayModeStateChange.ExitingEditMode ||
                state == PlayModeStateChange.EnteredPlayMode)
            {
                PcgScenePreviewRenderer.ReleaseAll();
            }

            if (state == PlayModeStateChange.EnteredEditMode)
                Register();
        }
    }
}
