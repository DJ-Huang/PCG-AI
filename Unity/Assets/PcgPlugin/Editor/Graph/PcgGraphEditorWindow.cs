using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechEditor.PCG;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgGraphEditorWindow : EditorWindow
    {
        private const string MenuPath = "PCG/Graph Editor";
        private const string IconPath = "Assets/PcgPlugin/Editor/Icons/pcg-icon-16.png";
        private const string PcgExtension = "pcg";
        private const string SubgraphExtension = "pcgsubgraph";

        private bool m_SubgraphAssetMode;

        public bool IsSubgraphAssetMode => m_SubgraphAssetMode;

        /// <summary>Asset GUID of the open .pcg file (Shader Graph: m_Selected).</summary>
        [SerializeField]
        private string m_Selected;

        private PcgGraphView m_GraphView;
        private PcgGraphBlackboard m_Blackboard;
        private PcgSubgraphInterfacePanel m_InterfacePanel;
        private PcgNodeInspector m_Inspector;
        private Button m_BlackboardToggle;
        private Button m_InspectorToggle;
        private Button m_SubgraphBackButton;
        private Label m_SubgraphBreadcrumb;
        private EnumField m_ScatterDisplayField;
        private string m_CurrentFilePath;
        private bool m_GraphLoaded;

        private FileSystemWatcher _fileWatcher;
        private bool _pendingExternalReload;
        private DateTime _lastWriteUtc = DateTime.MinValue;
        private DateTime _lastSelfSaveUtc = DateTime.MinValue;
        private const double SelfSaveIgnoreSeconds = 1.0;

        [SerializeField]
        private List<PcgPreviewMeshBinding> m_PreviewMeshBindings = new();

        [SerializeField]
        private string m_PreviewNodeId;

        [SerializeField]
        private string m_PreviewNodeLabel;

        [SerializeField]
        private string m_PreviewScopeSubgraphId;

        [SerializeField]
        private string[] m_PreviewInstanceChain;

        private Label m_PreviewStatusLabel;
        private Button m_ClearPreviewButton;

        public string selectedGuid => m_Selected;

        public string PreviewNodeId => m_PreviewNodeId;

        public string PreviewScopeSubgraphId => m_PreviewScopeSubgraphId;

        public IReadOnlyList<string> PreviewInstanceChain =>
            m_PreviewInstanceChain ?? System.Array.Empty<string>();

        public string PreviewNodeLabel =>
            string.IsNullOrEmpty(m_PreviewNodeLabel) ? m_PreviewNodeId : m_PreviewNodeLabel;

        public bool HasLoadedGraph => m_GraphLoaded && m_GraphView != null;

        internal PcgGraphView GraphView => m_GraphView;

        public string CurrentAssetPath => string.IsNullOrEmpty(m_CurrentFilePath) ? null : FullPathToAssetPath(m_CurrentFilePath);

        public IReadOnlyList<PcgPreviewMeshBinding> PreviewMeshBindings => m_PreviewMeshBindings;

        public PcgGraphDocument ExportLiveDocument() => m_GraphView?.ExportDocument();

        internal PcgExternalSubgraphLoader CreateExternalSubgraphLoader()
        {
            var diskLoader = PcgExecutionDocumentBuilder.CreateEditorAssetDatabaseLoader();
            return (string assetGuid, out string sourceJson, out string loadError) =>
            {
                sourceJson = null;
                loadError = null;
                if (m_GraphView != null &&
                    m_GraphView.TryGetLiveExternalSubgraphSourceJson(
                        assetGuid,
                        out sourceJson,
                        out loadError))
                {
                    return true;
                }

                if (!string.IsNullOrEmpty(loadError))
                    return false;

                return diskLoader(assetGuid, out sourceJson, out loadError);
            };
        }

        public bool MatchesGraphAsset(string assetDatabasePath, string assetGuid)
        {
            if (!string.IsNullOrEmpty(assetGuid) && selectedGuid == assetGuid)
                return true;

            if (string.IsNullOrEmpty(assetDatabasePath) || string.IsNullOrEmpty(m_CurrentFilePath))
                return false;

            var windowAssetPath = FullPathToAssetPath(m_CurrentFilePath);
            return string.Equals(windowAssetPath, assetDatabasePath, System.StringComparison.OrdinalIgnoreCase);
        }

        public void ToggleNodePreview(
            string nodeId,
            string nodeType,
            string displayTitle,
            string scopeSubgraphId = null,
            string[] instanceChain = null)
        {
            if (string.IsNullOrEmpty(nodeId))
                return;

            if (m_PreviewNodeId == nodeId &&
                string.Equals(m_PreviewScopeSubgraphId, scopeSubgraphId, System.StringComparison.Ordinal))
            {
                ClearNodePreview();
                return;
            }

            var label = string.IsNullOrEmpty(displayTitle) ? nodeType : $"{displayTitle} ({nodeType})";
            if (!string.IsNullOrEmpty(scopeSubgraphId))
                label = $"{label} @ {scopeSubgraphId}";
            SetPreviewNode(nodeId, label, scopeSubgraphId, instanceChain);
        }

        public void SetPreviewNode(
            string nodeId,
            string label,
            string scopeSubgraphId = null,
            string[] instanceChain = null)
        {
            if (string.IsNullOrEmpty(nodeId))
                return;

            m_PreviewNodeId = nodeId;
            m_PreviewNodeLabel = label;
            m_PreviewScopeSubgraphId = scopeSubgraphId;
            m_PreviewInstanceChain = instanceChain != null
                ? (string[])instanceChain.Clone()
                : System.Array.Empty<string>();
            OnPreviewNodeChanged();
        }

        public void ClearNodePreview(bool silent = false)
        {
            if (string.IsNullOrEmpty(m_PreviewNodeId))
                return;

            m_PreviewNodeId = null;
            m_PreviewNodeLabel = null;
            m_PreviewScopeSubgraphId = null;
            m_PreviewInstanceChain = null;
            OnPreviewNodeChanged(silent);
        }

        public void ValidatePreviewNodeExists()
        {
            if (string.IsNullOrEmpty(m_PreviewNodeId) || m_GraphView == null)
                return;

            // Navigation only changes which scope is visible. Keep previewing the
            // original scoped node while drilling into or out of a Subgraph.
            if (!m_GraphView.ContainsNodeInScope(m_PreviewNodeId, m_PreviewScopeSubgraphId))
                ClearNodePreview(silent: true);
        }

        private void OnPreviewNodeChanged(bool silent = false)
        {
            m_GraphView?.RefreshNodePreviewVisuals();
            RefreshPreviewToolbar();

            // Mark the old request obsolete without joining its native worker on the
            // Editor thread. Keep both caches: the managed cache is keyed by complete
            // preview JSON, while the native content-addressed cache can reuse unchanged
            // upstream nodes when only the preview sink changes.
            PcgGraphEditorCookBridge.CancelPreviewCooksForWindow(this);
            PcgGraphEditorCookBridge.NotifyGraphChanged(this, immediate: true);

            if (!silent)
            {
                if (string.IsNullOrEmpty(m_PreviewNodeId))
                    Debug.Log("[PCG] Node preview cleared — showing full graph output.");
                else
                    Debug.Log($"[PCG] Node preview enabled: {PreviewNodeLabel}");
            }
        }

        internal void RefreshPreviewToolbar()
        {
            if (m_PreviewStatusLabel == null || m_ClearPreviewButton == null)
                return;

            var hasPreview = !string.IsNullOrEmpty(m_PreviewNodeId);
            m_PreviewStatusLabel.style.display = hasPreview ? DisplayStyle.Flex : DisplayStyle.None;
            m_ClearPreviewButton.style.display = hasPreview ? DisplayStyle.Flex : DisplayStyle.None;

            if (hasPreview)
                m_PreviewStatusLabel.text = $"Preview: {PreviewNodeLabel}";
        }

        public void SetPreviewMeshFilter(string bindingKey, MeshFilter filter)
        {
            if (string.IsNullOrEmpty(bindingKey))
                bindingKey = "targetMesh";

            foreach (var binding in m_PreviewMeshBindings)
            {
                if (binding != null && binding.bindingKey == bindingKey)
                {
                    binding.previewMeshFilter = filter;
                    return;
                }
            }

            m_PreviewMeshBindings.Add(new PcgPreviewMeshBinding
            {
                bindingKey = bindingKey,
                previewMeshFilter = filter,
            });
        }

        [MenuItem(MenuPath)]
        public static void Open()
        {
            var window = CreateWindow<PcgGraphEditorWindow>();
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

            if (!assetPath.EndsWith("." + PcgExtension, System.StringComparison.OrdinalIgnoreCase) &&
                !assetPath.EndsWith("." + SubgraphExtension, System.StringComparison.OrdinalIgnoreCase))
                return false;

            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window.selectedGuid == guid)
                {
                    window.Focus();
                    return true;
                }
            }

            var editor = CreateWindow<PcgGraphEditorWindow>();
            var icon = AssetDatabase.LoadAssetAtPath<Texture2D>(IconPath);
            editor.titleContent = new GUIContent("PCG Graph", icon);
            editor.Initialize(guid);
            editor.Show();
            editor.Focus();
            return true;
        }

        public void Initialize(string assetGuid)
        {
            if (string.IsNullOrEmpty(assetGuid))
                return;

            var asset = AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(AssetDatabase.GUIDToAssetPath(assetGuid));
            if (asset == null || !EditorUtility.IsPersistent(asset))
                return;

            if (m_Selected == assetGuid && m_GraphLoaded)
                return;

            var assetPath = AssetDatabase.GUIDToAssetPath(assetGuid);
            if (string.IsNullOrEmpty(assetPath))
                return;

            var extension = Path.GetExtension(assetPath);
            if (string.IsNullOrEmpty(extension))
                return;
            var ext = extension.Substring(1);
            var isGraph = ext.Equals(PcgExtension, System.StringComparison.OrdinalIgnoreCase);
            var isSubgraphAsset = ext.Equals(SubgraphExtension, System.StringComparison.OrdinalIgnoreCase);
            if (!isGraph && !isSubgraphAsset)
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
            DisposeFileWatcher();

            if (m_GraphView != null)
            {
                foreach (var node in m_GraphView.nodes.ToList())
                {
                    if (node is PcgGraphNodeBase graphNode)
                        graphNode.DetachOverlays();
                }

                m_GraphView.DestroyUndoState();
            }

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

            if (_pendingExternalReload)
            {
                _pendingExternalReload = false;
                ReloadFromDisk();
            }
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
            toolbar.Add(MakeButton("Show in Project", LocateInProject));

            m_ClearPreviewButton = MakeButton("Clear Preview", () => ClearNodePreview());
            m_ClearPreviewButton.style.display = DisplayStyle.None;
            toolbar.Add(m_ClearPreviewButton);

            m_SubgraphBackButton = MakeButton("‹ Root", () => m_GraphView?.ExitSubgraph());
            m_SubgraphBackButton.style.display = DisplayStyle.None;
            toolbar.Add(m_SubgraphBackButton);
            m_SubgraphBreadcrumb = new Label("Root")
            {
                style =
                {
                    unityTextAlign = TextAnchor.MiddleLeft,
                    marginLeft = 4,
                    marginRight = 8,
                    color = new Color(0.65f, 0.8f, 1f),
                },
            };
            toolbar.Add(m_SubgraphBreadcrumb);

            var spacer = new VisualElement { style = { flexGrow = 1 } };
            toolbar.Add(spacer);

            m_ScatterDisplayField = new EnumField(
                "Scatter Display",
                PcgScatterDisplayMode.MergedMesh);
            m_ScatterDisplayField.tooltip =
                "How scatter points render on scene PcgGraphComponent(s) using this graph. " +
                "GPU Instancing is faster for many instances.";
            m_ScatterDisplayField.style.marginRight = 8;
            m_ScatterDisplayField.style.minWidth = 220;
            m_ScatterDisplayField.RegisterValueChangedCallback(OnScatterDisplayChanged);
            toolbar.Add(m_ScatterDisplayField);
            RefreshScatterDisplayField();

            m_BlackboardToggle = MakeButton("Parameters", ToggleBlackboard);
            toolbar.Add(m_BlackboardToggle);

            m_InspectorToggle = MakeButton("Inspector", ToggleInspector);
            toolbar.Add(m_InspectorToggle);

            rootVisualElement.Add(toolbar);
        }

        private void OnScatterDisplayChanged(ChangeEvent<System.Enum> evt)
        {
            if (evt.newValue is PcgScatterDisplayMode mode)
                PcgGraphEditorScatterUtil.SetDisplayMode(this, mode);
        }

        internal void RefreshScatterDisplayField()
        {
            if (m_ScatterDisplayField == null)
                return;

            m_ScatterDisplayField.SetValueWithoutNotify(PcgGraphEditorScatterUtil.GetDisplayMode(this));
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
            m_GraphView.SceneContextChanged += _ => m_GraphView.ScheduleInspectorRefresh();
            m_GraphView.SubgraphNavigationChanged += _ =>
            {
                RefreshSubgraphBreadcrumb();
                RefreshInterfacePanel();
                ValidatePreviewNodeExists();
            };
            if (!string.IsNullOrEmpty(m_Selected))
                m_GraphView.viewDataKey = m_Selected;

            m_Blackboard = new PcgGraphBlackboard(m_GraphView);
            m_GraphView.Blackboard = m_Blackboard;
            m_Blackboard.style.display = DisplayStyle.None;

            m_InterfacePanel = new PcgSubgraphInterfacePanel(m_GraphView);
            m_InterfacePanel.style.display = DisplayStyle.None;

            m_Inspector = new PcgNodeInspector(m_GraphView, m_Blackboard);
            m_GraphView.Inspector = m_Inspector;

            m_Blackboard.OnParametersChanged += () =>
            {
                m_Inspector.OnSelectionChanged();
                m_GraphView.NotifyDocumentChanged();
            };

            var graphHost = new VisualElement
            {
                style =
                {
                    flexGrow = 1,
                    position = Position.Relative,
                },
            };
            graphHost.Add(m_GraphView);

            // Status tip over the graph (not toolbar) so long preview names don't steal chrome space.
            m_PreviewStatusLabel = new Label
            {
                pickingMode = PickingMode.Ignore,
                style =
                {
                    position = Position.Absolute,
                    left = 8,
                    bottom = 8,
                    maxWidth = Length.Percent(70),
                    unityTextAlign = TextAnchor.MiddleLeft,
                    color = new Color(0.55f, 0.85f, 1f),
                    display = DisplayStyle.None,
                    whiteSpace = WhiteSpace.Normal,
                    // Soft backdrop so text stays readable over dense graphs.
                    backgroundColor = new Color(0.08f, 0.1f, 0.14f, 0.72f),
                    paddingLeft = 8,
                    paddingRight = 8,
                    paddingTop = 3,
                    paddingBottom = 3,
                    borderTopLeftRadius = 4,
                    borderTopRightRadius = 4,
                    borderBottomLeftRadius = 4,
                    borderBottomRightRadius = 4,
                },
            };
            graphHost.Add(m_PreviewStatusLabel);

            contentRow.Add(m_Blackboard);
            contentRow.Add(m_InterfacePanel);
            contentRow.Add(graphHost);
            contentRow.Add(m_Inspector);

            rootVisualElement.Add(contentRow);
            RefreshPreviewToolbar();
        }

        private void RefreshSubgraphBreadcrumb()
        {
            if (m_SubgraphBackButton == null || m_SubgraphBreadcrumb == null || m_GraphView == null)
                return;
            m_SubgraphBackButton.style.display = m_GraphView.IsInsideSubgraph
                ? DisplayStyle.Flex
                : DisplayStyle.None;
            m_SubgraphBreadcrumb.text = m_GraphView.IsInsideSubgraph
                ? m_GraphView.CurrentSubgraphPath
                : "Root";
        }

        internal void RefreshInterfacePanel()
        {
            if (m_InterfacePanel == null || m_GraphView == null)
                return;

            var show = m_SubgraphAssetMode || m_GraphView.IsInsideSubgraph;
            m_InterfacePanel.style.display = show ? DisplayStyle.Flex : DisplayStyle.None;
            if (!show)
                return;

            var definition = m_GraphView.FindSubgraphDefinition(m_GraphView.CurrentSubgraphId);
            m_InterfacePanel.Bind(definition);
        }

        private void LoadDefaultGraph()
        {
            DisposeFileWatcher();
            ClearNodePreview(silent: true);
            ExitSubgraphAssetModeUi();
            m_GraphView.LoadDocument(PcgGraphDefaults.CreatePipeline());
            m_Selected = null;
            m_CurrentFilePath = null;
            m_GraphLoaded = true;
            m_GraphView.viewDataKey = "PCG.DefaultGraph";
            UpdateTitle();
            SetStatus("Ready — default 3-node pipeline loaded.");
            RefreshScatterDisplayField();
        }

        private void ExitSubgraphAssetModeUi()
        {
            m_SubgraphAssetMode = false;
            if (m_Blackboard != null)
                m_Blackboard.style.display = DisplayStyle.None;
            if (m_InterfacePanel != null)
                m_InterfacePanel.style.display = DisplayStyle.None;
        }

        private void NewGraph()
        {
            if (!EditorUtility.DisplayDialog("New Graph", "Replace the current graph with the default pipeline?", "New", "Cancel"))
                return;

            LoadDefaultGraph();
        }

        public void ReconcileExternalSubgraphAssets(HashSet<string> changedGuids)
        {
            if (!HasLoadedGraph)
                return;
            m_GraphView?.ReconcileExternalNodes(changedGuids);
        }

        private bool ImportFromPath(string path)
        {
            PcgNodeManifest.Reload();

            var json = File.ReadAllText(path);
            var isSubgraphAsset = path.EndsWith("." + SubgraphExtension, StringComparison.OrdinalIgnoreCase);
            m_SubgraphAssetMode = isSubgraphAsset;

            if (isSubgraphAsset)
            {
                if (!PcgSubgraphAssetSerializer.TryFromJson(json, out var assetDoc, out var assetError))
                {
                    SetStatus($"Import failed: {assetError}");
                    Debug.LogError($"[PCG] Subgraph asset import failed: {assetError}");
                    return false;
                }

                var definition = assetDoc.ToRootDefinition("__asset_root__");
                var wrapper = new PcgGraphDocument
                {
                    version = "3.0",
                    nodes = definition.nodes,
                    edges = definition.edges,
                    subgraphs = assetDoc.subgraphs ?? new List<PcgSubgraphDefinition>(),
                };
                // Keep interface ports on a synthetic definition for Input/Output nodes.
                wrapper.subgraphs.Insert(0, definition);
                ClearNodePreview(silent: true);
                m_GraphView.LoadSubgraphAssetDocument(wrapper, definition, assetDoc.name);
            }
            else
            {
                if (!PcgGraphSerializer.TryFromJson(json, out var doc, out var error))
                {
                    SetStatus($"Import failed: {error}");
                    Debug.LogError($"[PCG] Graph import failed: {error}");
                    return false;
                }

                ClearNodePreview(silent: true);
                m_GraphView.LoadDocument(doc);
                if (doc.HasExternalSubgraphAssets())
                    m_GraphView.ReconcileExternalNodes(null);
            }

            m_CurrentFilePath = Path.GetFullPath(path);
            SetupFileWatcher(m_CurrentFilePath);

            var assetPath = FullPathToAssetPath(m_CurrentFilePath);
            if (!string.IsNullOrEmpty(assetPath))
            {
                m_Selected = AssetDatabase.AssetPathToGUID(assetPath);
                m_GraphView.viewDataKey = m_Selected;
            }

            m_GraphLoaded = true;
            UpdateTitle();
            SetStatus($"Imported: {path}");
            RefreshScatterDisplayField();
            if (m_Blackboard != null)
                m_Blackboard.style.display = m_SubgraphAssetMode ? DisplayStyle.None : DisplayStyle.Flex;
            RefreshInterfacePanel();
            return true;
        }

        private void SaveGraph()
        {
            if (string.IsNullOrEmpty(m_CurrentFilePath))
            {
                SaveAsGraph();
                return;
            }

            string json;
            if (m_SubgraphAssetMode)
            {
                if (!m_GraphView.TryExportSubgraphAssetDocument(out var assetDoc, out var exportError))
                {
                    SetStatus($"Save failed: {exportError}");
                    Debug.LogError($"[PCG] Subgraph asset save failed: {exportError}");
                    return;
                }
                json = PcgSubgraphAssetSerializer.ToJson(assetDoc);
            }
            else
            {
                if (!m_GraphView.TryFlushExternalNavigationAssets(out var flushError))
                {
                    SetStatus($"Save failed: {flushError}");
                    Debug.LogError($"[PCG] Failed to flush linked SubgraphAsset edits: {flushError}");
                    return;
                }

                var doc = m_GraphView.ExportDocumentForAuthoringSave();
                json = PcgGraphSerializer.ToJson(doc);
            }

            _lastSelfSaveUtc = DateTime.UtcNow;
            File.WriteAllText(m_CurrentFilePath, json);
            AssetDatabase.Refresh();
            SetStatus($"Saved: {m_CurrentFilePath}");
        }

        private void SaveAsGraph()
        {
            string json;
            string defaultName;
            string extension;
            string panelTitle;

            if (m_SubgraphAssetMode)
            {
                if (!m_GraphView.TryExportSubgraphAssetDocument(out var assetDoc, out var exportError))
                {
                    SetStatus($"Save As failed: {exportError}");
                    Debug.LogError($"[PCG] Subgraph asset Save As failed: {exportError}");
                    return;
                }
                json = PcgSubgraphAssetSerializer.ToJson(assetDoc);
                defaultName = "subgraph.pcgsubgraph";
                extension = SubgraphExtension;
                panelTitle = "Save Subgraph Asset As";
            }
            else
            {
                if (!m_GraphView.TryFlushExternalNavigationAssets(out var flushError))
                {
                    SetStatus($"Save As failed: {flushError}");
                    Debug.LogError($"[PCG] Failed to flush linked SubgraphAsset edits: {flushError}");
                    return;
                }

                var doc = m_GraphView.ExportDocumentForAuthoringSave();
                json = PcgGraphSerializer.ToJson(doc);
                defaultName = "graph.pcg";
                extension = PcgExtension;
                panelTitle = "Save Graph As";
            }

            var path = EditorUtility.SaveFilePanel(
                panelTitle,
                PcgGraphRunner.DefaultSchemaDir,
                defaultName,
                extension);

            if (string.IsNullOrEmpty(path))
                return;

            _lastSelfSaveUtc = DateTime.UtcNow;
            File.WriteAllText(path, json);
            m_CurrentFilePath = Path.GetFullPath(path);
            SetupFileWatcher(m_CurrentFilePath);

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

        internal void ToggleBlackboard()
        {
            var visible = m_Blackboard.style.display.value == DisplayStyle.Flex;
            m_Blackboard.style.display = visible ? DisplayStyle.None : DisplayStyle.Flex;
        }

        internal void ToggleInspector()
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
            if (string.IsNullOrEmpty(fullPath))
                return null;
            var normalizedFull = Path.GetFullPath(fullPath).Replace('\\', '/');
            var dataPath = Application.dataPath.Replace('\\', '/');
            if (!normalizedFull.StartsWith(dataPath))
                return null;

            return "Assets" + normalizedFull.Substring(dataPath.Length);
        }

        private void SetupFileWatcher(string fullPath)
        {
            DisposeFileWatcher();

            if (string.IsNullOrEmpty(fullPath) || !File.Exists(fullPath))
                return;

            var dir = Path.GetDirectoryName(fullPath);
            var file = Path.GetFileName(fullPath);
            if (string.IsNullOrEmpty(dir) || string.IsNullOrEmpty(file))
                return;

            _fileWatcher = new FileSystemWatcher(dir, file)
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName,
                EnableRaisingEvents = true,
            };

            _fileWatcher.Changed += OnWatchedFileChanged;
            _fileWatcher.Created += OnWatchedFileChanged;
            _fileWatcher.Renamed += OnWatchedFileChanged;
        }

        private void OnWatchedFileChanged(object sender, FileSystemEventArgs e)
        {
            var writeUtc = SafeGetLastWriteUtc(e.FullPath);
            if (writeUtc <= _lastWriteUtc)
                return;

            if ((writeUtc - _lastSelfSaveUtc).TotalSeconds < SelfSaveIgnoreSeconds)
                return;

            _lastWriteUtc = writeUtc;
            _pendingExternalReload = true;
        }

        private static DateTime SafeGetLastWriteUtc(string path)
        {
            try
            {
                return File.Exists(path) ? File.GetLastWriteTimeUtc(path) : DateTime.UtcNow;
            }
            catch
            {
                return DateTime.UtcNow;
            }
        }

        private void ReloadFromDisk()
        {
            if (string.IsNullOrEmpty(m_CurrentFilePath) || !File.Exists(m_CurrentFilePath))
            {
                DisposeFileWatcher();
                return;
            }

            ImportFromPath(m_CurrentFilePath);
            Debug.Log($"[PCG] Graph reloaded from disk: {m_CurrentFilePath}");
        }

        private void DisposeFileWatcher()
        {
            if (_fileWatcher == null)
                return;

            _fileWatcher.EnableRaisingEvents = false;
            _fileWatcher.Changed -= OnWatchedFileChanged;
            _fileWatcher.Created -= OnWatchedFileChanged;
            _fileWatcher.Renamed -= OnWatchedFileChanged;
            _fileWatcher.Dispose();
            _fileWatcher = null;
        }
    }
}
