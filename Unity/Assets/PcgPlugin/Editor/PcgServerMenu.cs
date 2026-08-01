using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    public static class PcgServerMenu
    {
        private const string DocsHint =
            "Start the backend with scripts/run-pcg-server.sh (macOS/Linux) or " +
            "scripts/build-pcg-server.ps1 -Run (Windows). See docs/pcg-server.md.";

        [MenuItem("PCG/Server/Health Check", false, 200)]
        public static void HealthCheck()
        {
            var (ok, version, error) = PcgCookClient.TryHealthCheck();
            if (ok)
            {
                EditorUtility.DisplayDialog(
                    "PCG Server",
                    $"Connected to {PcgCookClient.BaseUrl}\nVersion: {version}\nBackend: {PcgCookBackend.DescribeBackend()}",
                    "OK");
            }
            else
            {
                EditorUtility.DisplayDialog(
                    "PCG Server",
                    $"Health check failed for {PcgCookClient.BaseUrl}\n\n{error}\n\n{DocsHint}",
                    "OK");
            }
        }

        [MenuItem("PCG/Server/Set Server URL…", false, 220)]
        public static void SetServerUrl()
        {
            var current = PcgCookClient.BaseUrl;
            var next = EditorInputDialog.Show(
                "PCG Server URL",
                "Localhost cook server base URL:",
                current);
            if (next == null)
                return;
            PcgCookClient.BaseUrl = next;
            Debug.Log($"[PCG] Cook server URL → {PcgCookClient.BaseUrl}");
        }

        [MenuItem("PCG/Server/Open Setup Docs", false, 230)]
        public static void OpenDocs()
        {
            var path = System.IO.Path.GetFullPath(
                System.IO.Path.Combine(Application.dataPath, "../../docs/pcg-server.md"));
            if (System.IO.File.Exists(path))
                EditorUtility.OpenWithDefaultApp(path);
            else
                EditorUtility.DisplayDialog("PCG Server", DocsHint, "OK");
        }
    }

    internal static class EditorInputDialog
    {
        public static string Show(string title, string message, string defaultText)
        {
            var window = ScriptableObject.CreateInstance<InputDialogWindow>();
            window.titleContent = new GUIContent(title);
            window.Message = message;
            window.Value = defaultText ?? string.Empty;
            window.position = new Rect(Screen.width / 2f, Screen.height / 2f, 420, 120);
            window.ShowModalUtility();
            return window.Accepted ? window.Value : null;
        }

        private sealed class InputDialogWindow : EditorWindow
        {
            public string Message;
            public string Value;
            public bool Accepted;

            private void OnGUI()
            {
                EditorGUILayout.LabelField(Message, EditorStyles.wordWrappedLabel);
                GUI.SetNextControlName("pcg_server_url");
                Value = EditorGUILayout.TextField(Value);
                EditorGUI.FocusTextInControl("pcg_server_url");
                EditorGUILayout.Space();
                using (new EditorGUILayout.HorizontalScope())
                {
                    if (GUILayout.Button("Cancel"))
                    {
                        Accepted = false;
                        Close();
                    }
                    if (GUILayout.Button("OK"))
                    {
                        Accepted = true;
                        Close();
                    }
                }
            }
        }
    }
}
