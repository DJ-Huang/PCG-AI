using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Draws active scatter GPU instancers during URP camera rendering.
    /// </summary>
    internal static class PcgScatterRenderBridge
    {
        private static readonly HashSet<PcgGraphComponent> s_Components = new();
        private static bool s_Hooked;

        public static void Register(PcgGraphComponent component)
        {
            if (component == null)
                return;

            s_Components.Add(component);
            EnsureHooked();
        }

        public static void Unregister(PcgGraphComponent component)
        {
            if (component == null)
                return;

            s_Components.Remove(component);
            if (s_Components.Count == 0)
                Unhook();
        }

        private static void EnsureHooked()
        {
            if (s_Hooked)
                return;

            RenderPipelineManager.beginCameraRendering += OnBeginCameraRendering;
            s_Hooked = true;
        }

        private static void Unhook()
        {
            if (!s_Hooked)
                return;

            RenderPipelineManager.beginCameraRendering -= OnBeginCameraRendering;
            s_Hooked = false;
        }

        private static void OnBeginCameraRendering(ScriptableRenderContext context, Camera camera)
        {
            if (camera == null)
                return;

            foreach (var component in s_Components)
            {
                if (component == null || !component.isActiveAndEnabled)
                    continue;

                component.DrawScatterGpu(camera);
            }
        }
    }
}
