using System.IO;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    public static class PcgSubgraphAssetMenu
    {
        [MenuItem("Assets/Create/PCG/Subgraph Asset", false, 82)]
        public static void CreateSubgraphAsset()
        {
            var folder = "Assets";
            if (Selection.activeObject != null)
            {
                var selectedPath = AssetDatabase.GetAssetPath(Selection.activeObject);
                if (!string.IsNullOrEmpty(selectedPath))
                {
                    folder = Directory.Exists(selectedPath)
                        ? selectedPath
                        : Path.GetDirectoryName(selectedPath)?.Replace('\\', '/') ?? "Assets";
                }
            }

            var path = AssetDatabase.GenerateUniqueAssetPath(folder + "/NewSubgraph.pcgsubgraph");
            // Empty template: SubgraphInput + SubgraphOutput only.
            var doc = PcgSubgraphAssetDocument.CreateEmpty(
                Path.GetFileNameWithoutExtension(path));
            File.WriteAllText(path, PcgSubgraphAssetSerializer.ToJson(doc));
            AssetDatabase.ImportAsset(path);
            var asset = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(path);
            Selection.activeObject = asset;
            EditorGUIUtility.PingObject(asset);
            PcgGraphEditorWindow.ShowGraphEditWindow(path);
        }
    }
}
