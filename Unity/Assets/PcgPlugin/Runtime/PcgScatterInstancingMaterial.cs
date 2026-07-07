using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Builds a URP Lit scatter draw material with procedural GPU buffer instancing.
    /// Clones source material properties so visuals match the component's URP Lit material.
    /// </summary>
    internal static class PcgScatterInstancingMaterial
    {
        private const string ShaderName = "DJTech/PCG/Scatter Instanced Lit URP";

        private static Shader s_Shader;

        public static Material CreateFromSource(Material source)
        {
            var shader = ResolveShader();
            if (shader == null)
                return null;

            Material material;
            if (source != null)
            {
                material = new Material(source) { shader = shader, enableInstancing = true };
            }
            else
            {
                material = new Material(shader) { enableInstancing = true };
            }

            return material;
        }

        private static Shader ResolveShader()
        {
            if (s_Shader != null)
                return s_Shader;

            s_Shader = Shader.Find(ShaderName);
            if (s_Shader == null)
                Debug.LogError($"[PCG] Scatter instancing shader not found: {ShaderName}");

            return s_Shader;
        }
    }
}
