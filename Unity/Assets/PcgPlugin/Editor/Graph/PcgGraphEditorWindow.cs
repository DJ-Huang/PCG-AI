using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgGraphEditorWindow : EditorWindow
    {
        private const string MenuPath = "PCG/Graph Editor";
        private const string IconPath = "Assets/PcgPlugin/Editor/Icons/pcg-icon-16.png";
        private const string SessionFilePathKey = "PCG.GraphEditor.CurrentFilePath";

        private PcgGraphView m_GraphView;
        private PcgGraphBlackboard m_Blackboard;
        private Button m_BlackboardToggle;
        private string m_CurrentFilePath;

        [MenuItem(MenuPath)]
        public static void Open()
        {
            var window = GetWindow<PcgGraphEditorWindow>();
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            window.titleContent = new GUIContent("PCG Graph", icon);
            window.Show();
        }

        /// <summary>
        /// Opens the editor and loads a graph from the given asset path.
        /// </summary>
        public static void OpenWithFile(string assetPath)
        {
            var window = GetWindow<PcgGraphEditorWindow>();
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            window.titleContent = new GUIContent("PCG Graph", icon);
            window.Show();
            window.ImportFromPath(Path.GetFullPath(assetPath));
        }

        private void OnEnable()
        {
            ConstructToolbar();
            ConstructGraphView();

            var persistedPath = SessionState.GetString(SessionFilePathKey, null);
            if (!string.IsNullOrEmpty(persistedPath) && File.Exists(persistedPath))
                ImportFromPath(persistedPath);
            else
                LoadDefaultGraph();
        }

        private void ConstructToolbar()
        {
            var toolbar = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    paddingLeft = 6,
                    paddingRight = 6,
                    paddingTop = 4,
                    paddingBottom = 4,
                },
            };

            toolbar.Add(MakeButton("New", NewGraph));
            toolbar.Add(MakeButton("Save", SaveGraph));
            toolbar.Add(MakeButton("Save As…", SaveAsGraph));

            var spacer = new VisualElement { style = { flexGrow = 1 } };
            toolbar.Add(spacer);

            m_BlackboardToggle = MakeButton("Parameters", ToggleBlackboard);
            toolbar.Add(m_BlackboardToggle);

            toolbar.Add(MakeButton("Show in Project", LocateInProject));

            rootVisualElement.Add(toolbar);
        }

        private static Button MakeButton(string text, System.Action onClick)
        {
            var button = new Button(onClick) { text = text };
            button.style.marginRight = 4;
            return button;
        }

        private void ConstructGraphView()
        {
            var contentRow = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row, flexGrow = 1 },
            };

            m_GraphView = new PcgGraphView();
            m_GraphView.SetHostWindow(this);

            m_Blackboard = new PcgGraphBlackboard(m_GraphView);
            m_GraphView.Blackboard = m_Blackboard;
            m_Blackboard.style.display = DisplayStyle.None;

            contentRow.Add(m_Blackboard);
            contentRow.Add(m_GraphView);

            rootVisualElement.Add(contentRow);
        }

        private void LoadDefaultGraph()
        {
            m_GraphView.LoadDocument(PcgGraphDefaults.CreatePipeline());
            SetCurrentFilePath(null);
            UpdateTitle();
            SetStatus("Ready — default 3-node pipeline loaded.");
        }

        private void NewGraph()
        {
            if (!EditorUtility.DisplayDialog("New Graph", "Replace the current graph with the default pipeline?", "New", "Cancel"))
                return;

            LoadDefaultGraph();
        }

        private void ImportFromPath(string path)
        {
            var json = File.ReadAllText(path);
            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out var error))
            {
                SetStatus($"Import failed: {error}");
                Debug.LogError($"[PCG] Graph import failed: {error}");
                return;
            }

            m_GraphView.LoadDocument(doc);
            SetCurrentFilePath(path);
            UpdateTitle();
            SetStatus($"Imported: {path}");
        }

        private void SaveGraph()
        {
            if (string.IsNullOrEmpty(m_CurrentFilePath))
            {
                SaveAsGraph();
                return;
            }

            var doc = m_GraphView.ExportDocument();
            var json = PcgGraphSerializer.ToJson(doc);
            File.WriteAllText(m_CurrentFilePath, json);
            AssetDatabase.Refresh();
            SetStatus($"Saved: {m_CurrentFilePath}");
        }

        private void SaveAsGraph()
        {
            var doc = m_GraphView.ExportDocument();
            var json = PcgGraphSerializer.ToJson(doc);

            var path = EditorUtility.SaveFilePanel(
                "Save Graph As",
                PcgGraphRunner.DefaultSchemaDir,
                "graph.pcg",
                "pcg");

            if (string.IsNullOrEmpty(path))
                return;

            File.WriteAllText(path, json);
            SetCurrentFilePath(path);
            UpdateTitle();
            AssetDatabase.Refresh();
            SetStatus($"Saved: {path}");
        }

        private void SetCurrentFilePath(string path)
        {
            m_CurrentFilePath = path;
            SessionState.SetString(SessionFilePathKey, path ?? string.Empty);
        }

        private void UpdateTitle()
        {
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            var name = string.IsNullOrEmpty(m_CurrentFilePath)
                ? "PCG Graph"
                : $"PCG Graph — {Path.GetFileName(m_CurrentFilePath)}";
            titleContent = new GUIContent(name, icon);
        }

        private void SetStatus(string message)
        {
            Debug.Log($"[PCG] {message}");
        }

        private void LocateInProject()
        {
            if (string.IsNullOrEmpty(m_CurrentFilePath))
            {
                Debug.LogWarning("[PCG] No file loaded to locate.");
                return;
            }

            var assetPath = "Assets" + m_CurrentFilePath.Substring(Application.dataPath.Length).Replace('\\', '/');
            var asset = AssetDatabase.LoadMainAssetAtPath(assetPath);
            if (asset != null)
                EditorGUIUtility.PingObject(asset);
            else
                Debug.LogWarning($"[PCG] File not in project: {assetPath}");
        }

        private void ToggleBlackboard()
        {
            var visible = m_Blackboard.style.display.value == DisplayStyle.Flex;
            m_Blackboard.style.display = visible ? DisplayStyle.None : DisplayStyle.Flex;
        }
    }
}
