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
                    EditorGUILayout.HelpBox(
                        "All C++ cook/FBX work runs in localhost pcg-server. " +
                        "Unity no longer loads PcgCore / PcgFbxExporter native plugins. " +
                        "Start the server with scripts/run-pcg-server.sh before cooking.",
                        MessageType.Info);
                },
                keywords = new[] { "PCG", "logging", "debug", "server", "backend", "cook" },
            };

            return provider;
        }
    }
}
