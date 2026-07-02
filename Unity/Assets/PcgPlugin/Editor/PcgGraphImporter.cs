using System.IO;
using System.Text;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Menu items for importing and running PCG graphs.
    /// </summary>
    public static class PcgGraphImporter
    {
        private const string MenuRoot = "PCG/";

        [MenuItem(MenuRoot + "Run Graph from File…")]
        public static void RunGraphFromFile()
        {
            string path = EditorUtility.OpenFilePanel("Select Graph JSON", "", "json");
            if (string.IsNullOrEmpty(path))
                return;

            var result = PcgGraphLoader.LoadAndExecute(path);
            if (result != null)
            {
                Debug.Log($"[PCG] Result: {result}");
            }
        }

        [MenuItem(MenuRoot + "Print PcgCore Version")]
        public static void PrintVersion()
        {
            var version = PcgNative.GetVersion();
            Debug.Log($"[PCG] {version}");
        }
    }
}
