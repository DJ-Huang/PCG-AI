#if UNITY_EDITOR
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Houdini-style HeightField mask preview: red tint draped on Terrain from the
    /// cooked <c>mask</c> layer (or a chosen named layer via Isolate Layer → mask).
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgHeightFieldMaskOverlaySceneHandles
    {
        private const int MaxOverlayResolution = 256;
        private const float MaskEpsilon = 0.001f;
        private const float HeightBias = 0.05f;

        private static Mesh s_Mesh;
        private static Material s_Material;
        private static int s_CachedGeneration = -1;
        private static int s_CachedInstanceId;
        private static string s_CachedLayer;
        private static float s_CachedOpacity = -1f;
        private static int s_CachedResX;
        private static int s_CachedResZ;

        static PcgHeightFieldMaskOverlaySceneHandles()
        {
            SceneView.duringSceneGui -= OnSceneGui;
            SceneView.duringSceneGui += OnSceneGui;
            AssemblyReloadEvents.beforeAssemblyReload -= OnBeforeAssemblyReload;
            AssemblyReloadEvents.beforeAssemblyReload += OnBeforeAssemblyReload;
        }

        private static void OnBeforeAssemblyReload()
        {
            DestroyCachedResources();
        }

        private static void OnSceneGui(SceneView sceneView)
        {
            if (Application.isPlaying || sceneView == null)
                return;

            if (Event.current == null || Event.current.type != EventType.Repaint)
                return;

            // Mask tint is an editing aid — only while Enter PCG Mode is active.
            if (!PcgCreateSplineSceneHandles.IsPcgModeActive ||
                PcgCreateSplineSceneHandles.IsExternalSceneHandleIsolation)
            {
                if (s_Mesh != null)
                    DestroyCachedResources();
                return;
            }

            var component = ResolveComponent();
            if (component == null ||
                !component.ShowMaskOverlay ||
                component.HostOutputMode != PcgHostOutputMode.Terrain)
            {
                // Leaving preview / toggling overlay off must not leave a cached tint mesh.
                if (s_Mesh != null)
                    DestroyCachedResources();
                return;
            }

            var surface = component.LastCookedHeightField;
            if (surface == null || !surface.IsValid)
            {
                if (s_Mesh != null)
                    DestroyCachedResources();
                return;
            }

            var terrain = PcgTerrainBindingTable.ResolveSelfTerrain(component.gameObject);
            if (terrain == null || terrain.terrainData == null)
                return;

            var layerName = component.MaskOverlayLayer;
            if (!surface.TryGetLayer(layerName, out var maskLayer) ||
                maskLayer == null ||
                maskLayer.TupleSize != 1 ||
                maskLayer.Values == null)
            {
                return;
            }

            EnsureMaterial();
            EnsureMesh(
                component,
                surface,
                maskLayer,
                terrain,
                component.MaskOverlayOpacity);

            if (s_Mesh == null || s_Material == null)
                return;

            s_Material.SetPass(0);
            Graphics.DrawMeshNow(s_Mesh, Matrix4x4.identity);
        }

        private static PcgGraphComponent ResolveComponent()
        {
            var active = PcgCreateSplineSceneHandles.ActivePcgModeComponent;
            if (active != null)
                return active;

            var component = Selection.activeGameObject != null
                ? Selection.activeGameObject.GetComponent<PcgGraphComponent>()
                : null;

            if (component == null)
            {
                var window = EditorWindow.focusedWindow as PcgGraphEditorWindow;
                if (window == null || !window.HasLoadedGraph)
                    return null;
                component = FindComponentForWindow(window);
            }

            return component;
        }

        private static void EnsureMaterial()
        {
            if (s_Material != null)
                return;

            var shader = Shader.Find("Hidden/Internal-Colored");
            if (shader == null)
                return;

            s_Material = new Material(shader)
            {
                hideFlags = HideFlags.HideAndDontSave,
            };
            s_Material.SetInt("_SrcBlend", (int)BlendMode.SrcAlpha);
            s_Material.SetInt("_DstBlend", (int)BlendMode.OneMinusSrcAlpha);
            s_Material.SetInt("_Cull", (int)CullMode.Off);
            s_Material.SetInt("_ZWrite", 0);
        }

        private static void EnsureMesh(
            PcgGraphComponent component,
            PcgHostTerrainSurface surface,
            PcgHostTerrainLayer maskLayer,
            Terrain terrain,
            float opacity)
        {
            var instanceId = component.GetInstanceID();
            var generation = component.LastCookedHeightFieldGeneration;
            var layerName = component.MaskOverlayLayer;
            ResolveOverlayResolution(surface.ResolutionX, surface.ResolutionZ, out var resX, out var resZ);

            if (s_Mesh != null &&
                s_CachedInstanceId == instanceId &&
                s_CachedGeneration == generation &&
                s_CachedLayer == layerName &&
                Mathf.Approximately(s_CachedOpacity, opacity) &&
                s_CachedResX == resX &&
                s_CachedResZ == resZ)
            {
                return;
            }

            if (s_Mesh == null)
            {
                s_Mesh = new Mesh
                {
                    name = "PCG HeightField Mask Overlay",
                    hideFlags = HideFlags.HideAndDontSave,
                };
                s_Mesh.MarkDynamic();
            }
            else
            {
                s_Mesh.Clear();
            }

            BuildOverlayMesh(s_Mesh, surface, maskLayer, terrain, resX, resZ, opacity);

            s_CachedInstanceId = instanceId;
            s_CachedGeneration = generation;
            s_CachedLayer = layerName;
            s_CachedOpacity = opacity;
            s_CachedResX = resX;
            s_CachedResZ = resZ;
        }

        private static void ResolveOverlayResolution(
            int sourceX,
            int sourceZ,
            out int resX,
            out int resZ)
        {
            resX = Mathf.Max(2, sourceX);
            resZ = Mathf.Max(2, sourceZ);
            var maxDim = Mathf.Max(resX, resZ);
            if (maxDim <= MaxOverlayResolution)
                return;

            var scale = MaxOverlayResolution / (float)maxDim;
            resX = Mathf.Max(2, Mathf.RoundToInt(resX * scale));
            resZ = Mathf.Max(2, Mathf.RoundToInt(resZ * scale));
        }

        private static void BuildOverlayMesh(
            Mesh mesh,
            PcgHostTerrainSurface surface,
            PcgHostTerrainLayer maskLayer,
            Terrain terrain,
            int resX,
            int resZ,
            float opacity)
        {
            var vertexCount = resX * resZ;
            var vertices = new Vector3[vertexCount];
            var colors = new Color[vertexCount];
            var data = terrain.terrainData;
            var terrainTransform = terrain.transform;

            for (var z = 0; z < resZ; z++)
            {
                var v = resZ > 1 ? z / (float)(resZ - 1) : 0.5f;
                for (var x = 0; x < resX; x++)
                {
                    var u = resX > 1 ? x / (float)(resX - 1) : 0.5f;
                    var index = z * resX + x;
                    var mask = SampleLayerBilinear(surface, maskLayer, u, v);
                    var alpha = Mathf.Clamp01(mask) * opacity;

                    // Parametric UV matches Host Terrain export: HF (u,v) → Terrain UV.
                    var terrainLocal = new Vector3(
                        u * data.size.x,
                        0f,
                        v * data.size.z);
                    var world = terrainTransform.TransformPoint(terrainLocal);
                    world.y = terrain.SampleHeight(world) + HeightBias;
                    vertices[index] = world;
                    colors[index] = new Color(1f, 0f, 0f, alpha);
                }
            }

            var quadCount = (resX - 1) * (resZ - 1);
            var triangles = new int[quadCount * 6];
            var tri = 0;
            for (var z = 0; z < resZ - 1; z++)
            {
                for (var x = 0; x < resX - 1; x++)
                {
                    var i0 = z * resX + x;
                    var i1 = i0 + 1;
                    var i2 = i0 + resX;
                    var i3 = i2 + 1;

                    var a0 = colors[i0].a;
                    var a1 = colors[i1].a;
                    var a2 = colors[i2].a;
                    var a3 = colors[i3].a;
                    if (a0 <= MaskEpsilon && a1 <= MaskEpsilon &&
                        a2 <= MaskEpsilon && a3 <= MaskEpsilon)
                    {
                        continue;
                    }

                    triangles[tri++] = i0;
                    triangles[tri++] = i2;
                    triangles[tri++] = i1;
                    triangles[tri++] = i1;
                    triangles[tri++] = i2;
                    triangles[tri++] = i3;
                }
            }

            if (tri < triangles.Length)
                System.Array.Resize(ref triangles, tri);

            mesh.indexFormat = vertexCount > 65535
                ? IndexFormat.UInt32
                : IndexFormat.UInt16;
            mesh.vertices = vertices;
            mesh.colors = colors;
            mesh.triangles = triangles;
            mesh.RecalculateBounds();
        }

        private static float SampleLayerBilinear(
            PcgHostTerrainSurface surface,
            PcgHostTerrainLayer layer,
            float u,
            float v)
        {
            var gx = Mathf.Clamp01(u) * (surface.ResolutionX - 1);
            var gz = Mathf.Clamp01(v) * (surface.ResolutionZ - 1);
            var x0 = Mathf.FloorToInt(gx);
            var z0 = Mathf.FloorToInt(gz);
            var x1 = Mathf.Min(x0 + 1, surface.ResolutionX - 1);
            var z1 = Mathf.Min(z0 + 1, surface.ResolutionZ - 1);
            var tx = gx - x0;
            var tz = gz - z0;
            var a = layer.Values[z0 * surface.ResolutionX + x0];
            var b = layer.Values[z0 * surface.ResolutionX + x1];
            var c = layer.Values[z1 * surface.ResolutionX + x0];
            var d = layer.Values[z1 * surface.ResolutionX + x1];
            return Mathf.Lerp(Mathf.Lerp(a, b, tx), Mathf.Lerp(c, d, tx), tz);
        }

        internal static void DestroyCachedResources()
        {
            if (s_Mesh != null)
            {
                Object.DestroyImmediate(s_Mesh);
                s_Mesh = null;
            }

            if (s_Material != null)
            {
                Object.DestroyImmediate(s_Material);
                s_Material = null;
            }

            s_CachedGeneration = -1;
            s_CachedInstanceId = 0;
            s_CachedLayer = null;
            s_CachedOpacity = -1f;
        }

        private static PcgGraphComponent FindComponentForWindow(PcgGraphEditorWindow window)
        {
            var assetPath = window.CurrentAssetPath;
            var assetGuid = window.selectedGuid;
            foreach (var component in Object.FindObjectsOfType<PcgGraphComponent>())
            {
                if (component == null || component.GraphAsset == null)
                    continue;
                var path = AssetDatabase.GetAssetPath(component.GraphAsset);
                if (path == assetPath || AssetDatabase.AssetPathToGUID(path) == assetGuid)
                    return component;
            }

            return null;
        }
    }
}
#endif
