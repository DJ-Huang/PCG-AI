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
        private string _meshyApiKey = "";
        private string _tripoApiKey = "";
        private string _tripoApiRegion = PcgTripoSettings.ApiRegionGlobal;
        private bool _useHttpProxy = PcgThirdPartyHttpSettings.UseHttpProxy;
        private string _proxyUrl = PcgThirdPartyHttpSettings.ProxyUrl;
        private PcgScatterDisplayMode _defaultScatterDisplayMode = PcgScatterDisplayMode.MergedMesh;

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
            _meshyApiKey = PcgMeshySettings.ApiKey;
            _tripoApiKey = PcgTripoSettings.ApiKey;
            _tripoApiRegion = PcgTripoSettings.ApiRegion;
            _useHttpProxy = PcgThirdPartyHttpSettings.UseHttpProxy;
            _proxyUrl = PcgThirdPartyHttpSettings.ProxyUrl;
            _defaultScatterDisplayMode = PcgProjectSettings.DefaultScatterDisplayMode;
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
                    "pcg");
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
                    "pcg");
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
            EditorGUILayout.LabelField("Scatter Rendering", EditorStyles.boldLabel);
            _defaultScatterDisplayMode = (PcgScatterDisplayMode)EditorGUILayout.EnumPopup(
                new GUIContent(
                    "Default Display Mode",
                    "Default for new PcgGraphComponent instances. " +
                    "Per-component override: Inspector or Graph Editor toolbar."),
                _defaultScatterDisplayMode);

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Apply Default To New Components"))
                PcgProjectSettings.SetDefaultScatterDisplayMode(_defaultScatterDisplayMode);

            if (GUILayout.Button("Set All Scene Components"))
            {
                PcgProjectSettings.SetDefaultScatterDisplayMode(_defaultScatterDisplayMode);
                foreach (var component in Object.FindObjectsOfType<PcgGraphComponent>())
                {
                    if (component == null)
                        continue;
                    Undo.RecordObject(component, "Set Scatter Display Mode");
                    component.SetScatterDisplayMode(_defaultScatterDisplayMode);
                }
            }
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Third-Party API (HTTP Proxy)", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();
            _useHttpProxy = EditorGUILayout.Toggle(
                new GUIContent(
                    "Use HTTP Proxy",
                    "Route Meshy / Tripo HTTP through a local proxy (e.g. Clash mixed port). " +
                    "Disable for direct connection when the proxy is off."),
                _useHttpProxy);
            if (_useHttpProxy)
            {
                _proxyUrl = EditorGUILayout.TextField(
                    new GUIContent(
                        "Proxy URL",
                        "Example: http://127.0.0.1:7897 (Clash Verge mixed port)."),
                    _proxyUrl);
            }
            if (EditorGUI.EndChangeCheck())
            {
                PcgThirdPartyHttpSettings.UseHttpProxy = _useHttpProxy;
                PcgThirdPartyHttpSettings.ProxyUrl = _proxyUrl;
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Meshy (Image to 3D)", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();
            _meshyApiKey = EditorGUILayout.PasswordField(
                new GUIContent(
                    "Meshy API Key",
                    "Cached locally via EditorPrefs. Required by Meshy 3D Generator nodes."),
                _meshyApiKey);
            if (EditorGUI.EndChangeCheck())
                PcgMeshySettings.ApiKey = _meshyApiKey;

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Clear API Key", GUILayout.Width(120)))
            {
                PcgMeshySettings.ClearApiKey();
                _meshyApiKey = "";
            }
            EditorGUILayout.LabelField(
                PcgMeshySettings.HasApiKey ? "Key cached on this machine" : "No key set",
                EditorStyles.miniLabel);
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Tripo (Image to 3D)", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();
            _tripoApiKey = EditorGUILayout.PasswordField(
                new GUIContent(
                    "Tripo API Key",
                    "Cached locally via EditorPrefs. Required by Tripo 3D Generator nodes."),
                _tripoApiKey);
            if (EditorGUI.EndChangeCheck())
                PcgTripoSettings.ApiKey = _tripoApiKey;

            EditorGUI.BeginChangeCheck();
            var tripoRegionIndex = EditorGUILayout.Popup(
                new GUIContent(
                    "Tripo API Region",
                    "China keys from platform.tripo3d.com require the China endpoint. " +
                    "Global uses openapi.tripo3d.ai."),
                _tripoApiRegion == PcgTripoSettings.ApiRegionChina ? 1 : 0,
                new[] { "Global (openapi.tripo3d.ai)", "China (openapi.tripo3d.com)" });
            if (EditorGUI.EndChangeCheck())
            {
                _tripoApiRegion = tripoRegionIndex == 1
                    ? PcgTripoSettings.ApiRegionChina
                    : PcgTripoSettings.ApiRegionGlobal;
                PcgTripoSettings.ApiRegion = _tripoApiRegion;
            }

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Clear API Key", GUILayout.Width(120)))
            {
                PcgTripoSettings.ClearApiKey();
                _tripoApiKey = "";
            }
            EditorGUILayout.LabelField(
                PcgTripoSettings.HasApiKey ? "Key cached on this machine" : "No key set",
                EditorStyles.miniLabel);
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Core Version", PcgNative.GetVersion());
            EditorGUILayout.HelpBox(
                "Workflow: Web editor → Send to Unity → schema/editor-export.pcg → Reload Watched Graph (or enable Auto Reload).",
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
