using System;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Blocks Player builds when referenced graph assets failed import or still contain external refs.
    /// </summary>
    public sealed class PcgGraphBuildValidator : IPreprocessBuildWithReport
    {
        public int callbackOrder => 0;

        public void OnPreprocessBuild(BuildReport report)
        {
            var scenes = EditorBuildSettings.scenes
                .Where(scene => scene.enabled)
                .Select(scene => scene.path)
                .Where(path => !string.IsNullOrEmpty(path))
                .ToList();

            foreach (var scenePath in scenes)
            {
                var scene = SceneManager.GetSceneByPath(scenePath);
                var needClose = false;
                if (!scene.IsValid() || !scene.isLoaded)
                {
                    scene = UnityEditor.SceneManagement.EditorSceneManager.OpenScene(
                        scenePath, UnityEditor.SceneManagement.OpenSceneMode.Additive);
                    needClose = true;
                }

                try
                {
                    ValidateScene(scene);
                }
                finally
                {
                    if (needClose)
                        UnityEditor.SceneManagement.EditorSceneManager.CloseScene(scene, true);
                }
            }
        }

        private static void ValidateScene(Scene scene)
        {
            foreach (var root in scene.GetRootGameObjects())
            {
                foreach (var component in root.GetComponentsInChildren<PcgGraphComponent>(true))
                {
                    ValidateGraphAsset(component.GraphAsset, component);
                }
            }
        }

        internal static void ValidateGraphAsset(PcgGraphAsset asset, UnityEngine.Object context = null)
        {
            if (asset == null)
                return;

            if (!asset.ImportSucceeded || string.IsNullOrEmpty(asset.GraphJson))
            {
                throw new BuildFailedException(
                    $"[PCG] Graph asset '{AssetDatabase.GetAssetPath(asset)}' has no valid baked GraphJson: {asset.ImportError}");
            }

            if (PcgExecutionDocumentBuilder.ContainsExternalOrAuthoringV3(asset.GraphJson, out var reason))
            {
                throw new BuildFailedException(
                    $"[PCG] Graph asset '{AssetDatabase.GetAssetPath(asset)}' is not Player-ready: {reason}");
            }

            if (asset.DependencyGuids != null)
            {
                foreach (var guid in asset.DependencyGuids)
                {
                    var path = AssetDatabase.GUIDToAssetPath(PcgAssetGuidUtility.Canonicalize(guid));
                    if (string.IsNullOrEmpty(path))
                    {
                        throw new BuildFailedException(
                            $"[PCG] Graph asset '{AssetDatabase.GetAssetPath(asset)}' depends on missing SubgraphAsset GUID {guid}.");
                    }

                    var subgraph = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(path);
                    if (subgraph != null && !subgraph.ImportSucceeded)
                    {
                        throw new BuildFailedException(
                            $"[PCG] Dependency '{path}' failed import: {subgraph.ImportError}");
                    }
                }
            }
        }
    }
}
