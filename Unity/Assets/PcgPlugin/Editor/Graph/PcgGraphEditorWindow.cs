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
        private const string PcgExtension = "pcg";

        /// <summary>Asset GUID of the open .pcg file (Shader Graph: m_Selected).</summary>
        [SerializeField]
        private string m_Selected;

        private PcgGraphView m_GraphView;
        private PcgGraphBlackboard m_Blackboard;
        private PcgNodeInspector m_Inspector;
        private Button m_BlackboardToggle;
        private Button m_InspectorToggle;
        private string m_CurrentFilePath;
        private bool m_GraphLoaded;

        public string selectedGuid => m_Selected;

        public bool HasLoadedGraph => m_GraphLoaded && m_GraphView != null;

        public PcgGraphDocument ExportLiveDocument() => m_GraphView?.ExportDocument();

        public bool MatchesGraphAsset(string assetDatabasePath, string assetGuid)
        {
            if (!string.IsNullOrEmpty(assetGuid) && selectedGuid == assetGuid)
                return true;

            if (string.IsNullOrEmpty(assetDatabasePath) || string.IsNullOrEmpty(m_CurrentFilePath))
                return false;

            var windowAssetPath = FullPathToAssetPath(m_CurrentFilePath);
            return string.Equals(windowAssetPath, assetDatabasePath, System.StringComparison.OrdinalIgnoreCase);
        }

        [MenuItem(MenuPath)]
        public static void Open()
        {
            var window = GetWindow<PcgGraphEditorWindow>();
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            window.titleContent = new GUIContent("PCG Graph", icon);
            window.Show();
        }

        /// <summary>
        /// Opens the editor and loads a graph from the given project asset path.
        /// </summary>
        public static bool OpenWithFile(string assetPath)
        {
            return ShowGraphEditWindow(assetPath);
        }

        /// <summary>Shader Graph: ShowGraphEditWindow — focus existing or Initialize(guid).</summary>
        public static bool ShowGraphEditWindow(string assetPath)
        {
            if (string.IsNullOrEmpty(assetPath))
                return false;

            var guid = AssetDatabase.AssetPathToGUID(assetPath);
            if (string.IsNullOrEmpty(guid))
                return false;

            if (!assetPath.EndsWith("." + PcgExtension, System.StringComparison.OrdinalIgnoreCase))
                return false;

            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window.selectedGuid == guid)
                {
                    window.Focus();
                    return true;
                }
            }

            var editor = GetWindow<PcgGraphEditorWindow>();
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            editor.titleContent = new GUIContent("PCG Graph", icon);
            editor.Show();
            editor.Initialize(guid);
            editor.Focus();
            return true;
        }

        public void Initialize(string assetGuid)
        {
            if (string.IsNullOrEmpty(assetGuid))
                return;

            var asset = AssetDatabase.LoadAssetAtPath<Object>(AssetDatabase.GUIDToAssetPath(assetGuid));
            if (asset == null || !EditorUtility.IsPersistent(asset))
                return;

            if (m_Selected == assetGuid && m_GraphLoaded)
                return;

            var assetPath = AssetDatabase.GUIDToAssetPath(assetGuid);
            if (string.IsNullOrEmpty(assetPath))
                return;

            var extension = Path.GetExtension(assetPath);
            if (string.IsNullOrEmpty(extension) ||
                !extension.Substring(1).Equals(PcgExtension, System.StringComparison.OrdinalIgnoreCase))
                return;

            m_Selected = assetGuid;
            m_GraphLoaded = false;

            if (m_GraphView != null)
                m_GraphView.viewDataKey = assetGuid;

            var fullPath = AssetPathToFullPath(assetPath);
            if (string.IsNullOrEmpty(fullPath) || !ImportFromPath(fullPath))
            {
                Debug.LogWarning($"[PCG] Failed to load graph asset: {assetPath}");
                m_Selected = null;
                if (m_GraphView != null)
                    LoadDefaultGraph();
            }
        }

        private void OnEnable()
        {
            m_GraphLoaded = false;
            rootVisualElement.Clear();
            ConstructToolbar();
            ConstructGraphView();
        }

        private void OnDisable()
        {
            m_GraphView?.DestroyUndoState();
            m_GraphLoaded = false;
        }

        private void Update()
        {
            if (m_GraphView == null)
                return;

            if (!m_GraphLoaded)
            {
                if (!string.IsNullOrEmpty(m_Selected))
                {
                    var guid = m_Selected;
                    m_Selected = null;
                    Initialize(guid);
                }
                else
                {
                    LoadDefaultGraph();
                }

                return;
            }

            if (m_GraphView.State != null && m_GraphView.State.WasUndoRedoPerformed)
                m_GraphView.RestoreFromUndoState();
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

            m_InspectorToggle = MakeButton("Inspector", ToggleInspector);
            toolbar.Add(m_InspectorToggle);

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
            rootVisualElement.style.flexGrow = 1;

            var contentRow = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row, flexGrow = 1 },
            };

            m_GraphView = new PcgGraphView();
            m_GraphView.SetHostWindow(this);
            if (!string.IsNullOrEmpty(m_Selected))
                m_GraphView.viewDataKey = m_Selected;

            m_Blackboard = new PcgGraphBlackboard(m_GraphView);
            m_GraphView.Blackboard = m_Blackboard;
            m_Blackboard.style.display = DisplayStyle.None;

            m_Inspector = new PcgNodeInspector(m_GraphView, m_Blackboard);
            m_GraphView.Inspector = m_Inspector;

            m_Blackboard.OnParametersChanged += () => m_Inspector.OnSelectionChanged();

            contentRow.Add(m_Blackboard);
            contentRow.Add(m_GraphView);
            contentRow.Add(m_Inspector);

            rootVisualElement.Add(contentRow);
        }

        private void LoadDefaultGraph()
        {
            m_GraphView.LoadDocument(PcgGraphDefaults.CreatePipeline());
            m_Selected = null;
            m_CurrentFilePath = null;
            m_GraphLoaded = true;
            m_GraphView.viewDataKey = "PCG.DefaultGraph";
            UpdateTitle();
            SetStatus("Ready — default 3-node pipeline loaded.");
        }

        private void NewGraph()
        {
            if (!EditorUtility.DisplayDialog("New Graph", "Replace the current graph with the default pipeline?", "New", "Cancel"))
                return;

            LoadDefaultGraph();
        }

        private bool ImportFromPath(string path)
        {
            var json = File.ReadAllText(path);
            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out var error))
            {
                SetStatus($"Import failed: {error}");
                Debug.LogError($"[PCG] Graph import failed: {error}");
                return false;
            }

            m_GraphView.LoadDocument(doc);
            m_CurrentFilePath = Path.GetFullPath(path);

            var assetPath = FullPathToAssetPath(m_CurrentFilePath);
            if (!string.IsNullOrEmpty(assetPath))
            {
                m_Selected = AssetDatabase.AssetPathToGUID(assetPath);
                m_GraphView.viewDataKey = m_Selected;
            }

            m_GraphLoaded = true;
            UpdateTitle();
            SetStatus($"Imported: {path}");
            return true;
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
                PcgExtension);

            if (string.IsNullOrEmpty(path))
                return;

            File.WriteAllText(path, json);
            m_CurrentFilePath = Path.GetFullPath(path);

            var assetPath = FullPathToAssetPath(m_CurrentFilePath);
            if (!string.IsNullOrEmpty(assetPath))
            {
                AssetDatabase.Refresh();
                m_Selected = AssetDatabase.AssetPathToGUID(assetPath);
                m_GraphView.viewDataKey = m_Selected;
            }
            else
            {
                m_Selected = null;
            }

            m_GraphLoaded = true;
            UpdateTitle();
            AssetDatabase.Refresh();
            SetStatus($"Saved: {path}");
        }

        private void UpdateTitle()
        {
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            string title;
            if (!string.IsNullOrEmpty(m_Selected))
            {
                var assetPath = AssetDatabase.GUIDToAssetPath(m_Selected);
                title = string.IsNullOrEmpty(assetPath)
                    ? "PCG Graph"
                    : $"PCG Graph — {Path.GetFileName(assetPath)}";
            }
            else if (!string.IsNullOrEmpty(m_CurrentFilePath))
            {
                title = $"PCG Graph — {Path.GetFileName(m_CurrentFilePath)}*";
            }
            else
            {
                title = "PCG Graph";
            }

            titleContent = new GUIContent(title, icon);
        }

        private void SetStatus(string message)
        {
            Debug.Log($"[PCG] {message}");
        }

        private void LocateInProject()
        {
            if (string.IsNullOrEmpty(m_Selected))
            {
                Debug.LogWarning("[PCG] No project asset loaded to locate.");
                return;
            }

            var assetPath = AssetDatabase.GUIDToAssetPath(m_Selected);
            var asset = AssetDatabase.LoadMainAssetAtPath(assetPath);
            if (asset != null)
                EditorGUIUtility.PingObject(asset);
            else
                Debug.LogWarning($"[PCG] Asset not found: {assetPath}");
        }

        private void ToggleBlackboard()
        {
            var visible = m_Blackboard.style.display.value == DisplayStyle.Flex;
            m_Blackboard.style.display = visible ? DisplayStyle.None : DisplayStyle.Flex;
        }

        private void ToggleInspector()
        {
            m_Inspector.ToggleVisible();
        }

        private static string AssetPathToFullPath(string assetPath)
        {
            var projectRoot = Path.GetDirectoryName(Application.dataPath);
            if (string.IsNullOrEmpty(projectRoot))
                return null;

            return Path.GetFullPath(Path.Combine(projectRoot, assetPath.Replace('/', Path.DirectorySeparatorChar)));
        }

        private static string FullPathToAssetPath(string fullPath)
        {
            var normalizedFull = Path.GetFullPath(fullPath).Replace('\\', '/');
            var dataPath = Application.dataPath.Replace('\\', '/');
            if (!normalizedFull.StartsWith(dataPath))
                return null;

            return "Assets" + normalizedFull.Substring(dataPath.Length);
        }
    }
}
