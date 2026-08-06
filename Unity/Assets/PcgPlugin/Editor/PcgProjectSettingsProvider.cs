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
            var provider = new SettingsProvider("Project/PCG AI", SettingsScope.Project)
            {
                label = "PCG AI",
                guiHandler = _ =>
                {
                    var logging = PcgProjectSettings.IsLogEnabled;
                    var serverUrl = PcgCookClient.BaseUrl;
                    var meshyApiKey = PcgMeshySettings.ApiKey;

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
                    EditorGUILayout.HelpBox(
                        "All C++ cook/FBX work runs in localhost pcg-server. " +
                        "Unity no longer loads PcgCore / PcgFbxExporter native plugins. " +
                        "Start the server with scripts/run-pcg-server.sh before cooking. " +
                        "Meshy API key is machine-local (EditorPrefs) and used by Meshy 3D Generator nodes.",
                        MessageType.Info);
                },
                keywords = new[]
                {
                    "PCG", "logging", "debug", "server", "backend", "cook", "meshy", "api", "key",
                },
            };

            return provider;
        }
    }
}
