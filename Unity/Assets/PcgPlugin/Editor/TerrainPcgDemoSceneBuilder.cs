using System.IO;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>Builds Assets/Scenes/TerrainDemo.scene with HeightField → Unity Terrain binding.</summary>
    public static class TerrainPcgDemoSceneBuilder
    {
        private const string DemoDirectory = "Assets/PCGDemo/TerrainPcgDemo";
        private const string TerrainDataPath = DemoDirectory + "/TerrainPcgDemoTerrain.asset";
        private const string PreviewMeshPath = DemoDirectory + "/TerrainPcgDemoPreviewMesh.asset";
        private const string PreviewMaterialPath = DemoDirectory + "/TerrainPcgDemoPreview.mat";
        private const string ScenePath = "Assets/Scenes/TerrainDemo.scene";
        private const string GenerateGraphPath =
            "Assets/PcgPlugin/Examples/PCGDemo/terrain-binding-demo.pcg";
        private const string HostImportGraphPath =
            "Assets/PcgPlugin/Examples/PCGDemo/terrain-host-import-demo.pcg";

        private static string PendingMarkerPath =>
            Path.Combine(
                Path.GetDirectoryName(Application.dataPath) ?? ".",
                "Temp",
                "pcg-build-terrain-demo.request");

        [InitializeOnLoadMethod]
        private static void ConsumePendingBuildRequest()
        {
            var marker = PendingMarkerPath;
            if (!File.Exists(marker))
                return;

            EditorApplication.delayCall += () =>
            {
                if (!File.Exists(marker))
                    return;
                try
                {
                    File.Delete(marker);
                }
                catch
                {
                    // Ignored: another domain reload may race the marker.
                }

                try
                {
                    Create();
                }
                catch (System.Exception ex)
                {
                    Debug.LogException(ex);
                }
            };
        }

        [MenuItem("Tools/PCG/Create Terrain PCG Demo Scene")]
        public static void Create()
        {
            EnsureFolder("Assets/PCGDemo");
            EnsureFolder(DemoDirectory);

            AssetDatabase.ImportAsset(GenerateGraphPath, ImportAssetOptions.ForceUpdate);
            AssetDatabase.ImportAsset(HostImportGraphPath, ImportAssetOptions.ForceUpdate);

            var generateGraph = LoadGraphAsset(GenerateGraphPath);
            var hostImportGraph = LoadGraphAsset(HostImportGraphPath);

            var terrainData = LoadOrCreateTerrainData();
            var previewMaterial = LoadOrCreatePreviewMaterial();

            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            CreateLighting();
            CreateCamera();
            var terrainObject = CreateTerrain(terrainData);
            var generateMesh = CookGenerateGraph(generateGraph, terrainObject);
            CreateMeshPreview(generateMesh, previewMaterial);
            CreateHostImportGraph(hostImportGraph, terrainObject);

            EditorSceneManager.MarkSceneDirty(scene);
            if (!EditorSceneManager.SaveScene(scene, ScenePath))
                throw new System.InvalidOperationException($"Failed to save {ScenePath}");

            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            Debug.Log(
                $"[PCG] Terrain binding demo ready: {ScenePath}\n" +
                "- PCG Generate Graph writes HeightField → bound Terrain (writeCookResult)\n" +
                "- PCG Host Import Graph reads Terrain → Mesh (readFromHost / GetTerrainData)\n" +
                "- PCG Mesh Preview keeps the ConvertHeightField mesh path");
        }

        private static PcgGraphAsset LoadGraphAsset(string assetPath)
        {
            AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceUpdate);
            var json = ReadProjectText(assetPath);
            if (string.IsNullOrWhiteSpace(json))
                throw new System.InvalidOperationException($"Empty or missing graph JSON: {assetPath}");

            var asset = AssetDatabase.LoadAssetAtPath<PcgGraphAsset>(assetPath);
            if (asset == null)
            {
                asset = ScriptableObject.CreateInstance<PcgGraphAsset>();
                asset.name = Path.GetFileNameWithoutExtension(assetPath);
                Debug.LogWarning(
                    $"[PCG] {assetPath} was not imported as PcgGraphAsset yet; using transient JSON asset.");
            }

            // ScriptedImporter sometimes leaves GraphJson empty in fresh Library caches;
            // always hydrate from disk so HasGraph / serializers see the document.
            asset.SetGraphJson(json);
            EditorUtility.SetDirty(asset);
            return asset;
        }

        private static string ReadProjectText(string assetPath)
        {
            var fullPath = Path.GetFullPath(Path.Combine(Application.dataPath, "..", assetPath));
            return File.Exists(fullPath) ? File.ReadAllText(fullPath) : null;
        }

        /// <summary>Agent / CLI helper: queue a build on the next editor delayCall.</summary>
        public static void RequestCreate()
        {
            var marker = PendingMarkerPath;
            Directory.CreateDirectory(Path.GetDirectoryName(marker) ?? "Temp");
            File.WriteAllText(marker, "build");
            AssetDatabase.Refresh();
        }

        private static TerrainData LoadOrCreateTerrainData()
        {
            var terrainData = AssetDatabase.LoadAssetAtPath<TerrainData>(TerrainDataPath);
            if (terrainData == null)
            {
                terrainData = new TerrainData();
                AssetDatabase.CreateAsset(terrainData, TerrainDataPath);
            }

            terrainData.heightmapResolution = 257;
            terrainData.size = new Vector3(256f, 80f, 256f);
            terrainData.SetHeights(0, 0, new float[257, 257]);
            EditorUtility.SetDirty(terrainData);
            return terrainData;
        }

        private static Material LoadOrCreatePreviewMaterial()
        {
            var material = AssetDatabase.LoadAssetAtPath<Material>(PreviewMaterialPath);
            if (material == null)
            {
                var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
                if (shader == null)
                    throw new System.InvalidOperationException(
                        "No Lit shader is available for the PCG preview.");
                material = new Material(shader);
                AssetDatabase.CreateAsset(material, PreviewMaterialPath);
            }

            if (material.HasProperty("_BaseColor"))
                material.SetColor("_BaseColor", new Color(0.17f, 0.42f, 0.21f));
            else if (material.HasProperty("_Color"))
                material.SetColor("_Color", new Color(0.17f, 0.42f, 0.21f));
            if (material.HasProperty("_Smoothness"))
                material.SetFloat("_Smoothness", 0.12f);
            EditorUtility.SetDirty(material);
            return material;
        }

        private static void CreateLighting()
        {
            var lightObject = new GameObject("Sun");
            lightObject.transform.rotation = Quaternion.Euler(48f, -32f, 0f);
            var light = lightObject.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.15f;
            light.color = new Color(1f, 0.95f, 0.82f);
        }

        private static void CreateCamera()
        {
            var cameraObject = new GameObject("Demo Camera");
            cameraObject.tag = "MainCamera";
            var camera = cameraObject.AddComponent<Camera>();
            camera.clearFlags = CameraClearFlags.Skybox;
            camera.fieldOfView = 56f;
            camera.nearClipPlane = 0.3f;
            camera.farClipPlane = 1200f;
            cameraObject.transform.SetPositionAndRotation(
                new Vector3(430f, 280f, -440f),
                Quaternion.LookRotation(
                    new Vector3(110f, 8f, 0f) - new Vector3(430f, 280f, -440f)));
        }

        private static GameObject CreateTerrain(TerrainData terrainData)
        {
            var terrainObject = Terrain.CreateTerrainGameObject(terrainData);
            terrainObject.name = "PCG Terrain";
            // 256m ZX terrain centered at origin for graph space alignment.
            terrainObject.transform.position = new Vector3(-128f, 0f, -128f);
            terrainObject.GetComponent<Terrain>().drawInstanced = true;
            return terrainObject;
        }

        private static Mesh CookGenerateGraph(PcgGraphAsset projectGraph, GameObject terrainObject)
        {
            var graphObject = new GameObject("PCG Generate Graph");
            var component = graphObject.AddComponent<PcgGraphComponent>();
            AssignGraphForCook(component, projectGraph, GenerateGraphPath);

            component.TerrainBindings.Add(new PcgTerrainBinding
            {
                bindingKey = "targetTerrain",
                source = PcgTerrainBindingSource.SceneObject,
                sceneObject = terrainObject,
                readFromHost = false,
                writeCookResult = true,
            });

            if (!component.Run(skipDocumentRefresh: true, forceSynchronous: true))
                throw new System.InvalidOperationException("Terrain generate graph cook failed.");

            var meshFilter = graphObject.GetComponent<MeshFilter>();
            if (meshFilter == null || meshFilter.sharedMesh == null)
                throw new System.InvalidOperationException(
                    "Terrain generate graph did not produce a preview mesh.");
            var meshRenderer = graphObject.GetComponent<MeshRenderer>();
            if (meshRenderer != null)
                meshRenderer.enabled = false;
            return meshFilter.sharedMesh;
        }

        private static void CreateHostImportGraph(PcgGraphAsset projectGraph, GameObject terrainObject)
        {
            var graphObject = new GameObject("PCG Host Import Graph");
            graphObject.transform.position = new Vector3(300f, 0f, 300f);
            var component = graphObject.AddComponent<PcgGraphComponent>();
            AssignGraphForCook(component, projectGraph, HostImportGraphPath);

            component.TerrainBindings.Add(new PcgTerrainBinding
            {
                bindingKey = "targetTerrain",
                source = PcgTerrainBindingSource.SceneObject,
                sceneObject = terrainObject,
                readFromHost = true,
                writeCookResult = false,
            });

            // Import after generate so the mesh reflects the written Terrain heights.
            if (!component.Run(skipDocumentRefresh: true, forceSynchronous: true))
                throw new System.InvalidOperationException("Host import graph cook failed.");
        }

        private static void AssignGraphForCook(
            PcgGraphComponent component,
            PcgGraphAsset projectGraph,
            string assetPath)
        {
            var json = ReadProjectText(assetPath);
            if (string.IsNullOrWhiteSpace(json))
                throw new System.InvalidOperationException($"Empty graph JSON at {assetPath}");

            // Always cook from a fresh ScriptableObject so we never depend on
            // ScriptedImporter GraphJson hydration / AssetDatabase path quirks.
            var cookAsset = ScriptableObject.CreateInstance<PcgGraphAsset>();
            cookAsset.name = Path.GetFileNameWithoutExtension(assetPath);
            cookAsset.SetGraphJson(json);
            component.GraphAsset = cookAsset;
            component.RefreshDocument();
            if (component.Document == null)
            {
                if (!PcgGraphSerializer.TryFromJson(json, out _, out var error))
                    throw new System.InvalidOperationException(
                        $"Graph JSON parse failed for {assetPath}: {error}");
                throw new System.InvalidOperationException(
                    $"Graph document still null for {assetPath} (jsonLen={json.Length}).");
            }

            // Persist a project asset reference when available so the saved scene
            // points at Assets/.../*.pcg instead of a transient instance.
            if (projectGraph != null)
            {
                projectGraph.SetGraphJson(json);
                component.GraphAsset = projectGraph;
                component.RefreshDocument();
                if (component.Document == null)
                {
                    // Fall back to the transient asset for a usable demo scene.
                    component.GraphAsset = cookAsset;
                    component.RefreshDocument();
                }
            }
        }

        private static void CreateMeshPreview(Mesh generatedMesh, Material material)
        {
            var previewMesh = AssetDatabase.LoadAssetAtPath<Mesh>(PreviewMeshPath);
            if (previewMesh == null)
            {
                previewMesh = Object.Instantiate(generatedMesh);
                previewMesh.name = "TerrainPcgDemoPreviewMesh";
                AssetDatabase.CreateAsset(previewMesh, PreviewMeshPath);
            }
            else
            {
                EditorUtility.CopySerialized(generatedMesh, previewMesh);
            }
            EditorUtility.SetDirty(previewMesh);

            var previewObject = new GameObject("PCG Mesh Preview");
            previewObject.transform.position = new Vector3(300f, 0f, 0f);
            previewObject.AddComponent<MeshFilter>().sharedMesh = previewMesh;
            previewObject.AddComponent<MeshRenderer>().sharedMaterial = material;
        }

        private static void EnsureFolder(string path)
        {
            if (AssetDatabase.IsValidFolder(path))
                return;

            var separator = path.LastIndexOf('/');
            if (separator <= 0)
                throw new System.InvalidOperationException($"Cannot create Unity folder: {path}");
            var parent = path.Substring(0, separator);
            var name = path.Substring(separator + 1);
            if (!AssetDatabase.IsValidFolder(parent))
                EnsureFolder(parent);
            AssetDatabase.CreateFolder(parent, name);
        }
    }
}
