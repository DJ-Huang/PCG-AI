using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Settings window for PCG plugin configuration.
    /// </summary>
    public class PcgSettingsWindow : EditorWindow
    {
        private int _seed = 42;
        private string _graphPath = "";

        [MenuItem("PCG/Settings")]
        public static void ShowWindow()
        {
            GetWindow<PcgSettingsWindow>("PCG Settings");
        }

        private void OnGUI()
        {
            GUILayout.Label("PCG Plugin Settings", EditorStyles.boldLabel);
            EditorGUILayout.Space();

            _seed = EditorGUILayout.IntField("Seed", _seed);
            _graphPath = EditorGUILayout.TextField("Graph JSON Path", _graphPath);

            EditorGUILayout.Space();

            if (GUILayout.Button("Browse…"))
            {
                _graphPath = EditorUtility.OpenFilePanel("Select Graph JSON", "", "json");
            }

            EditorGUILayout.Space();

            if (GUILayout.Button("Run Graph"))
            {
                if (!string.IsNullOrEmpty(_graphPath))
                {
                    PcgGraphLoader.LoadAndExecute(_graphPath, _seed);
                }
                else
                {
                    Debug.LogWarning("[PCG] No graph path specified.");
                }
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Core Version", PcgNative.GetVersion());
        }
    }
}
