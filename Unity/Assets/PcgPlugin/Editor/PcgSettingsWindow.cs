using System.IO;
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
        private string _watchedPath = "";
        private bool _autoReload = true;

        [MenuItem("PCG/Settings")]
        public static void ShowWindow()
        {
            GetWindow<PcgSettingsWindow>("PCG Settings");
        }

        private void OnEnable()
        {
            _seed = PcgGraphWatcher.Seed;
            _watchedPath = PcgGraphWatcher.WatchedPath;
            _autoReload = PcgGraphWatcher.AutoReload;
            _graphPath = _watchedPath;
        }

        private void OnGUI()
        {
            GUILayout.Label("PCG Plugin Settings", EditorStyles.boldLabel);
            EditorGUILayout.Space();

            _seed = EditorGUILayout.IntField("Seed", _seed);
            _graphPath = EditorGUILayout.TextField("Graph JSON Path", _graphPath);

            EditorGUILayout.Space();

            if (GUILayout.Button("Browse Graph…"))
            {
                _graphPath = EditorUtility.OpenFilePanel(
                    "Select Graph JSON",
                    PcgGraphRunner.DefaultSchemaDir,
                    "json");
            }

            if (GUILayout.Button("Run Graph"))
            {
                if (!string.IsNullOrEmpty(_graphPath))
                    PcgGraphRunner.RunFileAndUpdatePreview(_graphPath, _seed);
                else
                    Debug.LogWarning("[PCG] No graph path specified.");
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Watched Graph (Web → Unity loop)", EditorStyles.boldLabel);

            _watchedPath = EditorGUILayout.TextField("Watched Path", _watchedPath);
            _autoReload = EditorGUILayout.Toggle("Auto Reload on File Change", _autoReload);

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Browse Watched…"))
            {
                var picked = EditorUtility.OpenFilePanel(
                    "Select Watched Graph JSON",
                    PcgGraphRunner.DefaultSchemaDir,
                    "json");
                if (!string.IsNullOrEmpty(picked))
                    _watchedPath = picked;
            }

            if (GUILayout.Button("Reload Watched"))
            {
                ApplyWatchSettings();
                if (File.Exists(_watchedPath))
                    PcgGraphRunner.RunFileAndUpdatePreview(_watchedPath, _seed);
                else
                    Debug.LogWarning($"[PCG] Watched file not found: {_watchedPath}");
            }
            EditorGUILayout.EndHorizontal();

            if (GUILayout.Button("Apply Watch Settings"))
                ApplyWatchSettings();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Core Version", PcgNative.GetVersion());
            EditorGUILayout.HelpBox(
                "Workflow: Web editor → Send to Unity → schema/editor-export.pcg.json → Reload Watched Graph (or enable Auto Reload).",
                MessageType.Info);
        }

        private void ApplyWatchSettings()
        {
            PcgGraphWatcher.Seed = _seed;
            PcgGraphWatcher.WatchedPath = _watchedPath;
            PcgGraphWatcher.AutoReload = _autoReload;
        }
    }
}
