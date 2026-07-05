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

                    EditorGUILayout.Space();
                    EditorGUI.BeginChangeCheck();
                    logging = EditorGUILayout.Toggle("Enable Logging", logging);
                    if (EditorGUI.EndChangeCheck())
                        PcgProjectSettings.SetLogging(logging);

                    EditorGUILayout.Space();
                    EditorGUILayout.HelpBox(
                        "When enabled, PCG graph execution results will be logged to the Console.",
                        MessageType.Info);
                },
                keywords = new[] { "PCG", "logging", "debug" },
            };

            return provider;
        }
    }
}
