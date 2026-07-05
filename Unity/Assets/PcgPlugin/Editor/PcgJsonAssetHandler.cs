using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Intercepts double-click on *.pcg assets in the Project window
    /// and opens them in the PCG Graph Editor.
    /// </summary>
    [InitializeOnLoad]
    public static class PcgJsonAssetHandler
    {
        private const string PcgExtension = ".pcg";

        static PcgJsonAssetHandler()
        {
            EditorApplication.projectWindowItemOnGUI += OnProjectWindowItemGUI;
        }

        private static void OnProjectWindowItemGUI(string guid, Rect rect)
        {
            if (Event.current == null)
                return;

            if (Event.current.type != EventType.MouseDown || Event.current.clickCount != 2)
                return;

            if (!rect.Contains(Event.current.mousePosition))
                return;

            var path = AssetDatabase.GUIDToAssetPath(guid);
            if (string.IsNullOrEmpty(path) || !path.EndsWith(PcgExtension, StringComparison.OrdinalIgnoreCase))
                return;

            if (!IsValidPcgGraph(path))
                return;

            Event.current.Use();
            GUIUtility.hotControl = 0;
            var capturedPath = path;
            EditorApplication.delayCall += () => Graph.PcgGraphEditorWindow.OpenWithFile(capturedPath);
        }

        private static bool IsValidPcgGraph(string assetPath)
        {
            var asset = AssetDatabase.LoadAssetAtPath<PcgGraphAsset>(assetPath);
            if (asset != null && !string.IsNullOrEmpty(asset.GraphJson))
                return asset.GraphJson.Contains("\"version\"") && asset.GraphJson.Contains("\"nodes\"");

            var fullPath = Path.GetFullPath(assetPath);
            if (!File.Exists(fullPath))
                return false;

            try
            {
                var json = File.ReadAllText(fullPath);
                return json.Contains("\"version\"") && json.Contains("\"nodes\"");
            }
            catch
            {
                return false;
            }
        }
    }
}
