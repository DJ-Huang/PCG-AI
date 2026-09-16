using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    public static class PcgProjectSettingsProvider
    {
        [SettingsProvider]
        public static SettingsProvider CreateProvider()
        {
            var provider = new SettingsProvider("Project/PICG", SettingsScope.Project)
            {
                label = "PICG",
                guiHandler = _ =>
                {
                    var logging = PcgProjectSettings.IsLogEnabled;
                    var serverUrl = PcgCookClient.BaseUrl;
                    var meshyApiKey = PcgMeshySettings.ApiKey;
                    var tripoApiKey = PcgTripoSettings.ApiKey;
                    var tripoApiRegion = PcgTripoSettings.ApiRegion;
                    var useHttpProxy = PcgThirdPartyHttpSettings.UseHttpProxy;
                    var proxyUrl = PcgThirdPartyHttpSettings.ProxyUrl;

                    EditorGUILayout.Space();
                    EditorGUI.BeginChangeCheck();
                    logging = EditorGUILayout.Toggle("Enable Logging", logging);
                    if (EditorGUI.EndChangeCheck())
                        PcgProjectSettings.SetLogging(logging);

                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Cook Backend", PcgCookBackend.DescribeBackend());
                    EditorGUI.BeginChangeCheck();
                    serverUrl = EditorGUILayout.TextField("Cook Server URL", serverUrl);
                    if (EditorGUI.EndChangeCheck())
                        PcgCookClient.BaseUrl = serverUrl;

                    if (GUILayout.Button("Health Check"))
                        PcgServerMenu.HealthCheck();

                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Third-Party API (HTTP Proxy)", EditorStyles.boldLabel);
                    EditorGUI.BeginChangeCheck();
                    useHttpProxy = EditorGUILayout.Toggle(
                        new GUIContent(
                            "Use HTTP Proxy",
                            "Route Meshy / Tripo HTTP through a local proxy. Disable for direct connection."),
                        useHttpProxy);
                    if (useHttpProxy)
                    {
                        proxyUrl = EditorGUILayout.TextField(
                            new GUIContent("Proxy URL", "Example: http://127.0.0.1:7897"),
                            proxyUrl);
                    }
                    if (EditorGUI.EndChangeCheck())
                    {
                        PcgThirdPartyHttpSettings.UseHttpProxy = useHttpProxy;
                        PcgThirdPartyHttpSettings.ProxyUrl = proxyUrl;
                    }

                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Meshy (Image to 3D)", EditorStyles.boldLabel);
                    EditorGUI.BeginChangeCheck();
                    meshyApiKey = EditorGUILayout.PasswordField(
                        new GUIContent(
                            "Meshy API Key",
                            "Stored locally in EditorPrefs (PCG.Meshy.ApiKey). Not saved into project assets."),
                        meshyApiKey);
                    if (EditorGUI.EndChangeCheck())
                        PcgMeshySettings.ApiKey = meshyApiKey;

                    EditorGUILayout.BeginHorizontal();
                    if (GUILayout.Button("Clear API Key", GUILayout.Width(120)))
                        PcgMeshySettings.ClearApiKey();
                    EditorGUILayout.LabelField(
                        PcgMeshySettings.HasApiKey ? "Key cached on this machine" : "No key set",
                        EditorStyles.miniLabel);
                    EditorGUILayout.EndHorizontal();

                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Tripo (Image to 3D)", EditorStyles.boldLabel);
                    EditorGUI.BeginChangeCheck();
                    tripoApiKey = EditorGUILayout.PasswordField(
                        new GUIContent(
                            "Tripo API Key",
                            "Stored locally in EditorPrefs (PCG.Tripo.ApiKey). Not saved into project assets."),
                        tripoApiKey);
                    if (EditorGUI.EndChangeCheck())
                        PcgTripoSettings.ApiKey = tripoApiKey;

                    EditorGUI.BeginChangeCheck();
                    var tripoRegionIndex = EditorGUILayout.Popup(
                        new GUIContent(
                            "Tripo API Region",
                            "China keys from platform.tripo3d.com require the China endpoint."),
                        tripoApiRegion == PcgTripoSettings.ApiRegionChina ? 1 : 0,
                        new[] { "Global (openapi.tripo3d.ai)", "China (openapi.tripo3d.com)" });
                    if (EditorGUI.EndChangeCheck())
                    {
                        tripoApiRegion = tripoRegionIndex == 1
                            ? PcgTripoSettings.ApiRegionChina
                            : PcgTripoSettings.ApiRegionGlobal;
                        PcgTripoSettings.ApiRegion = tripoApiRegion;
                    }

                    EditorGUILayout.BeginHorizontal();
                    if (GUILayout.Button("Clear API Key", GUILayout.Width(120)))
                        PcgTripoSettings.ClearApiKey();
                    EditorGUILayout.LabelField(
                        PcgTripoSettings.HasApiKey ? "Key cached on this machine" : "No key set",
                        EditorStyles.miniLabel);
                    EditorGUILayout.EndHorizontal();

                    EditorGUILayout.Space();
                    EditorGUILayout.HelpBox(
                        "All C++ cook/FBX work runs in localhost pcg-server. " +
                        "Unity no longer loads PcgCore / PcgFbxExporter native plugins. " +
                        "Start the server with scripts/run-pcg-server.sh before cooking. " +
                        "Meshy API key is machine-local (EditorPrefs) and used by Meshy 3D Generator nodes. " +
                        "Tripo API key is also machine-local and used by Tripo 3D Generator nodes.",
                        MessageType.Info);
                },
                keywords = new[]
                {
                    "PCG", "logging", "debug", "server", "backend", "cook", "meshy", "tripo", "api", "key", "proxy",
                },
            };

            return provider;
        }
    }
}
