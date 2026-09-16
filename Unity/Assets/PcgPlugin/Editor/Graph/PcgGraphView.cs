using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechEditor.PCG;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public enum SceneEditLevel
    {
        Object,
        Component,
    }

    public enum SceneEditDomain
    {
        None,
        SplineControlPoint,
        Vertex,
        Edge,
        Face,
    }

    public readonly struct PcgSceneEditContext
    {
        public readonly SceneEditLevel Level;
        public readonly SceneEditDomain Domain;
        public readonly string ActiveNodeId;
        public readonly SceneEditDomain[] SupportedDomains;

        public PcgSceneEditContext(
            SceneEditLevel level,
            SceneEditDomain domain,
            string activeNodeId,
            SceneEditDomain[] supportedDomains)
        {
            Level = level;
            Domain = domain;
            ActiveNodeId = activeNodeId;
            SupportedDomains = supportedDomains ?? System.Array.Empty<SceneEditDomain>();
        }

        public static PcgSceneEditContext ObjectMode => new(
            SceneEditLevel.Object, SceneEditDomain.None, null, System.Array.Empty<SceneEditDomain>());

        public bool IsObjectMode => Level == SceneEditLevel.Object;
        public bool IsComponentMode => Level == SceneEditLevel.Component;

        public bool SupportsDomain(SceneEditDomain domain) =>
            System.Array.IndexOf(SupportedDomains, domain) >= 0;
    }

    public sealed class PcgGraphView : GraphView
    {
        private readonly PcgGraphSearchWindow m_SearchWindow;
        private PcgGraphState m_State;
        private int m_EdgeCounter = 100;
        private EditorWindow m_HostWindow;
        private Vector2 m_LastMousePos;
        private PcgGraphBlackboard m_Blackboard;
        private PcgNodeInspector m_Inspector;
        private bool m_SuppressUndo;
        private string m_PendingSnapshot;
        private string m_PendingAction;
        private bool m_PendingCommit;
        private PcgGraphNodeBase m_ActiveRadialMenuNode;
        private PcgNodeInfoPanel m_InfoPanel;
        private Dictionary<string, PcgNodeMeshStats> m_NodeMeshStats = new();
        private Dictionary<string, List<NodeGroupEntry>> m_NodeGroups = new();
        private Dictionary<string, List<NodeAttrEntry>> m_NodeAttrs = new();
        private string m_LastStatsJson;
        private PcgGraphDocument m_RootDocument;
        private string m_CurrentSubgraphId;
        private string m_CurrentSubgraphInstanceId;
        private readonly List<string> m_SubgraphParents = new();
        private readonly List<string> m_SubgraphParentInstances = new();
        /// <summary>Root external nav definition id → asset GUID (session cache for drill-in / flush).</summary>
        private readonly Dictionary<string, string> m_ExternalNavRootGuidByDefId = new(StringComparer.Ordinal);
        private readonly HashSet<string> m_DirtyExternalNavDefIds = new(StringComparer.Ordinal);
        private readonly Dictionary<string, string> m_ExternalNavLoadedContentHashByDefId = new(StringComparer.Ordinal);

        public static event Action<PcgGraphEditorWindow> GraphDocumentChanged;
        public event Action<string> SubgraphNavigationChanged;

        private static readonly SceneEditDomain[] s_SplineDomains = { SceneEditDomain.SplineControlPoint };
        private static readonly SceneEditDomain[] s_GroupDomains = { SceneEditDomain.Vertex, SceneEditDomain.Edge, SceneEditDomain.Face };

        private PcgSceneEditContext m_SceneEditContext = PcgSceneEditContext.ObjectMode;
        private bool m_DuplicateInProgress;
        private bool m_InspectorRefreshScheduled;
        private readonly Dictionary<string, PcgInterfaceInputAnchorView> m_InterfaceInputAnchors = new();
        private readonly Dictionary<string, PcgInterfaceOutputAnchorView> m_InterfaceOutputAnchors = new();

        public PcgSceneEditContext SceneEditContext => m_SceneEditContext;

        public event Action<PcgSceneEditContext> SceneContextChanged;

        public PcgGraphState State => m_State;

        public EditorWindow HostWindow => m_HostWindow;
        public bool IsInsideSubgraph => !string.IsNullOrEmpty(m_CurrentSubgraphId);
        public string CurrentSubgraphId => m_CurrentSubgraphId;
        public string CurrentSubgraphInstanceId => m_CurrentSubgraphInstanceId;
        public string CurrentSubgraphName => FindSubgraph(m_CurrentSubgraphId)?.name ?? "Root";

        /// <summary>Instance node ids from root down to the currently opened subgraph instance.</summary>
        public string[] GetSubgraphInstanceChain()
        {
            if (!IsInsideSubgraph)
                return System.Array.Empty<string>();
            var chain = new List<string>(m_SubgraphParentInstances.Count + 1);
            chain.AddRange(m_SubgraphParentInstances);
            if (!string.IsNullOrEmpty(m_CurrentSubgraphInstanceId))
                chain.Add(m_CurrentSubgraphInstanceId);
            return chain.ToArray();
        }

        public string CurrentSubgraphPath
        {
            get
            {
                var names = m_SubgraphParents
                    .Select(id => FindSubgraph(id)?.name ?? id)
                    .Concat(IsInsideSubgraph ? new[] { CurrentSubgraphName } : Array.Empty<string>());
                return string.Join(" / ", new[] { "Root" }.Concat(names));
            }
        }

        public PcgGraphBlackboard Blackboard
        {
            get => m_Blackboard;
            set => m_Blackboard = value;
        }

        public PcgNodeInspector Inspector
        {
            get => m_Inspector;
            set => m_Inspector = value;
        }

        /// <summary>Called by radial menu "i" button — shows read-only Node Info panel.</summary>
        public void ShowNodeInfoPanel(PcgGraphNodeBase node)
        {
            if (m_InfoPanel == null)
            {
                m_InfoPanel = new PcgNodeInfoPanel(this);
                contentViewContainer.Add(m_InfoPanel);
            }
            m_InfoPanel.Show(node);
        }

        /// <summary>Hides the Node Info panel if visible.</summary>
        public void HideNodeInfoPanel()
        {
            m_InfoPanel?.Hide();
        }

        internal bool TryGetNodeMeshStats(string nodeId, out PcgNodeMeshStats stats)
        {
            var json = ResolveCookResultJson();
            var cacheKey = (json ?? "") + "\u001f" + PcgGraphExecutionBridge.LastOutputStatsAliasesVersion;
            if (cacheKey != m_LastStatsJson)
            {
                m_LastStatsJson = cacheKey;
                m_NodeMeshStats.Clear();
                m_NodeGroups.Clear();
                m_NodeAttrs.Clear();
                if (!string.IsNullOrEmpty(json))
                {
                    try
                    {
                        var wrapper = JsonUtility.FromJson<NodeStatsWrapper>(json);
                        if (wrapper?.node_stats != null)
                        {
                            foreach (var entry in wrapper.node_stats)
                            {
                                m_NodeMeshStats[entry.node_id] = new PcgNodeMeshStats
                                {
                                    pointCount = entry.point_count,
                                    faceCount = entry.face_count,
                                    vertexCount = entry.vertex_count,
                                    triangleCount = entry.triangle_count,
                                    hasBBox = entry.has_bbox,
                                    bboxMin = new Vector3(entry.bbox_min_x, entry.bbox_min_y, entry.bbox_min_z),
                                    bboxMax = new Vector3(entry.bbox_max_x, entry.bbox_max_y, entry.bbox_max_z),
                                };
                            }
                        }
                        if (wrapper?.node_groups != null)
                        {
                            foreach (var g in wrapper.node_groups)
                            {
                                if (!m_NodeGroups.ContainsKey(g.node_id))
                                    m_NodeGroups[g.node_id] = new List<NodeGroupEntry>();
                                m_NodeGroups[g.node_id].Add(g);
                            }
                        }
                        if (wrapper?.node_attrs != null)
                        {
                            foreach (var a in wrapper.node_attrs)
                            {
                                if (string.IsNullOrEmpty(a.node_id))
                                    continue;
                                if (!m_NodeAttrs.ContainsKey(a.node_id))
                                    m_NodeAttrs[a.node_id] = new List<NodeAttrEntry>();
                                m_NodeAttrs[a.node_id].Add(a);
                            }
                        }
                    }
                    catch { /* JSON shape mismatch — silently skip */ }
                }

                ApplyOutputStatsAliases(
                    PcgGraphExecutionBridge.LastOutputStatsAliases,
                    m_NodeMeshStats,
                    m_NodeGroups,
                    m_NodeAttrs);
            }
            return m_NodeMeshStats.TryGetValue(nodeId, out stats);
        }

        /// <summary>
        /// Subgraph instances are flattened away before cook, so the cook result has no stats
        /// under the instance node id. Alias each instance to the flat node that sourced its
        /// first output (Houdini shows the subnet output's geometry info on the subnet node).
        /// </summary>
        public static void ApplyOutputStatsAliases(
            System.Collections.Generic.IReadOnlyDictionary<string, string> aliases,
            Dictionary<string, PcgNodeMeshStats> nodeMeshStats,
            Dictionary<string, List<NodeGroupEntry>> nodeGroups,
            Dictionary<string, List<NodeAttrEntry>> nodeAttrs)
        {
            if (aliases == null)
                return;

            foreach (var pair in aliases)
            {
                var source = ResolveStatsAliasSource(aliases, pair.Key, nodeMeshStats);
                if (source == null)
                    continue;
                if (!nodeMeshStats.ContainsKey(pair.Key) &&
                    nodeMeshStats.TryGetValue(source, out var stats))
                    nodeMeshStats[pair.Key] = stats;
                if (!nodeGroups.ContainsKey(pair.Key) &&
                    nodeGroups.TryGetValue(source, out var groups))
                    nodeGroups[pair.Key] = groups;
                if (!nodeAttrs.ContainsKey(pair.Key) &&
                    nodeAttrs.TryGetValue(source, out var attrs))
                    nodeAttrs[pair.Key] = attrs;
            }
        }

        /// <summary>
        /// Follows alias chains (e.g. interface Output anchor → Subgraph instance → flat
        /// internal node) and returns the first target that has cook stats; falls back to
        /// the chain end so groups/attrs can still resolve there.
        /// </summary>
        private static string ResolveStatsAliasSource(
            System.Collections.Generic.IReadOnlyDictionary<string, string> aliases,
            string key,
            Dictionary<string, PcgNodeMeshStats> nodeMeshStats)
        {
            if (!aliases.TryGetValue(key, out var target) || string.IsNullOrEmpty(target))
                return null;

            var visited = new HashSet<string> { key };
            var current = target;
            string last = null;
            while (current != null && visited.Add(current))
            {
                last = current;
                if (nodeMeshStats.ContainsKey(current))
                    return current;
                if (!aliases.TryGetValue(current, out var next) || next == current)
                    break;
                current = next;
            }

            return last;
        }

        internal bool TryGetNodeGroups(string nodeId, out List<NodeGroupEntry> groups)
        {
            TryGetNodeMeshStats(nodeId, out _);
            return m_NodeGroups.TryGetValue(nodeId, out groups);
        }

        internal bool TryGetNodeAttrs(string nodeId, out List<NodeAttrEntry> attrs)
        {
            TryGetNodeMeshStats(nodeId, out _);
            return m_NodeAttrs.TryGetValue(nodeId, out attrs);
        }

        private string ResolveCookResultJson()
        {
            if (m_HostWindow is PcgGraphEditorWindow window)
            {
                var assetPath = window.CurrentAssetPath;
                if (!string.IsNullOrEmpty(assetPath))
                {
                    foreach (var component in UnityEngine.Object.FindObjectsOfType<PcgGraphComponent>())
                    {
                        if (component == null || component.GraphAsset == null)
                            continue;
                        var componentPath = UnityEditor.AssetDatabase.GetAssetPath(component.GraphAsset);
                        if (componentPath == assetPath)
                            return component.LastCookResultJson;
                    }
                }
            }
            return null;
        }

        public PcgGraphView()
        {
            m_SearchWindow = ScriptableObject.CreateInstance<PcgGraphSearchWindow>();
            m_SearchWindow.Initialize(this);
            m_State = PcgGraphState.Create();

            style.flexGrow = 1;
            // DefaultMinScale is 0.25; large graphs need deeper zoom-out for overview.
            SetupZoom(0.05f, 4f);
            this.AddManipulator(new ContentDragger());
            this.AddManipulator(new SelectionDragger());
            this.AddManipulator(new RectangleSelector());

            var grid = new GridBackground();
            Insert(0, grid);
            grid.StretchToParentSize();

            graphViewChanged = OnGraphViewChanged;

            RegisterCallback<MouseMoveEvent>(OnMouseMove);
            RegisterCallback<KeyDownEvent>(OnKeyDown);
            RegisterCallback<KeyDownEvent>(OnDuplicateKeyDown, TrickleDown.TrickleDown);
            RegisterCallback<PointerDownEvent>(OnPointerDown, TrickleDown.TrickleDown);
            RegisterCallback<PointerUpEvent>(OnPointerUp);
            RegisterCallback<DragUpdatedEvent>(OnDragUpdated);
            RegisterCallback<DragPerformEvent>(OnDragPerform);
        }

        /// <summary>Ensure the undo proxy exists. Recreated if it was destroyed
        /// (e.g. after a prior editor window teardown).</summary>
        private PcgGraphState EnsureUndoState()
        {
            if (!m_State)
            {
                m_State = PcgGraphState.Create();
                m_State.SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
            }
            return m_State;
        }

        /// <summary>Release undo entries and destroy the proxy (editor window shutdown).</summary>
        public void DestroyUndoState()
        {
            if (!m_State)
                return;
            m_State.Destroy();
            m_State = null;
        }

        public void SetHostWindow(EditorWindow window) => m_HostWindow = window;

        private void OnKeyDown(KeyDownEvent evt)
        {
            if (evt.target is not PcgGraphView)
                return;

            // Ignore shortcuts when typing in a text field
            if (evt.target is TextField || (evt.target as VisualElement)?.GetFirstAncestorOfType<TextField>() != null)
                return;

            if (evt.keyCode == KeyCode.Space)
            {
                ShowSearchWindow(m_LastMousePos);
                evt.StopPropagation();
            }
            else if (evt.keyCode == KeyCode.F)
            {
                var selected = selection.OfType<GraphElement>();
                if (selected.Any())
                {
                    var bounds = Rect.MinMaxRect(float.MaxValue, float.MaxValue, float.MinValue, float.MinValue);
                    foreach (var elem in selected)
                    {
                        var r = elem.GetPosition();
                        bounds = Rect.MinMaxRect(
                            Mathf.Min(bounds.xMin, r.xMin),
                            Mathf.Min(bounds.yMin, r.yMin),
                            Mathf.Max(bounds.xMax, r.xMax),
                            Mathf.Max(bounds.yMax, r.yMax));
                    }
                    var viewportSize = layout.size;
                    float scaleX = viewportSize.x / bounds.width;
                    float scaleY = viewportSize.y / bounds.height;
                    float scale = Mathf.Min(scaleX, scaleY) * 0.8f;
                    scale = Mathf.Clamp(scale, 0.05f, 4f);
                    var viewPos = viewportSize * 0.5f - bounds.center * scale;
                    UpdateViewTransform(viewPos, Vector3.one * scale);
                }
                else
                    FrameAll();
                evt.StopPropagation();
            }
            else if (evt.keyCode == KeyCode.P)
            {
                (m_HostWindow as PcgGraphEditorWindow)?.ToggleBlackboard();
                evt.StopPropagation();
            }
            else if (evt.keyCode == KeyCode.I)
            {
                (m_HostWindow as PcgGraphEditorWindow)?.ToggleInspector();
                evt.StopPropagation();
            }
            else if (evt.keyCode == KeyCode.Escape)
            {
                HideNodeInfoPanel();
            }
        }

        private void OnDuplicateKeyDown(KeyDownEvent evt)
        {
            if (evt.keyCode != KeyCode.D || !(evt.ctrlKey || evt.commandKey))
                return;

            if (evt.target is TextField ||
                (evt.target as VisualElement)?.GetFirstAncestorOfType<TextField>() != null)
            {
                return;
            }

            if (!CanDuplicateSelectedNodes())
                return;

            DuplicateSelectedNodes();
            evt.StopPropagation();
        }

        public override void BuildContextualMenu(ContextualMenuPopulateEvent evt)
        {
            base.BuildContextualMenu(evt);

            if (evt.target is VisualElement ve)
            {
                var nodeView = ve.GetFirstAncestorOfType<PcgGraphNodeBase>()
                             ?? (ve as PcgGraphNodeBase);
                if (nodeView != null && nodeView is not PcgInterfaceAnchorNodeBase)
                {
                    evt.menu.AppendAction(
                        "Rename",
                        _ => nodeView.RequestRename());

                    evt.menu.AppendAction(
                        "Duplicate",
                        _ => DuplicateSelectedNodes(),
                        CanDuplicateSelectedNodes()
                            ? DropdownMenuAction.Status.Normal
                            : DropdownMenuAction.Status.Disabled);

                    evt.menu.AppendAction(
                        "Preview in Scene",
                        _ => ToggleNodePreview(nodeView));

                    evt.menu.AppendAction(
                        "Copy Raw Data",
                        _ =>
                        {
                            var doc = new PcgGraphDocument { version = "1.0" };
                            var rect = nodeView.GetPosition();
                            doc.nodes.Add(new PcgGraphNodeRecord
                            {
                                id = nodeView.NodeId,
                                type = nodeView.NodeType,
                                position = PcgGraphPosition.FromVector2(rect.position),
                                data = nodeView.CollectData(),
                            });
                            var json = PcgGraphSerializer.ToJson(doc, pretty: true);
                            GUIUtility.systemCopyBuffer = json;
                        });
                }
            }

            if (selection.OfType<PcgGraphNodeBase>().Any(node =>
                    node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput" &&
                    node.NodeType != "Output" &&
                    node.NodeType != PcgStructuralNodeTypes.SubgraphParentRef))
            {
                evt.menu.AppendSeparator();
                evt.menu.AppendAction("Create Subgraph from Selection", _ => CreateSubgraphFromSelection());
                evt.menu.AppendAction("Create Subgraph Asset from Selection", _ => CreateSubgraphAssetFromSelection());
            }

            AppendPromoteSubgraphInputMenu(evt);
            AppendPromoteInlineSubgraphToAssetMenu(evt);
            AppendSubgraphInterfaceConnectMenu(evt);
        }

        private bool IsEditingSubgraphInterface() =>
            IsInsideSubgraph ||
            (m_HostWindow is PcgGraphEditorWindow host && host.IsSubgraphAssetMode);

        private PcgSubgraphDefinition GetActiveInterfaceDefinition() =>
            FindSubgraph(m_CurrentSubgraphId) ??
            m_RootDocument?.subgraphs?.FirstOrDefault();

        private void AppendSubgraphInterfaceConnectMenu(ContextualMenuPopulateEvent evt)
        {
            if (!IsEditingSubgraphInterface())
                return;

            var definition = GetActiveInterfaceDefinition();
            if (definition == null)
                return;

            var inputs = definition.inputs?.Where(p => p != null).ToList() ?? new List<PcgSubgraphPort>();
            if (inputs.Count == 0)
                return;

            evt.menu.AppendSeparator();
            foreach (var port in inputs)
            {
                var captured = port;
                evt.menu.AppendAction(
                    $"Subgraph Interface/Input: {captured.name}",
                    _ => SpawnInterfaceInputConnector(captured));
            }

        }

        private void AppendPromoteInlineSubgraphToAssetMenu(ContextualMenuPopulateEvent evt)
        {
            var inlineInstances = selection.OfType<PcgSubgraphNodeView>()
                .Where(node => node.NodeType == PcgStructuralNodeTypes.Subgraph &&
                               node.Kind == PcgSubgraphNodeKind.Instance)
                .ToList();
            if (inlineInstances.Count != 1 ||
                selection.OfType<PcgGraphNodeBase>().Count() != 1)
                return;

            evt.menu.AppendSeparator();
            evt.menu.AppendAction(
                "Promote Subgraph to Asset",
                _ => PromoteInlineSubgraphToAsset(inlineInstances[0]));
        }

        private void AppendPromoteSubgraphInputMenu(ContextualMenuPopulateEvent evt)
        {
            var selectedEdge = selection.OfType<Edge>().FirstOrDefault();
            if (selectedEdge?.input?.node is PcgSubgraphNodeView edgeInstance &&
                edgeInstance.NodeType == "Subgraph" &&
                selectedEdge.output?.node is PcgGraphNodeBase edgeSource &&
                edgeSource.NodeType != PcgStructuralNodeTypes.SubgraphInput &&
                edgeSource.NodeType != PcgStructuralNodeTypes.SubgraphOutput &&
                edgeSource.NodeType != PcgStructuralNodeTypes.SubgraphParentRef)
            {
                evt.menu.AppendSeparator();
                evt.menu.AppendAction(
                    "Promote Wire to Subgraph Input",
                    _ => PromoteEdgeToSubgraphInput(selectedEdge));
            }
            else if (selectedEdge?.input?.node is PcgExternalSubgraphNodeView extInstance &&
                     selectedEdge.output?.node is PcgGraphNodeBase extSource &&
                     extSource.NodeType != PcgStructuralNodeTypes.SubgraphInput &&
                     extSource.NodeType != PcgStructuralNodeTypes.SubgraphOutput &&
                     extSource.NodeType != PcgStructuralNodeTypes.SubgraphParentRef)
            {
                evt.menu.AppendSeparator();
                evt.menu.AppendAction(
                    "Promote Wire to Subgraph Input",
                    _ => PromoteEdgeToSubgraphAssetInput(selectedEdge, extInstance));
            }

            var selectedNodes = selection.OfType<PcgGraphNodeBase>().ToList();
            if (selectedNodes.Count == 2)
            {
                var sourceNode = selectedNodes.FirstOrDefault(n =>
                    n.NodeType != "Subgraph" &&
                    n.NodeType != PcgStructuralNodeTypes.SubgraphAsset &&
                    n.NodeType != PcgStructuralNodeTypes.SubgraphInput &&
                    n.NodeType != PcgStructuralNodeTypes.SubgraphOutput &&
                    n.NodeType != PcgStructuralNodeTypes.SubgraphParentRef);
                var inlineInstance = selectedNodes.FirstOrDefault(n => n.NodeType == "Subgraph") as PcgSubgraphNodeView;
                var assetInstance = selectedNodes.FirstOrDefault(n =>
                    n.NodeType == PcgStructuralNodeTypes.SubgraphAsset) as PcgExternalSubgraphNodeView;
                if (sourceNode != null && inlineInstance != null)
                {
                    evt.menu.AppendSeparator();
                    evt.menu.AppendAction(
                        "Connect as Subgraph Input",
                        _ => ConnectSelectionAsSubgraphInput(sourceNode, inlineInstance));
                }
                else if (sourceNode != null && assetInstance != null)
                {
                    evt.menu.AppendSeparator();
                    evt.menu.AppendAction(
                        "Connect as Subgraph Input",
                        _ => ConnectSelectionAsSubgraphAssetInput(sourceNode, assetInstance));
                }
            }
        }

        public void PromoteEdgeToSubgraphInput(Edge graphEdge)
        {
            if (graphEdge?.input?.node is not PcgSubgraphNodeView instance ||
                instance.NodeType != "Subgraph" ||
                graphEdge.output?.node is not PcgGraphNodeBase source)
                return;

            var definition = FindSubgraph(instance.SubgraphDefinitionId);
            if (definition == null)
                return;

            var sourceHandle = graphEdge.output.userData as string ?? graphEdge.output.portName ?? "out";
            var targetHandle = graphEdge.input.userData as string ?? graphEdge.input.portName ?? "in";
            var pinType = ResolveVisiblePinType(source.NodeId, sourceHandle, output: true);

            RecordUndo("Promote to Subgraph Input");
            if (!PcgSubgraphInterfaceUtility.TryPromoteWireToSubgraphInput(
                    definition,
                    source.NodeId,
                    sourceHandle,
                    targetHandle,
                    pinType,
                    out var portId,
                    out var error))
            {
                Debug.LogWarning($"[PCG] Promote to Subgraph Input failed: {error}");
                return;
            }

            var scope = CaptureVisibleDocument();
            var parentEdge = scope.edges.FirstOrDefault(e =>
                e.source == source.NodeId &&
                e.target == instance.NodeId &&
                e.sourceHandle == sourceHandle);
            if (parentEdge != null)
                parentEdge.targetHandle = portId;
            else
            {
                scope.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}",
                    source = source.NodeId,
                    target = instance.NodeId,
                    sourceHandle = sourceHandle,
                    targetHandle = portId,
                });
            }

            ApplyScopeDocument(scope);
            RefreshSubgraphDefinitionInterface(definition.id);
            CommitState();
            NotifyDocumentChanged();
        }

        public void ConnectSelectionAsSubgraphInput(PcgGraphNodeBase source, PcgSubgraphNodeView instance)
        {
            if (source == null || instance == null)
                return;

            var definition = FindSubgraph(instance.SubgraphDefinitionId);
            if (definition == null)
                return;

            var sourceHandle = source.GetOutputPort()?.userData as string ?? "out";
            var pinType = ResolveVisiblePinType(source.NodeId, sourceHandle, output: true);
            var portId = PcgSubgraphInterfaceUtility.NextInputPortId(definition);

            RecordUndo("Connect as Subgraph Input");
            PcgSubgraphInterfaceUtility.AddInputPort(definition, portId, pinType, source.GetDisplayTitle());
            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);

            var scope = CaptureVisibleDocument();
            scope.edges.Add(new PcgGraphEdgeRecord
            {
                id = $"e{++m_EdgeCounter}",
                source = source.NodeId,
                target = instance.NodeId,
                sourceHandle = sourceHandle,
                targetHandle = portId,
            });
            ApplyScopeDocument(scope);
            RefreshSubgraphDefinitionInterface(definition.id);
            CommitState();
            NotifyDocumentChanged();
        }

        public void PromoteEdgeToSubgraphAssetInput(Edge graphEdge, PcgExternalSubgraphNodeView instance)
        {
            if (graphEdge?.output?.node is not PcgGraphNodeBase source || instance == null)
                return;

            if (!TryLoadSubgraphAssetDocument(instance.AssetGuid, out var assetDoc, out var assetPath, out var loadError))
            {
                Debug.LogWarning($"[PCG] Promote to Subgraph Input failed: {loadError}");
                return;
            }

            var sourceHandle = graphEdge.output.userData as string ?? graphEdge.output.portName ?? "out";
            var targetHandle = graphEdge.input.userData as string ?? graphEdge.input.portName ?? "in";
            var pinType = ResolveVisiblePinType(source.NodeId, sourceHandle, output: true);

            RecordUndo("Promote to Subgraph Input");
            if (!PcgSubgraphInterfaceUtility.TryPromoteWireToAssetDocument(
                    assetDoc,
                    source.NodeId,
                    sourceHandle,
                    targetHandle,
                    pinType,
                    out var portId,
                    out var error))
            {
                Debug.LogWarning($"[PCG] Promote to Subgraph Input failed: {error}");
                return;
            }

            if (!TryWriteSubgraphAssetDocument(assetPath, assetDoc, out error))
            {
                Debug.LogWarning($"[PCG] Promote to Subgraph Input failed: {error}");
                return;
            }

            var scope = CaptureVisibleDocument();
            var parentEdge = scope.edges.FirstOrDefault(e =>
                e.source == source.NodeId &&
                e.target == instance.NodeId &&
                e.sourceHandle == sourceHandle);
            if (parentEdge != null)
                parentEdge.targetHandle = portId;
            else
            {
                scope.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}",
                    source = source.NodeId,
                    target = instance.NodeId,
                    sourceHandle = sourceHandle,
                    targetHandle = portId,
                });
            }

            ApplyScopeDocument(scope);
            ReconcileExternalNodes(new HashSet<string> { instance.AssetGuid });
            CommitState();
            NotifyDocumentChanged();
        }

        public void ConnectSelectionAsSubgraphAssetInput(
            PcgGraphNodeBase source,
            PcgExternalSubgraphNodeView instance)
        {
            if (source == null || instance == null)
                return;

            if (!TryLoadSubgraphAssetDocument(instance.AssetGuid, out var assetDoc, out var assetPath, out var loadError))
            {
                Debug.LogWarning($"[PCG] Connect as Subgraph Input failed: {loadError}");
                return;
            }

            var sourceHandle = source.GetOutputPort()?.userData as string ?? "out";
            var pinType = ResolveVisiblePinType(source.NodeId, sourceHandle, output: true);
            var definition = assetDoc.ToRootDefinition("__connect__");
            var portId = PcgSubgraphInterfaceUtility.NextInputPortId(definition);

            RecordUndo("Connect as Subgraph Input");
            PcgSubgraphInterfaceUtility.AddInputPort(definition, portId, pinType, source.GetDisplayTitle());
            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);
            PcgSubgraphAssetDocument.ApplyDefinitionToAssetDocument(definition, assetDoc);

            if (!TryWriteSubgraphAssetDocument(assetPath, assetDoc, out var error))
            {
                Debug.LogWarning($"[PCG] Connect as Subgraph Input failed: {error}");
                return;
            }

            var scope = CaptureVisibleDocument();
            scope.edges.Add(new PcgGraphEdgeRecord
            {
                id = $"e{++m_EdgeCounter}",
                source = source.NodeId,
                target = instance.NodeId,
                sourceHandle = sourceHandle,
                targetHandle = portId,
            });
            ApplyScopeDocument(scope);
            ReconcileExternalNodes(new HashSet<string> { instance.AssetGuid });
            CommitState();
            NotifyDocumentChanged();
        }

        private static bool TryLoadSubgraphAssetDocument(
            string assetGuid,
            out PcgSubgraphAssetDocument assetDoc,
            out string assetPath,
            out string error)
        {
            assetDoc = null;
            assetPath = null;
            error = null;
            var guid = PcgAssetGuidUtility.Canonicalize(assetGuid);
            assetPath = AssetDatabase.GUIDToAssetPath(guid);
            if (string.IsNullOrEmpty(assetPath))
            {
                error = "SubgraphAsset path not found for GUID " + guid;
                return false;
            }

            try
            {
                var json = File.ReadAllText(assetPath);
                if (!PcgSubgraphAssetSerializer.TryFromJson(json, out assetDoc, out error))
                    return false;
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        private static bool TryWriteSubgraphAssetDocument(
            string assetPath,
            PcgSubgraphAssetDocument assetDoc,
            out string error)
        {
            error = null;
            if (string.IsNullOrEmpty(assetPath) || assetDoc == null)
            {
                error = "Invalid subgraph asset write target.";
                return false;
            }

            try
            {
                LogSaveRepair(assetPath, PcgGraphIntegrityRepair.RepairForSave(assetDoc));
                File.WriteAllText(assetPath, PcgSubgraphAssetSerializer.ToJson(assetDoc));
                AssetDatabase.ImportAsset(assetPath);
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        public PcgSubgraphParentRefNodeView CreateParentReferenceNode(
            string parentNodeId,
            string parentHandle,
            Vector2 graphPosition)
        {
            if (!IsInsideSubgraph || string.IsNullOrEmpty(parentNodeId))
                return null;

            RecordUndo("Create Parent Reference");
            var data = new PcgNodeData();
            data.SetRaw("parentNodeId", parentNodeId);
            data.SetRaw("parentHandle", string.IsNullOrEmpty(parentHandle) ? "out" : parentHandle);
            var node = (PcgSubgraphParentRefNodeView)PcgGraphNodeFactory.Create(
                PcgStructuralNodeTypes.SubgraphParentRef,
                PcgGraphNodeFactory.NextNodeId(),
                graphPosition,
                data,
                FindSubgraph(m_CurrentSubgraphId),
                FindSubgraph);
            AddElement(node);
            ClearSelection();
            AddToSelection(node);
            node.BringToFront();
            CommitState();
            return node;
        }

        public bool TryGetImmediateParentScope(
            out List<PcgGraphNodeRecord> nodes,
            out List<PcgGraphEdgeRecord> edges)
        {
            nodes = null;
            edges = null;
            if (!IsInsideSubgraph || m_RootDocument == null)
                return false;

            if (m_SubgraphParents.Count == 0)
            {
                nodes = m_RootDocument.nodes;
                edges = m_RootDocument.edges;
                return nodes != null;
            }

            var parentDefId = m_SubgraphParents[^1];
            var parentDef = FindSubgraph(parentDefId);
            if (parentDef == null)
                return false;
            nodes = parentDef.nodes;
            edges = parentDef.edges;
            return nodes != null;
        }

        internal void RefreshSubgraphDefinitionInterface(string definitionId)
        {
            if (string.IsNullOrEmpty(definitionId))
                return;

            SaveVisibleScope();
            if (IsInsideSubgraph && m_CurrentSubgraphId == definitionId)
            {
                var definition = FindSubgraph(definitionId);
                if (definition != null)
                    LoadScope(definition.nodes, definition.edges, ScopeParameters(definition), definition, clearUndo: false);
            }
            else if (!IsInsideSubgraph)
            {
                LoadScope(
                    m_RootDocument.nodes,
                    m_RootDocument.edges,
                    m_RootDocument.parameters,
                    null,
                    clearUndo: false);
            }

            if (m_HostWindow is PcgGraphEditorWindow window)
                window.RefreshInterfacePanel();
        }

        private void ApplyScopeDocument(PcgGraphDocument scope)
        {
            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                parent.nodes = scope.nodes;
                parent.edges = scope.edges;
            }
            else
            {
                m_RootDocument.nodes = scope.nodes;
                m_RootDocument.edges = scope.edges;
            }
        }

        public PcgSubgraphDefinition FindSubgraph(string id) =>
            m_RootDocument?.subgraphs?.FirstOrDefault(subgraph => subgraph.id == id);

        public PcgSubgraphDefinition FindSubgraphDefinition(string subgraphId) => FindSubgraph(subgraphId);

        internal bool ContainsNodeInScope(string nodeId, string scopeSubgraphId)
        {
            if (string.IsNullOrEmpty(nodeId))
                return false;

            if (string.Equals(
                    m_CurrentSubgraphId ?? "",
                    scopeSubgraphId ?? "",
                    StringComparison.Ordinal))
            {
                return nodes
                    .OfType<PcgGraphNodeBase>()
                    .Any(node => node.NodeId == nodeId);
            }

            var scopeNodes = string.IsNullOrEmpty(scopeSubgraphId)
                ? m_RootDocument?.nodes
                : FindSubgraph(scopeSubgraphId)?.nodes;
            return scopeNodes?.Any(node => node != null && node.id == nodeId) == true;
        }

        internal void RefreshSubgraphInstanceTitles(string subgraphId)
        {
            if (string.IsNullOrEmpty(subgraphId))
                return;

            foreach (var node in nodes.OfType<PcgSubgraphNodeView>())
            {
                if (node.NodeType == "Subgraph" && node.SubgraphDefinitionId == subgraphId)
                    node.RefreshDefinitionTitle();
            }

            SubgraphNavigationChanged?.Invoke(m_CurrentSubgraphId);
        }

        public void ExitSubgraph()
        {
            if (!IsInsideSubgraph || m_RootDocument == null)
                return;
            SaveVisibleScope();
            var exitingSubgraphId = m_CurrentSubgraphId;
            var exitingInstanceId = m_CurrentSubgraphInstanceId;
            var externalInterfaceSync = SyncExternalNavigationRootInterfaceToParentRecord(
                exitingSubgraphId,
                exitingInstanceId);
            if (m_SubgraphParents.Count > 0)
            {
                m_CurrentSubgraphId = m_SubgraphParents[^1];
                m_CurrentSubgraphInstanceId = m_SubgraphParentInstances.Count > 0
                    ? m_SubgraphParentInstances[^1]
                    : null;
                m_SubgraphParents.RemoveAt(m_SubgraphParents.Count - 1);
                if (m_SubgraphParentInstances.Count > 0)
                    m_SubgraphParentInstances.RemoveAt(m_SubgraphParentInstances.Count - 1);
                var parent = FindSubgraph(m_CurrentSubgraphId);
                LoadScope(parent.nodes, parent.edges, ScopeParameters(parent), parent, clearUndo: false);
            }
            else
            {
                m_CurrentSubgraphId = null;
                m_CurrentSubgraphInstanceId = null;
                LoadScope(m_RootDocument.nodes, m_RootDocument.edges, m_RootDocument.parameters, null, clearUndo: false);
            }
            ApplyExternalInterfaceSyncToVisibleNode(exitingInstanceId, externalInterfaceSync);
            SubgraphNavigationChanged?.Invoke(m_CurrentSubgraphId);
            FocusNodeById(exitingInstanceId);
        }

        private void FocusNodeById(string nodeId)
        {
            if (string.IsNullOrEmpty(nodeId))
            {
                FrameAll();
                return;
            }

            var view = nodes.OfType<PcgGraphNodeBase>().FirstOrDefault(node => node.NodeId == nodeId);
            if (view == null)
            {
                FrameAll();
                return;
            }

            ClearSelection();
            AddToSelection(view);
            FrameSelection();
        }

        private void EnterSubgraph(string instanceNodeId, string definitionId)
        {
            var definition = FindSubgraph(definitionId);
            if (definition == null)
                return;
            SaveVisibleScope();
            if (IsInsideSubgraph)
            {
                m_SubgraphParents.Add(m_CurrentSubgraphId);
                m_SubgraphParentInstances.Add(m_CurrentSubgraphInstanceId);
            }
            m_CurrentSubgraphId = definitionId;
            m_CurrentSubgraphInstanceId = instanceNodeId;
            LoadScope(definition.nodes, definition.edges, ScopeParameters(definition), definition, clearUndo: false);
            SubgraphNavigationChanged?.Invoke(definitionId);
            FrameAll();
        }

        private void CreateSubgraphFromSelection()
        {
            var selectedViews = selection.OfType<PcgGraphNodeBase>()
                .Where(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput" &&
                               node.NodeType != "Output" &&
                               node.NodeType != PcgStructuralNodeTypes.SubgraphParentRef)
                .ToList();
            if (selectedViews.Count == 0)
                return;

            var selectedIds = new HashSet<string>(selectedViews.Select(node => node.NodeId));
            var affectedBindings = PcgGraphParameterUtility.FindBindingsTargetingNodes(
                EnumerateActiveParameterSources(), selectedIds);

            var scope = CaptureVisibleDocument();
            var selectedRecords = scope.nodes.Where(node => selectedIds.Contains(node.id)).ToList();
            var internalEdges = scope.edges.Where(edge =>
                selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var incoming = scope.edges.Where(edge =>
                !selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var outgoing = scope.edges.Where(edge =>
                selectedIds.Contains(edge.source) && !selectedIds.Contains(edge.target)).ToList();
            var outputSources = outgoing
                .Select(edge => (edge.source, edge.sourceHandle))
                .Distinct()
                .ToList();
            if (outputSources.Count > 1)
            {
                EditorUtility.DisplayDialog(
                    "Create Subgraph",
                    "A Subgraph exposes one Output. Select a network with one distinct outgoing value, or merge the values before creating the Subgraph.",
                    "OK");
                return;
            }
            RecordUndo("Create Subgraph");

            var localSubgraphId = NextSubgraphId();
            var externalRootId = FindExternalNavigationRootId(m_CurrentSubgraphId);
            var subgraphId = externalRootId != null
                ? externalRootId + "__" + localSubgraphId
                : localSubgraphId;
            var definition = new PcgSubgraphDefinition
            {
                id = subgraphId,
                name = $"Subgraph {m_RootDocument.subgraphs.Count + 1}",
                nodes = selectedRecords,
                edges = internalEdges,
            };

            var minY = selectedRecords.Min(node => node.position.y);
            var maxY = selectedRecords.Max(node => node.position.y);
            var centerX = selectedRecords.Average(node => node.position.x);
            var centerY = selectedRecords.Average(node => node.position.y);
            var inputNodeId = PcgGraphNodeFactory.NextNodeId();
            var outputNodeId = PcgGraphNodeFactory.NextNodeId();
            definition.nodes.Add(new PcgGraphNodeRecord
            {
                id = inputNodeId,
                type = "SubgraphInput",
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, minY - 160f)),
                data = new PcgNodeData(),
            });
            definition.nodes.Add(new PcgGraphNodeRecord
            {
                id = outputNodeId,
                type = "Output",
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, maxY + 160f)),
                data = PcgNodeManifest.DefaultDataFor("Output"),
            });

            var parentEdges = scope.edges.Where(edge =>
                !selectedIds.Contains(edge.source) && !selectedIds.Contains(edge.target)).ToList();
            var instanceId = PcgGraphNodeFactory.NextNodeId();

            for (var i = 0; i < incoming.Count; i++)
            {
                var edge = incoming[i];
                var portId = $"in_{i + 1}";
                definition.inputs.Add(new PcgSubgraphPort
                {
                    id = portId,
                    name = string.IsNullOrEmpty(edge.targetHandle) ? portId : edge.targetHandle,
                    pinType = PcgSubgraphInputUtility.AnyPinType,
                });
                definition.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}", source = inputNodeId, target = edge.target,
                    sourceHandle = portId, targetHandle = edge.targetHandle,
                });
                parentEdges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id, source = edge.source, target = instanceId,
                    sourceHandle = edge.sourceHandle, targetHandle = portId,
                });
            }

            var outputPortId = "out_1";
            var outputSource = outputSources.FirstOrDefault();
            definition.outputs.Add(new PcgSubgraphPort
            {
                id = outputPortId,
                name = string.IsNullOrEmpty(outputSource.sourceHandle) ? "Output" : outputSource.sourceHandle,
                pinType = outputSources.Count == 0
                    ? "Any"
                    : ResolveVisiblePinType(outputSource.source, outputSource.sourceHandle, output: true),
            });
            if (outputSources.Count == 1)
            {
                definition.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}", source = outputSource.source, target = outputNodeId,
                    sourceHandle = outputSource.sourceHandle, targetHandle = "in",
                });
            }
            foreach (var edge in outgoing)
            {
                parentEdges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id, source = instanceId, target = edge.target,
                    sourceHandle = outputPortId, targetHandle = edge.targetHandle,
                });
            }

            var instanceData = new PcgNodeData();
            instanceData.SetRaw("subgraphId", subgraphId);
            var parentNodes = scope.nodes.Where(node => !selectedIds.Contains(node.id)).ToList();
            parentNodes.Add(new PcgGraphNodeRecord
            {
                id = instanceId,
                type = "Subgraph",
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, centerY)),
                data = instanceData,
            });

            PcgSubgraphContractUtility.Synchronize(definition);
            m_RootDocument.subgraphs.Add(definition);
            if (affectedBindings.Count > 0)
                PromoteBindingsIntoDefinition(definition, selectedIds);
            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                parent.nodes = parentNodes;
                parent.edges = parentEdges;
                LoadScope(parent.nodes, parent.edges, ScopeParameters(parent), parent, clearUndo: false);
            }
            else
            {
                m_RootDocument.nodes = parentNodes;
                m_RootDocument.edges = parentEdges;
                LoadScope(parentNodes, parentEdges, m_RootDocument.parameters, null, clearUndo: false);
            }
            CommitState();
            NotifyDocumentChanged();
        }

        private string FindExternalNavigationRootId(string scopeDefinitionId)
        {
            if (string.IsNullOrEmpty(scopeDefinitionId))
                return null;
            if (m_ExternalNavRootGuidByDefId.ContainsKey(scopeDefinitionId))
                return scopeDefinitionId;
            foreach (var rootId in m_ExternalNavRootGuidByDefId.Keys)
            {
                if (scopeDefinitionId.StartsWith(rootId + "__", StringComparison.Ordinal))
                    return rootId;
            }

            return null;
        }

        private string NextSubgraphId()
        {
            var counter = m_RootDocument.subgraphs.Count + 1;
            string id;
            do id = $"subgraph_{counter++}";
            while (FindSubgraph(id) != null ||
                   m_ExternalNavRootGuidByDefId.Keys.Any(root =>
                       FindSubgraph(root + "__" + id) != null));
            return id;
        }

        private void CloneInlineSubgraphDefinitionForDuplicate(PcgGraphNodeRecord clone)
        {
            var sourceDefinitionId = clone.data?.GetRaw("subgraphId")?.ToString() ?? "";
            if (string.IsNullOrEmpty(sourceDefinitionId))
                return;

            if (!PcgSubgraphDefinitionCloner.TryCloneDefinitionClosure(
                    sourceDefinitionId,
                    m_RootDocument?.subgraphs,
                    AllocateDuplicateSubgraphDefinitionId(sourceDefinitionId),
                    out var cloneResult))
            {
                return;
            }

            if (m_RootDocument == null)
                m_RootDocument = new PcgGraphDocument();
            m_RootDocument.subgraphs ??= new List<PcgSubgraphDefinition>();
            foreach (var definition in cloneResult.Definitions)
                m_RootDocument.subgraphs.Add(definition);

            clone.data ??= new PcgNodeData();
            clone.data.SetRaw("subgraphId", cloneResult.RootDefinitionId);
        }

        private Func<string> AllocateDuplicateSubgraphDefinitionId(string sourceDefinitionId)
        {
            var externalRootId = FindExternalNavigationRootId(sourceDefinitionId);
            if (string.IsNullOrEmpty(externalRootId))
                return NextSubgraphId;
            return () => externalRootId + "__" + NextSubgraphId();
        }

        private static List<PcgGraphParameter> ScopeParameters(PcgSubgraphDefinition definition) =>
            definition?.parameters ?? new List<PcgGraphParameter>();

        private IEnumerable<PcgGraphParameter> EnumerateActiveParameterSources()
        {
            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                if (parent?.parameters != null)
                {
                    foreach (var parameter in parent.parameters)
                        yield return parameter;
                }
            }
            else if (m_Blackboard?.Parameters != null)
            {
                foreach (var parameter in m_Blackboard.Parameters)
                    yield return parameter;
            }

            if (m_RootDocument?.parameters == null)
                yield break;
            foreach (var parameter in m_RootDocument.parameters)
                yield return parameter;
        }

        private void PromoteBindingsIntoDefinition(PcgSubgraphDefinition definition, ISet<string> nodeIds)
        {
            if (definition == null || nodeIds == null || nodeIds.Count == 0)
                return;

            definition.parameters ??= new List<PcgGraphParameter>();
            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                if (parent != null)
                {
                    parent.parameters ??= new List<PcgGraphParameter>();
                    PcgGraphParameterUtility.PromoteBindings(parent.parameters, definition, nodeIds);
                }
            }
            else if (m_Blackboard != null && m_RootDocument != null)
            {
                m_RootDocument.parameters = m_Blackboard.CollectParameters();
            }

            if (m_RootDocument?.parameters != null)
                PcgGraphParameterUtility.PromoteBindings(m_RootDocument.parameters, definition, nodeIds);

            if (m_Blackboard != null && !IsInsideSubgraph)
                m_Blackboard.LoadParameters(m_RootDocument.parameters);
        }

        private void PromoteBindingsIntoAsset(PcgSubgraphAssetDocument assetDoc, ISet<string> nodeIds)
        {
            if (assetDoc == null || nodeIds == null || nodeIds.Count == 0)
                return;

            assetDoc.parameters ??= new List<PcgGraphParameter>();
            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                if (parent?.parameters != null)
                    PcgGraphParameterUtility.PromoteBindingsToList(parent.parameters, assetDoc.parameters, nodeIds);
            }
            else if (m_Blackboard != null && m_RootDocument != null)
            {
                m_RootDocument.parameters = m_Blackboard.CollectParameters();
            }

            if (m_RootDocument?.parameters != null)
                PcgGraphParameterUtility.PromoteBindingsToList(m_RootDocument.parameters, assetDoc.parameters, nodeIds);

            if (m_Blackboard != null && !IsInsideSubgraph)
                m_Blackboard.LoadParameters(m_RootDocument.parameters);

            if (assetDoc.parameters.Count > 0)
                assetDoc.version = PcgSubgraphAssetMigration.Version20;
        }

        public PcgSubgraphDefinition FindSubgraphDefinitionForNode(PcgGraphNodeBase node)
        {
            if (node is PcgSubgraphNodeView subgraph && subgraph.Kind == PcgSubgraphNodeKind.Instance)
                return FindSubgraph(subgraph.SubgraphDefinitionId);
            return null;
        }

        public bool TryLoadExternalSubgraphParameters(
            string assetGuid,
            out List<PcgGraphParameter> parameters,
            out string contentHash,
            out string schemaVersion)
        {
            parameters = new List<PcgGraphParameter>();
            contentHash = "";
            schemaVersion = PcgSubgraphAssetMigration.Version10;
            var canonical = PcgAssetGuidUtility.Canonicalize(assetGuid);
            if (!PcgAssetGuidUtility.IsValid(canonical))
                return false;

            var path = AssetDatabase.GUIDToAssetPath(canonical);
            if (string.IsNullOrEmpty(path))
                return false;

            var asset = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(path);
            var json = asset != null && asset.ImportSucceeded ? asset.SourceJson : System.IO.File.ReadAllText(path);
            if (!PcgSubgraphAssetSerializer.TryFromJson(json, out var document, out _))
                return false;

            parameters = document.parameters ?? new List<PcgGraphParameter>();
            contentHash = document.contentHash ?? "";
            schemaVersion = document.version ?? PcgSubgraphAssetMigration.Version10;
            return true;
        }

        public void UpgradeExternalSubgraphInstance(PcgExternalSubgraphNodeView instance)
        {
            if (instance == null)
                return;

            RecordUndo("Upgrade Subgraph Asset Instance");
            SaveVisibleScope();
            instance.ClearPinnedAssetVersion();
            ReconcileExternalNodes(new HashSet<string> { instance.AssetGuid });
            CommitState();
            NotifyDocumentChanged();
        }

        private string ResolveVisiblePinType(string nodeId, string handle, bool output)
        {
            var node = nodes.OfType<PcgGraphNodeBase>().FirstOrDefault(item => item.NodeId == nodeId);
            if (node == null)
                return "Any";
            return ResolveNodePinType(node, handle, output);
        }

        private static string ResolveNodePinType(PcgGraphNodeBase node, string handle, bool output)
        {
            if (node is PcgInterfaceAnchorNodeBase anchor)
                return anchor.ResolvedPinType;
            if (node is PcgSubgraphNodeView subgraph)
                return output ? subgraph.GetOutputPinType(handle) : subgraph.GetInputPinType(handle);
            if (node is PcgExternalSubgraphNodeView external)
                return output ? external.GetOutputPinType(handle) : external.GetInputPinType(handle);
            return output
                ? PcgNodeManifest.GetOutputPinType(node.NodeType, handle)
                : PcgNodeManifest.GetInputPinType(node.NodeType, handle);
        }

        public override List<Port> GetCompatiblePorts(Port startPort, NodeAdapter nodeAdapter)
        {
            var compatible = new List<Port>();
            ports.ForEach(port =>
            {
                if (startPort == port || startPort.node == port.node)
                    return;
                if (startPort.direction == port.direction)
                    return;

                if (startPort.node is PcgGraphNodeBase srcNode && port.node is PcgGraphNodeBase tgtNode)
                {
                    string outType, inType, outHandle, inHandle;
                    if (startPort.direction == Direction.Output)
                    {
                        outType = srcNode.NodeType;
                        inType = tgtNode.NodeType;
                        outHandle = startPort.userData as string ?? startPort.portName;
                        inHandle = port.userData as string ?? port.portName;
                    }
                    else
                    {
                        outType = tgtNode.NodeType;
                        inType = srcNode.NodeType;
                        outHandle = port.userData as string ?? port.portName;
                        inHandle = startPort.userData as string ?? startPort.portName;
                    }

                    var outputNode = startPort.direction == Direction.Output ? srcNode : tgtNode;
                    var inputNode = startPort.direction == Direction.Output ? tgtNode : srcNode;
                    if (!PcgConnectionValidator.ArePinTypesCompatible(outputNode, inputNode, outHandle, inHandle))
                        return;
                }

                compatible.Add(port);
            });
            return compatible;
        }

        public void ShowSearchWindow(Vector2 panelMousePos)
        {
            m_SearchWindow.ClearPortDragContext();
            var graphPos = PanelToGraphPosition(panelMousePos);
            m_SearchWindow.SetSpawnPosition(graphPos);

            var screenPos = this.LocalToWorld(panelMousePos) + (Vector2)m_HostWindow.position.position;
            SearchWindow.Open(new SearchWindowContext(screenPos), m_SearchWindow);
        }

        private Vector2 PanelToGraphPosition(Vector2 panelPosition)
        {
            var local = contentViewContainer.WorldToLocal(panelPosition);
            return local;
        }

        // ─── Undo System (Shader Graph pattern: ScriptableObject proxy) ──

        /// <summary>Save current state and register with Unity's Undo system.
        /// Call BEFORE applying a mutation, then call <see cref="CommitState"/> after.</summary>
        public void RecordUndo(string actionName)
        {
            if (m_SuppressUndo) return;
            var state = EnsureUndoState();
            state.SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
            state.RegisterCompleteObjectUndo(string.IsNullOrEmpty(actionName) ? "Graph Change" : actionName);
        }

        /// <summary>Update the proxy with the post-mutation state.
        /// Call AFTER applying a mutation.</summary>
        public void CommitState()
        {
            if (m_SuppressUndo) return;
            EnsureUndoState().SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
            MarkExternalNavDirtyIfEditing();
        }

        private void MarkExternalNavDirtyIfEditing()
        {
            var rootId = FindExternalNavigationRootId(m_CurrentSubgraphId);
            if (!string.IsNullOrEmpty(rootId))
                m_DirtyExternalNavDefIds.Add(rootId);
        }

        private static string HashUtf8Content(string content)
        {
            using var sha = SHA256.Create();
            var bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(content ?? ""));
            return BitConverter.ToString(bytes).Replace("-", "").ToLowerInvariant();
        }

        /// <summary>Convenience wrapper: record, apply, commit in one call.</summary>
        public void WithUndo(string actionName, Action action)
        {
            if (m_SuppressUndo) { action(); return; }
            RecordUndo(actionName);
            action();
            CommitState();
        }

        /// <summary>Begin a drag transaction. Pre-state is captured but not yet
        /// registered. Call <see cref="EndDrag"/> when the drag ends.</summary>
        public void BeginDrag(string actionName)
        {
            if (m_SuppressUndo) return;
            // A previous drag (e.g., Scene View spline handle) may not have been
            // properly ended. Discard the stale snapshot so the new drag starts fresh.
            m_PendingSnapshot = null;
            m_PendingAction = null;
            m_PendingSnapshot = PcgGraphSerializer.ToJson(ExportDocument(), pretty: false);
            m_PendingAction = actionName;
        }

        /// <summary>End a drag transaction. If the graph state changed since
        /// <see cref="BeginDrag"/>, register a single undo step with Unity.</summary>
        public void EndDrag()
        {
            if (m_PendingSnapshot == null) return;
            var currentJson = PcgGraphSerializer.ToJson(ExportDocument(), pretty: false);
            if (currentJson != m_PendingSnapshot)
            {
                var state = EnsureUndoState();
                state.SetGraphJson(m_PendingSnapshot);
                state.RegisterCompleteObjectUndo(string.IsNullOrEmpty(m_PendingAction) ? "Graph Change" : m_PendingAction);
                state.SetGraphJson(currentJson);
            }
            m_PendingSnapshot = null;
            m_PendingAction = null;
            MarkExternalNavDirtyIfEditing();
            NotifyDocumentChanged();
        }

        public void NotifyDocumentChanged()
        {
            if (m_HostWindow is PcgGraphEditorWindow window)
            {
                window.ValidatePreviewNodeExists();
                GraphDocumentChanged?.Invoke(window);
            }
        }

        public void NotifyInterfaceChanged()
        {
            if (!IsInsideSubgraph)
                return;
            var definition = FindSubgraph(m_CurrentSubgraphId);
            if (definition == null)
                return;
            SaveVisibleScope();
            SyncExternalNavigationRootInterfaceToParentRecord(
                m_CurrentSubgraphId,
                m_CurrentSubgraphInstanceId);
            LoadScope(definition.nodes, definition.edges, ScopeParameters(definition), definition, clearUndo: false);
            CommitState();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
                window.RefreshInterfacePanel();
        }

        internal void SpawnInterfaceInputConnector(PcgSubgraphPort ifacePort)
        {
            if (ifacePort == null || !IsEditingSubgraphInterface())
                return;

            var definition = GetActiveInterfaceDefinition();
            if (definition == null)
                return;

            RecordUndo("Add Subgraph Input");
            SaveVisibleScope();

            PcgSubgraphInterfaceUtility.AddInputPort(definition, ifacePort.id, ifacePort.pinType);
            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);

            var graphPos = PanelToGraphPosition(m_LastMousePos);
            var port = definition.inputs?.FirstOrDefault(p => p != null && p.id == ifacePort.id);
            if (port != null)
            {
                port.anchorPlaced = true;
                port.anchorX = graphPos.x;
                port.anchorY = graphPos.y;
            }

            PlaceOrCreateInterfaceInputAnchor(definition, ifacePort.id, graphPos);
            CommitState();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
            {
                window.RefreshInterfacePanel();
                window.SetStatus($"Placed input '{ifacePort.name}'. Drag from its output to a node.");
            }
        }

        internal void SpawnInterfaceOutputConnector(PcgSubgraphPort ifacePort)
        {
            if (ifacePort == null || !IsEditingSubgraphInterface())
                return;

            var definition = GetActiveInterfaceDefinition();
            if (definition == null)
                return;

            RecordUndo("Add Subgraph Output");
            SaveVisibleScope();

            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);

            var graphPos = PanelToGraphPosition(m_LastMousePos);
            var port = definition.outputs?.FirstOrDefault(p => p != null && p.id == ifacePort.id);
            if (port != null)
            {
                port.anchorPlaced = true;
                port.anchorX = graphPos.x;
                port.anchorY = graphPos.y;
            }

            PlaceOrCreateInterfaceOutputAnchor(definition, ifacePort.id, graphPos);
            CommitState();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
            {
                window.RefreshInterfacePanel();
                window.SetStatus($"Placed output '{ifacePort.name}'. Drag a node output into it.");
            }
        }

        private void PlaceOrCreateInterfaceInputAnchor(
            PcgSubgraphDefinition definition,
            string portId,
            Vector2 graphPos)
        {
            var anchor = GetOrCreateInterfaceInputAnchor(definition, portId);
            anchor.SetPosition(new Rect(graphPos.x, graphPos.y, PcgGraphNodeBase.NodeWidth, PcgGraphNodeBase.NodeHeight));
            ClearSelection();
            AddToSelection(anchor);
            anchor.BringToFront();
        }

        private void PlaceOrCreateInterfaceOutputAnchor(
            PcgSubgraphDefinition definition,
            string portId,
            Vector2 graphPos)
        {
            var anchor = GetOrCreateInterfaceOutputAnchor(definition, portId);
            anchor.SetPosition(new Rect(graphPos.x, graphPos.y, PcgGraphNodeBase.NodeWidth, PcgGraphNodeBase.NodeHeight));
            ClearSelection();
            AddToSelection(anchor);
            anchor.BringToFront();
        }

        public bool ConnectInterfaceInputPort(string portId, string pinType, Port targetInputPort)
        {
            if (!IsEditingSubgraphInterface() || targetInputPort?.node is not PcgGraphNodeBase target)
                return false;
            if (IsHiddenSubgraphInterfaceNodeType(target.NodeType))
                return false;

            var definition = GetActiveInterfaceDefinition();
            if (definition == null || string.IsNullOrEmpty(portId))
                return false;

            var targetHandle = targetInputPort.userData as string ?? targetInputPort.portName ?? "in";
            if (!PcgNodeManifest.PinTypesCompatible(
                    pinType,
                    PcgNodeManifest.GetInputPinType(target.NodeType, targetHandle)))
                return false;

            RecordUndo("Connect Interface Input");
            SaveVisibleScope();
            PcgSubgraphInterfaceUtility.AddInputPort(definition, portId, pinType);
            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);
            PcgSubgraphInterfaceUtility.EnsureInternalInputEdge(definition, portId, target.NodeId, targetHandle);
            LoadScope(definition.nodes, definition.edges, ScopeParameters(definition), definition, clearUndo: false);
            CommitState();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
                window.RefreshInterfacePanel();
            return true;
        }

        public bool ConnectInterfaceOutputPort(string portId, string pinType, Port sourceOutputPort)
        {
            if (!IsEditingSubgraphInterface() || sourceOutputPort?.node is not PcgGraphNodeBase source)
                return false;
            if (IsHiddenSubgraphInterfaceNodeType(source.NodeType))
                return false;

            var definition = GetActiveInterfaceDefinition();
            if (definition == null || string.IsNullOrEmpty(portId))
                return false;

            var sourceHandle = sourceOutputPort.userData as string ?? sourceOutputPort.portName ?? "out";
            if (!PcgNodeManifest.PinTypesCompatible(
                    PcgNodeManifest.GetOutputPinType(source.NodeType, sourceHandle),
                    pinType))
                return false;

            RecordUndo("Connect Interface Output");
            SaveVisibleScope();
            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);
            PcgSubgraphInterfaceUtility.EnsureInternalOutputEdge(
                definition, portId, source.NodeId, sourceHandle);
            LoadScope(definition.nodes, definition.edges, ScopeParameters(definition), definition, clearUndo: false);
            CommitState();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
                window.RefreshInterfacePanel();
            return true;
        }

        private bool TryCommitInterfaceAnchorEdge(Edge edge)
        {
            if (!IsEditingSubgraphInterface() || edge == null)
                return false;

            if (edge.output?.node is PcgInterfaceInputAnchorView inputAnchor &&
                edge.input?.node is PcgGraphNodeBase target &&
                target is not PcgInterfaceAnchorNodeBase &&
                !IsHiddenSubgraphInterfaceNodeType(target.NodeType))
            {
                var definition = GetActiveInterfaceDefinition();
                var port = definition?.inputs?.FirstOrDefault(p => p != null && p.id == inputAnchor.PortId);
                if (port == null)
                    return false;

                ConnectInterfaceInputPort(port.id, port.pinType, edge.input);
                return true;
            }

            if (edge.output?.node is PcgGraphNodeBase source &&
                source is not PcgInterfaceAnchorNodeBase &&
                !IsHiddenSubgraphInterfaceNodeType(source.NodeType) &&
                edge.input?.node is PcgInterfaceOutputAnchorView outputAnchor)
            {
                var definition = GetActiveInterfaceDefinition();
                var port = definition?.outputs?.FirstOrDefault(p => p != null && p.id == outputAnchor.PortId);
                if (port == null)
                    return false;

                ConnectInterfaceOutputPort(port.id, port.pinType, edge.output);
                return true;
            }

            return false;
        }

        private static void HandleInterfaceAnchorEdgeRemoved(PcgSubgraphDefinition definition, Edge edge)
        {
            if (definition == null || edge == null)
                return;

            if (edge.output?.node is PcgInterfaceInputAnchorView inputAnchor)
                PcgSubgraphInterfaceUtility.RemoveInternalInputEdges(definition, inputAnchor.PortId);
            else if (edge.input?.node is PcgInterfaceOutputAnchorView outputAnchor)
                PcgSubgraphInterfaceUtility.RemoveInternalOutputEdges(definition, outputAnchor.PortId);
        }

        private static bool IsHiddenSubgraphInterfaceNodeType(string type) =>
            type == PcgStructuralNodeTypes.SubgraphInput ||
            type == PcgStructuralNodeTypes.SubgraphOutput;

        private void LoadInterfaceEdges(
            PcgGraphDocument doc,
            PcgSubgraphDefinition definition,
            Dictionary<string, PcgGraphNodeBase> nodeViews)
        {
            m_InterfaceInputAnchors.Clear();
            m_InterfaceOutputAnchors.Clear();

            var inputNodeId = PcgSubgraphInterfaceUtility.GetSubgraphInputNodeId(definition);
            if (doc.edges == null)
                return;

            foreach (var edgeRecord in doc.edges)
            {
                if (!string.IsNullOrEmpty(inputNodeId) &&
                    edgeRecord.source == inputNodeId &&
                    nodeViews.TryGetValue(edgeRecord.target, out var inputTarget))
                {
                    var portId = edgeRecord.sourceHandle ?? string.Empty;
                    var anchor = GetOrCreateInterfaceInputAnchor(definition, portId);
                    var savedPort = definition.inputs?.FirstOrDefault(p => p.id == portId);
                    if (savedPort is { anchorPlaced: true })
                    {
                        anchor.SetPosition(new Rect(savedPort.anchorX, savedPort.anchorY,
                            PcgGraphNodeBase.NodeWidth, PcgGraphNodeBase.NodeHeight));
                    }
                    else
                    {
                        anchor.PlaceLeftOf(inputTarget);
                    }

                    var input = inputTarget.FindInputPort(edgeRecord.targetHandle ?? "in");
                    if (anchor.OutputPort == null || input == null)
                        continue;

                    var edge = anchor.OutputPort.ConnectTo(input);
                    edge.userData = edgeRecord.id;
                    AddElement(edge);
                }
            }

            RestorePlacedInterfaceAnchors(definition);
        }

        private void RestorePlacedInterfaceAnchors(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return;

            foreach (var port in definition.inputs ?? Enumerable.Empty<PcgSubgraphPort>())
            {
                if (port == null || !port.anchorPlaced || m_InterfaceInputAnchors.ContainsKey(port.id))
                    continue;

                var anchor = GetOrCreateInterfaceInputAnchor(definition, port.id);
                anchor.SetPosition(new Rect(port.anchorX, port.anchorY, PcgGraphNodeBase.NodeWidth, PcgGraphNodeBase.NodeHeight));
            }

        }

        private PcgInterfaceInputAnchorView GetOrCreateInterfaceInputAnchor(
            PcgSubgraphDefinition definition,
            string portId)
        {
            if (m_InterfaceInputAnchors.TryGetValue(portId, out var existing))
                return existing;

            var port = definition.inputs?.FirstOrDefault(p => p.id == portId);
            if (port == null)
            {
                port = new PcgSubgraphPort
                {
                    id = portId,
                    name = portId,
                    pinType = "Any",
                };
            }

            var anchor = new PcgInterfaceInputAnchorView(port);
            AddElement(anchor);
            m_InterfaceInputAnchors[portId] = anchor;
            return anchor;
        }

        private PcgInterfaceOutputAnchorView GetOrCreateInterfaceOutputAnchor(
            PcgSubgraphDefinition definition,
            string portId)
        {
            if (m_InterfaceOutputAnchors.TryGetValue(portId, out var existing))
                return existing;

            var port = definition.outputs?.FirstOrDefault(p => p.id == portId);
            if (port == null)
            {
                port = new PcgSubgraphPort
                {
                    id = portId,
                    name = portId,
                    pinType = "Any",
                };
            }

            var anchor = new PcgInterfaceOutputAnchorView(port);
            AddElement(anchor);
            m_InterfaceOutputAnchors[portId] = anchor;
            return anchor;
        }

        private void SyncInterfaceAnchorPositions(PcgSubgraphDefinition definition)
        {
            if (definition == null)
                return;

            foreach (var port in definition.inputs ?? Enumerable.Empty<PcgSubgraphPort>())
            {
                if (port == null || !m_InterfaceInputAnchors.TryGetValue(port.id, out var anchor))
                    continue;

                var pos = anchor.GetPosition().position;
                port.anchorPlaced = true;
                port.anchorX = pos.x;
                port.anchorY = pos.y;
            }

        }

        public void ToggleNodePreview(PcgGraphNodeBase node)
        {
            if (node == null || m_HostWindow is not PcgGraphEditorWindow window)
                return;

            if (node.NodeType == "SubgraphInput")
            {
                Debug.LogWarning("[PCG] SubgraphInput has no geometry to preview.");
                return;
            }

            window.ToggleNodePreview(
                node.NodeId,
                node.NodeType,
                node.GetDisplayTitle(),
                CurrentSubgraphId,
                GetSubgraphInstanceChain());
        }

        /// <summary>
        /// Houdini-style: selecting Match Size should preview that node's cooked output,
        /// not leave Scene View showing upstream geometry while only the target box moves.
        /// </summary>
        internal void EnsureMatchSizeScenePreview()
        {
            var selected = selection.OfType<PcgManifestNodeView>()
                .FirstOrDefault(node => node.NodeType == "MatchSize");
            if (selected == null || m_HostWindow is not PcgGraphEditorWindow window)
                return;

            var scope = CurrentSubgraphId ?? string.Empty;
            if (window.PreviewNodeId == selected.NodeId &&
                string.Equals(window.PreviewScopeSubgraphId ?? string.Empty, scope,
                    System.StringComparison.Ordinal))
                return;

            window.SetPreviewNode(
                selected.NodeId,
                selected.GetDisplayTitle(),
                CurrentSubgraphId,
                GetSubgraphInstanceChain());
        }

        public void RefreshNodePreviewVisuals()
        {
            var previewNodeId = m_HostWindow is PcgGraphEditorWindow window ? window.PreviewNodeId : null;
            foreach (var node in nodes.OfType<PcgGraphNodeBase>())
                node.SetNodePreviewState(!string.IsNullOrEmpty(previewNodeId) && node.NodeId == previewNodeId);
        }

        internal void RefreshInspector() => ScheduleInspectorRefresh();

        internal void ScheduleInspectorRefresh()
        {
            if (m_Inspector == null)
                return;
            if (m_InspectorRefreshScheduled)
                return;
            m_InspectorRefreshScheduled = true;
            EditorApplication.delayCall += () =>
            {
                m_InspectorRefreshScheduled = false;
                if (m_Inspector == null)
                    return;
                m_Inspector.OnSelectionChanged();
            };
        }

        /// <summary>
        /// Re-derive the scene edit context from the current graph selection.
        /// Called on selection change and when entering PCG mode so the toolbar
        /// reflects the already-selected node instead of defaulting to Object.
        /// </summary>
        internal void RefreshSceneEditContext() => UpdateSceneEditContext();

        private void UpdateSceneEditContext()
        {
            var selected = selection.OfType<PcgGraphNodeBase>().FirstOrDefault();
            if (selected is PcgManifestNodeView manifestNode)
            {
                if (manifestNode.NodeType == "CreateSpline" || manifestNode.NodeType == "CreateBezierSpline")
                {
                    m_SceneEditContext = new PcgSceneEditContext(
                        SceneEditLevel.Component,
                        SceneEditDomain.SplineControlPoint,
                        selected.NodeId,
                        s_SplineDomains);
                }
                else if (manifestNode.NodeType == "MatchSize")
                {
                    m_SceneEditContext = PcgSceneEditContext.ObjectMode;
                }
                else if (manifestNode.NodeType == "GroupDelete"
                    || manifestNode.NodeType == "GroupCreate"
                    || manifestNode.NodeType == "GroupCombine")
                {
                    var nodeData = manifestNode.CollectData();
                    var domainStr = nodeData?.GetRaw("domain")?.ToString() ?? "edge";
                    var defaultDomain = domainStr switch
                    {
                        "edge" => SceneEditDomain.Edge,
                        "face" => SceneEditDomain.Face,
                        "point" => SceneEditDomain.Vertex,
                        _ => SceneEditDomain.Edge,
                    };
                    m_SceneEditContext = new PcgSceneEditContext(
                        SceneEditLevel.Component,
                        defaultDomain,
                        selected.NodeId,
                        s_GroupDomains);
                }
                else if (NodeHasOrReceivesGroups(manifestNode))
                {
                    m_SceneEditContext = new PcgSceneEditContext(
                        SceneEditLevel.Component,
                        SceneEditDomain.Edge,
                        selected.NodeId,
                        s_GroupDomains);
                }
                else
                {
                    m_SceneEditContext = PcgSceneEditContext.ObjectMode;
                }
            }
            else
            {
                m_SceneEditContext = PcgSceneEditContext.ObjectMode;
            }
            SceneContextChanged?.Invoke(m_SceneEditContext);
        }

        private bool NodeHasOrReceivesGroups(PcgManifestNodeView node)
        {
            // Check if node has manifest output groups
            if (PcgNodeManifest.TryGet(node.NodeType, out var def) && def.outputGroups.Count > 0)
                return true;

            // Check dynamic output groups (properties with isGroupOutput)
            if (def != null)
            {
                foreach (var (_, prop) in def.properties)
                {
                    if (prop.isGroupOutput)
                        return true;
                }
            }

            // Check upstream groups (input groups from upstream nodes)
            if (m_Inspector != null)
            {
                var upstream = m_Inspector.ResolveUpstreamGroups(node.NodeId);
                if (upstream.Count > 0)
                    return true;
            }

            return false;
        }

        public void SetSceneMode(SceneEditLevel level, SceneEditDomain domain)
        {
            if (level == SceneEditLevel.Object)
            {
                // Keep the capabilities inferred from the selected graph node. Object
                // Mode only changes which handles own the Scene View; it must not make
                // the Spline/Group toolbar entries disappear until the node selection
                // actually changes.
                m_SceneEditContext = new PcgSceneEditContext(
                    SceneEditLevel.Object,
                    SceneEditDomain.None,
                    m_SceneEditContext.ActiveNodeId,
                    m_SceneEditContext.SupportedDomains);
            }
            else
            {
                m_SceneEditContext = new PcgSceneEditContext(
                    level, domain, m_SceneEditContext.ActiveNodeId, m_SceneEditContext.SupportedDomains);
            }
            SceneContextChanged?.Invoke(m_SceneEditContext);
            SceneView.RepaintAll();
        }

        /// <summary>
        /// Refresh selection visuals on every node after a selection change.
        /// GraphView toggles the "selected" USS class internally; each node
        /// reads that class to update its custom m_NodeFrame border color.
        /// </summary>
        private void RefreshAllNodeSelectionVisuals()
        {
            foreach (var node in graphElements.OfType<PcgGraphNodeBase>())
                node.RefreshSelectionVisual();
        }

        /// <summary>Called by EditorWindow.Update() when version mismatch is detected.
        /// Restores the graph from the proxy's serialized JSON.</summary>
        public void RestoreFromUndoState()
        {
            m_SuppressUndo = true;

            // Capture subgraph navigation so undo doesn't kick the user back to root.
            var navParents = new List<string>(m_SubgraphParents);
            var navParentInstances = new List<string>(m_SubgraphParentInstances);
            var navCurrentId = m_CurrentSubgraphId;
            var navCurrentInstanceId = m_CurrentSubgraphInstanceId;

            // Capture selection + scene edit context so spline/group editing survives undo.
            var selectedNodeIds = selection
                .OfType<PcgGraphNodeBase>()
                .Select(node => node.NodeId)
                .Where(id => !string.IsNullOrEmpty(id))
                .ToList();
            var prevContext = m_SceneEditContext;

            var state = EnsureUndoState();
            if (PcgGraphSerializer.TryFromJson(state.GraphJson, out var doc, out _))
            {
                LoadDocument(doc, clearUndo: false);
                RestoreSubgraphNavigation(navParents, navParentInstances, navCurrentId, navCurrentInstanceId);
            }
            state.HandleUndoRedo();
            m_SuppressUndo = false;

            RestoreSelectionAndEditContext(selectedNodeIds, prevContext);

            RefreshInspector();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
                PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            SceneView.RepaintAll();
        }

        /// <summary>Re-enter the deepest still-existing subgraph in the previous
        /// navigation chain after an undo restored the root document. Falls back
        /// to a shallower ancestor if an intermediate definition was removed
        /// (e.g., undo of Create Subgraph).</summary>
        private void RestoreSubgraphNavigation(
            List<string> parents,
            List<string> parentInstances,
            string currentId,
            string currentInstanceId)
        {
            if (string.IsNullOrEmpty(currentId))
                return;

            var fullChain = new List<string>(parents) { currentId };
            var fullInstances = new List<string>(parentInstances);
            while (fullInstances.Count < fullChain.Count)
                fullInstances.Add(null);
            fullInstances[fullChain.Count - 1] = currentInstanceId;

            var deepest = -1;
            for (var i = 0; i < fullChain.Count; i++)
            {
                if (string.IsNullOrEmpty(fullChain[i]) || FindSubgraph(fullChain[i]) == null)
                    break;
                deepest = i;
            }
            if (deepest < 0)
                return;

            for (var i = 0; i < deepest; i++)
            {
                m_SubgraphParents.Add(fullChain[i]);
                m_SubgraphParentInstances.Add(fullInstances[i]);
            }
            var targetId = fullChain[deepest];
            m_CurrentSubgraphId = targetId;
            m_CurrentSubgraphInstanceId = fullInstances[deepest];
            var definition = FindSubgraph(targetId);
            LoadScope(definition.nodes, definition.edges, ScopeParameters(definition), definition, clearUndo: false);
            SubgraphNavigationChanged?.Invoke(targetId);
        }

        /// <summary>Re-select previously selected nodes after an undo restore and
        /// re-establish the scene edit context (spline control point / group domain).
        /// Prefers the exact previous context when its active node still exists so
        /// manual domain switches survive; otherwise re-derives from selection.</summary>
        private void RestoreSelectionAndEditContext(List<string> selectedNodeIds, PcgSceneEditContext prevContext)
        {
            if (selectedNodeIds.Count > 0)
            {
                ClearSelection();
                foreach (var id in selectedNodeIds)
                {
                    var view = nodes.OfType<PcgGraphNodeBase>().FirstOrDefault(node => node.NodeId == id);
                    if (view != null)
                        AddToSelection(view);
                }
            }

            if (!string.IsNullOrEmpty(prevContext.ActiveNodeId) &&
                nodes.OfType<PcgGraphNodeBase>().Any(node => node.NodeId == prevContext.ActiveNodeId))
            {
                m_SceneEditContext = prevContext;
                SceneContextChanged?.Invoke(m_SceneEditContext);
            }
            else
            {
                UpdateSceneEditContext();
            }
        }

        private void OnMouseMove(MouseMoveEvent evt)
        {
            m_LastMousePos = evt.mousePosition;
            UpdateActiveRadialMenuFromPointer(evt.mousePosition);
        }

        internal void NotifyRadialMenuOpened(PcgGraphNodeBase node)
        {
            if (node == null)
                return;

            if (m_ActiveRadialMenuNode != null && m_ActiveRadialMenuNode != node)
                m_ActiveRadialMenuNode.DismissRadialMenu();

            m_ActiveRadialMenuNode = node;
        }

        internal void NotifyRadialMenuClosed(PcgGraphNodeBase node)
        {
            if (m_ActiveRadialMenuNode == node)
                m_ActiveRadialMenuNode = null;
            // Info panel stays visible — it's dismissed by explicit interaction
        }

        private void UpdateActiveRadialMenuFromPointer(Vector2 panelMousePosition)
        {
            if (m_ActiveRadialMenuNode == null || !m_ActiveRadialMenuNode.IsRadialMenuOpen)
                return;

            var graphPos = contentViewContainer.WorldToLocal(panelMousePosition);
            if (!m_ActiveRadialMenuNode.ContainsGraphPointer(graphPos))
                m_ActiveRadialMenuNode.DismissRadialMenu();
        }

        private static bool IsEdgePointerTarget(VisualElement target)
        {
            if (target == null)
                return false;

            if (target is Edge)
                return true;

            return target.GetFirstAncestorOfType<Edge>() != null;
        }

        // ─── Pointer Events (node drag only; edge drag handled by EdgeConnector) ──

        private void OnPointerDown(PointerDownEvent evt)
        {
            if (m_SuppressUndo) return;

            // Dismiss info panel when clicking on a node, edge, or button (not blank graph)
            if (m_InfoPanel != null && m_InfoPanel.style.display == DisplayStyle.Flex)
            {
                var targetVE = evt.target as VisualElement;
                bool clickedInsidePanel = false;
                var walker = targetVE;
                while (walker != null)
                {
                    if (walker == m_InfoPanel)
                    {
                        clickedInsidePanel = true;
                        break;
                    }
                    walker = walker.parent;
                }
                if (!clickedInsidePanel)
                {
                    bool clickedNode = targetVE is PcgGraphNodeBase || targetVE?.GetFirstAncestorOfType<PcgGraphNodeBase>() != null;
                    bool clickedEdge = IsEdgePointerTarget(targetVE);
                    bool clickedButton = targetVE is Button || targetVE?.GetFirstAncestorOfType<Button>() != null;
                    if (clickedNode || clickedEdge || clickedButton)
                        HideNodeInfoPanel();
                }
            }

            if (m_ActiveRadialMenuNode != null &&
                m_ActiveRadialMenuNode.IsRadialMenuOpen &&
                IsEdgePointerTarget(evt.target as VisualElement))
            {
                m_ActiveRadialMenuNode.DismissRadialMenu();
            }

            // Always clear stale Scene View drag state when interacting with the GraphView,
            // even if the click didn't land on a node (e.g., empty canvas).
            PcgCreateSplineSceneHandles.ForceClearDragState();

            if (evt.target is VisualElement ve &&
                (ve is PcgGraphNodeBase || ve.GetFirstAncestorOfType<PcgGraphNodeBase>() != null))
            {
                BeginDrag("Move Node");
            }
        }

        private void OnPointerUp(PointerUpEvent evt)
        {
            EndDrag();
        }

        // ─── Node operations ─────────────────────────────────────────

        public PcgGraphNodeBase CreateNode(string type, Vector2 position)
        {
            if (type == "Output" && IsEditingSubgraphInterface())
                return SelectExistingSubgraphOutput();

            RecordUndo("Create Node");
            var interfaceDefinition = IsEditingSubgraphInterface()
                ? GetActiveInterfaceDefinition()
                : null;
            var node = PcgGraphNodeFactory.Create(
                type, PcgGraphNodeFactory.NextNodeId(), position, null,
                interfaceDefinition, FindSubgraph);
            AttachSubgraphNavigation(node);
            AddElement(node);
            ClearSelection();
            AddToSelection(node);
            node.BringToFront();
            CommitState();
            return node;
        }

        public override void AddToSelection(ISelectable selectable)
        {
            base.AddToSelection(selectable);
            RefreshAllNodeSelectionVisuals();
            UpdateSceneEditContext();
            EnsureMatchSizeScenePreview();
            ScheduleInspectorRefresh();
        }

        public override void RemoveFromSelection(ISelectable selectable)
        {
            base.RemoveFromSelection(selectable);
            RefreshAllNodeSelectionVisuals();
            UpdateSceneEditContext();
            ScheduleInspectorRefresh();
        }

        public override void ClearSelection()
        {
            base.ClearSelection();
            RefreshAllNodeSelectionVisuals();
            UpdateSceneEditContext();
            ScheduleInspectorRefresh();
        }

        public override EventPropagation DeleteSelection()
        {
            return base.DeleteSelection();
        }

        private bool CanDuplicateSelectedNodes() =>
            selection.OfType<PcgGraphNodeBase>()
                .Any(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput" &&
                             (!IsEditingSubgraphInterface() || node.NodeType != "Output") &&
                             node.NodeType != PcgStructuralNodeTypes.SubgraphParentRef);

        private void DuplicateSelectedNodes()
        {
            if (m_DuplicateInProgress)
                return;

            var selectedViews = selection.OfType<PcgGraphNodeBase>()
                .Where(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput" &&
                               (!IsEditingSubgraphInterface() || node.NodeType != "Output") &&
                               node.NodeType != PcgStructuralNodeTypes.SubgraphParentRef)
                .ToList();
            if (selectedViews.Count == 0)
                return;

            m_DuplicateInProgress = true;
            try
            {
                RecordUndo("Duplicate");
                m_SuppressUndo = true;

                var selectedIds = new HashSet<string>(selectedViews.Select(node => node.NodeId));
                var idMap = new Dictionary<string, string>();
                var newViews = new Dictionary<string, PcgGraphNodeBase>();
                const float offset = 40f;
                var interfaceDefinition = FindSubgraph(m_CurrentSubgraphId);

                foreach (var view in selectedViews)
                {
                    var rect = view.GetPosition();
                    var record = new PcgGraphNodeRecord
                    {
                        id = view.NodeId,
                        type = view.NodeType,
                        position = PcgGraphPosition.FromVector2(rect.position),
                        data = view.CollectData(),
                    };
                    if (view is PcgExternalSubgraphNodeView external)
                        record.subgraphInterface = external.CollectInterfaceSnapshot();

                    var clone = record.Clone();
                    clone.id = PcgGraphNodeFactory.NextNodeId();
                    clone.position = PcgGraphPosition.FromVector2(record.position.ToVector2() + new Vector2(offset, offset));
                    if (clone.type == PcgStructuralNodeTypes.Subgraph)
                        CloneInlineSubgraphDefinitionForDuplicate(clone);
                    idMap[view.NodeId] = clone.id;

                    var newView = PcgGraphNodeFactory.Create(
                        clone.type,
                        clone.id,
                        clone.position.ToVector2(),
                        clone.data,
                        interfaceDefinition,
                        FindSubgraph,
                        clone.subgraphInterface);
                    AttachSubgraphNavigation(newView);
                    newView.SetPosition(new Rect(clone.position.ToVector2(), rect.size));
                    AddElement(newView);
                    newViews[clone.id] = newView;
                }

                foreach (var edge in edges.ToList())
                {
                    if (edge.output?.node is not PcgGraphNodeBase source ||
                        edge.input?.node is not PcgGraphNodeBase target)
                    {
                        continue;
                    }

                    if (!selectedIds.Contains(source.NodeId) || !selectedIds.Contains(target.NodeId))
                        continue;

                    if (!idMap.TryGetValue(source.NodeId, out var newSourceId) ||
                        !idMap.TryGetValue(target.NodeId, out var newTargetId) ||
                        !newViews.TryGetValue(newSourceId, out var newSource) ||
                        !newViews.TryGetValue(newTargetId, out var newTarget))
                    {
                        continue;
                    }

                    var sourceHandle = edge.output.userData as string ?? edge.output.portName;
                    var targetHandle = edge.input.userData as string ?? edge.input.portName;
                    var output = newSource.FindOutputPort(sourceHandle ?? "out");
                    var input = newTarget.FindInputPort(targetHandle ?? "in");
                    if (output == null || input == null)
                        continue;

                    var newEdge = output.ConnectTo(input);
                    newEdge.userData = $"e{++m_EdgeCounter}";
                    AddElement(newEdge);
                }

                ClearSelection();
                foreach (var view in newViews.Values)
                {
                    AddToSelection(view);
                    view.BringToFront();
                }

                m_SuppressUndo = false;
                CommitState();
                NotifyDocumentChanged();
            }
            finally
            {
                m_DuplicateInProgress = false;
            }
        }

        // ─── Port drag → filtered search → create + connect ─────────

        /// <summary>Called by PcgEdgeConnectorListener.OnDropOutsidePort.
        /// position is in screen coordinates.</summary>
        public void ShowPortDragSearchWindow(Edge edge, Vector2 screenPosition)
        {
            var port = edge.output ?? edge.input;
            if (port == null) return;

            var sourceNode = port.node as PcgGraphNodeBase;
            if (sourceNode == null) return;

            var handle = port.userData as string ?? port.portName;
            bool isOutput = port.direction == Direction.Output;
            var pinType = ResolveNodePinType(sourceNode, handle, isOutput);

            var compatible = new HashSet<string>();
            foreach (var def in PcgNodeManifest.All)
            {
                if (isOutput ? PcgNodeManifest.HasCompatibleInputPin(def.type, pinType)
                              : PcgNodeManifest.HasCompatibleOutputPin(def.type, pinType))
                    compatible.Add(def.type);
            }

            var hasLibraryMatch = !string.IsNullOrEmpty(pinType) && PcgBuiltinLibrary.All.Any(item =>
                isOutput ? PcgBuiltinLibrary.HasCompatibleInput(item, pinType)
                         : PcgBuiltinLibrary.HasCompatibleOutput(item, pinType));

            if (compatible.Count == 0 && !hasLibraryMatch)
                return;

            // Convert screen position to graph position for node spawn
            Vector2 windowPos = m_HostWindow != null ? (Vector2)m_HostWindow.position.position : Vector2.zero;
            Vector2 panelPos = screenPosition - windowPos;
            Vector2 graphPos = PanelToGraphPosition(panelPos);

            m_SearchWindow.SetSpawnPosition(graphPos);
            m_SearchWindow.SetPortDragContext(port, compatible, pinType);

            SearchWindow.Open(new SearchWindowContext(screenPosition), m_SearchWindow);
        }

        public void CreateNodeAndConnect(string type, Vector2 position, Port draggedPort)
        {
            if (type == "Output" && IsEditingSubgraphInterface())
            {
                var existing = SelectExistingSubgraphOutput();
                if (draggedPort?.direction == Direction.Output)
                {
                    var input = existing?.FindInputPort("in");
                    if (input != null && !input.connected)
                    {
                        RecordUndo("Connect Subgraph Output");
                        AddElement(draggedPort.ConnectTo(input));
                        CommitState();
                    }
                }
                return;
            }

            RecordUndo("Create Node from Port");

            m_SuppressUndo = true;

            var node = PcgGraphNodeFactory.Create(
                type,
                PcgGraphNodeFactory.NextNodeId(),
                position,
                null,
                IsEditingSubgraphInterface() ? GetActiveInterfaceDefinition() : null,
                FindSubgraph);
            AddElement(node);
            ClearSelection();
            AddToSelection(node);
            node.BringToFront();

            // Auto-connect: find first compatible port on the new node
            bool isOutputDrag = draggedPort.direction == Direction.Output;
            var sourceNode = draggedPort.node as PcgGraphNodeBase;
            if (sourceNode != null)
            {
                var sourceHandle = draggedPort.userData as string ?? draggedPort.portName;
                var pinType = ResolveNodePinType(sourceNode, sourceHandle, isOutputDrag);

                if (isOutputDrag)
                {
                    foreach (var inputPort in node.inputContainer.Query<Port>().ToList())
                    {
                        var h = inputPort.userData as string ?? inputPort.portName;
                        if (PcgNodeManifest.GetInputPinType(node.NodeType, h) == pinType)
                        {
                            AddElement(draggedPort.ConnectTo(inputPort));
                            break;
                        }
                    }
                }
                else
                {
                    foreach (var outputPort in node.outputContainer.Query<Port>().ToList())
                    {
                        var h = outputPort.userData as string ?? outputPort.portName;
                        if (PcgNodeManifest.GetOutputPinType(node.NodeType, h) == pinType)
                        {
                            AddElement(outputPort.ConnectTo(draggedPort));
                            break;
                        }
                    }
                }
            }

            node.RefreshPorts();
            m_SuppressUndo = false;
            CommitState();
        }

        private PcgGraphNodeBase SelectExistingSubgraphOutput()
        {
            var output = nodes
                .ToList()
                .OfType<PcgGraphNodeBase>()
                .FirstOrDefault(node => node.NodeType == "Output");
            if (output == null)
                return null;
            ClearSelection();
            AddToSelection(output);
            output.BringToFront();
            return output;
        }

        public void LoadDocument(PcgGraphDocument doc, bool clearUndo = true)
        {
            m_RootDocument = doc ?? new PcgGraphDocument();
            foreach (var definition in m_RootDocument.subgraphs ?? Enumerable.Empty<PcgSubgraphDefinition>())
                PcgSubgraphContractUtility.Synchronize(definition);
            m_CurrentSubgraphId = null;
            m_CurrentSubgraphInstanceId = null;
            m_SubgraphParents.Clear();
            m_SubgraphParentInstances.Clear();
            m_ExternalNavRootGuidByDefId.Clear();
            m_DirtyExternalNavDefIds.Clear();
            m_ExternalNavLoadedContentHashByDefId.Clear();
            LoadScope(m_RootDocument.nodes, m_RootDocument.edges, m_RootDocument.parameters, null, clearUndo);
            SubgraphNavigationChanged?.Invoke(null);
        }

        public void LoadSubgraphAssetDocument(
            PcgGraphDocument wrapper,
            PcgSubgraphDefinition rootDefinition,
            string assetName)
        {
            m_RootDocument = wrapper ?? new PcgGraphDocument();
            m_CurrentSubgraphId = rootDefinition?.id;
            m_CurrentSubgraphInstanceId = null;
            m_SubgraphParents.Clear();
            m_SubgraphParentInstances.Clear();
            m_ExternalNavRootGuidByDefId.Clear();
            m_DirtyExternalNavDefIds.Clear();
            m_ExternalNavLoadedContentHashByDefId.Clear();
            if (rootDefinition != null)
            {
                rootDefinition.name = assetName ?? rootDefinition.name;
                PcgSubgraphContractUtility.Synchronize(rootDefinition);
            }
            LoadScope(
                rootDefinition?.nodes ?? new List<PcgGraphNodeRecord>(),
                rootDefinition?.edges ?? new List<PcgGraphEdgeRecord>(),
                new List<PcgGraphParameter>(),
                rootDefinition,
                clearUndo: true);
            SubgraphNavigationChanged?.Invoke(rootDefinition?.name);
        }

        public bool TryExportSubgraphAssetDocument(out PcgSubgraphAssetDocument assetDoc, out string error)
        {
            assetDoc = null;
            error = null;
            SaveVisibleScope();
            if (m_RootDocument == null)
            {
                error = "No document loaded.";
                return false;
            }

            var definition = FindSubgraph(m_CurrentSubgraphId) ?? m_RootDocument.subgraphs?.FirstOrDefault();
            if (definition == null)
            {
                error = "Subgraph asset root definition is missing.";
                return false;
            }

            assetDoc = PcgSubgraphAssetDocument.FromDefinition(definition);
            assetDoc.subgraphs = m_RootDocument.subgraphs
                .Where(sg => sg != null && sg.id != definition.id)
                .Select(sg => sg.Clone())
                .ToList();
            LogSaveRepair("SubgraphAsset", PcgGraphIntegrityRepair.RepairForSave(assetDoc));
            return true;
        }

        private void LoadScope(
            List<PcgGraphNodeRecord> scopeNodes,
            List<PcgGraphEdgeRecord> scopeEdges,
            List<PcgGraphParameter> scopeParameters,
            PcgSubgraphDefinition interfaceDefinition,
            bool clearUndo)
        {
            if (interfaceDefinition != null)
                PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(interfaceDefinition);
            var doc = new PcgGraphDocument
            {
                nodes = scopeNodes,
                edges = scopeEdges,
                parameters = scopeParameters,
            };
            m_ActiveRadialMenuNode?.DismissRadialMenu();
            m_ActiveRadialMenuNode = null;

            List<string> selectedNodeIds = null;
            if (!clearUndo)
            {
                selectedNodeIds = selection
                    .OfType<PcgGraphNodeBase>()
                    .Select(node => node.NodeId)
                    .Where(id => !string.IsNullOrEmpty(id))
                    .ToList();
            }

            m_SuppressUndo = true;

            foreach (var node in nodes.ToList().OfType<PcgGraphNodeBase>())
                node.DetachOverlays();

            DeleteElements(graphElements.ToList());
            PcgGraphNodeFactory.ResetCounterFromDocument(doc);

            var nodeViews = new Dictionary<string, PcgGraphNodeBase>();
            foreach (var record in doc.nodes)
            {
                if (IsHiddenSubgraphInterfaceNodeType(record.type))
                    continue;

                var view = PcgGraphNodeFactory.Create(
                    record.type,
                    record.id,
                    record.position.ToVector2(),
                    record.data,
                    interfaceDefinition,
                    FindSubgraph,
                    record.subgraphInterface);
                AttachSubgraphNavigation(view);
                AddElement(view);
                nodeViews[record.id] = view;
            }

            ResetEdgeCounterFromDocument(doc);
            foreach (var edgeRecord in doc.edges)
            {
                if (!nodeViews.TryGetValue(edgeRecord.source, out var source) ||
                    !nodeViews.TryGetValue(edgeRecord.target, out var target))
                {
                    continue;
                }

                var output = source.FindOutputPort(edgeRecord.sourceHandle ?? "out");
                var input = target.FindInputPort(edgeRecord.targetHandle ?? "in");
                if (output == null || input == null)
                    continue;

                var edge = output.ConnectTo(input);
                edge.userData = edgeRecord.id;
                AddElement(edge);
            }

            if (interfaceDefinition != null && IsEditingSubgraphInterface())
                LoadInterfaceEdges(doc, interfaceDefinition, nodeViews);

            if (m_Blackboard != null)
                m_Blackboard.LoadParameters(doc.parameters);

            m_SuppressUndo = false;
            if (clearUndo)
            {
                var state = EnsureUndoState();
                state.ClearUndo();
                state.SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
            }
            else if (selectedNodeIds is { Count: > 0 })
            {
                ClearSelection();
                foreach (var id in selectedNodeIds)
                {
                    if (nodeViews.TryGetValue(id, out var selectedView))
                        AddToSelection(selectedView);
                }
            }

            m_Inspector?.OnSelectionChanged();
            RefreshNodePreviewVisuals();
            UpdateSceneEditContext();
            if (m_HostWindow is PcgGraphEditorWindow window)
                window.RefreshPreviewToolbar();
        }

        public PcgGraphDocument ExportDocument()
        {
            SaveVisibleScope();
            var doc = m_RootDocument ?? new PcgGraphDocument();
            if (doc.HasExternalSubgraphAssets())
                doc.version = "3.0";
            else if (doc.version == "3.0")
                doc.version = "2.0";
            return doc;
        }

        /// <summary>
        /// Authoring save of a consumer <c>.pcg</c>: clone live document and strip session-only
        /// <c>__ext_*</c> definitions injected for SubgraphAsset drill-in.
        /// </summary>
        public PcgGraphDocument ExportDocumentForAuthoringSave()
        {
            var doc = ExportDocumentWithExternalInterfacesReconciled().Clone();
            StripExternalNavigationDefinitions(doc);
            LogSaveRepair("Graph", PcgGraphIntegrityRepair.RepairForSave(doc));
            if (doc.HasExternalSubgraphAssets())
                doc.version = "3.0";
            else if (doc.version == "3.0")
                doc.version = "2.0";
            return doc;
        }

        internal PcgGraphDocument ExportDocumentWithExternalInterfacesReconciled()
        {
            ReconcileExternalNodeRecords();
            var doc = m_RootDocument ?? new PcgGraphDocument();
            if (doc.HasExternalSubgraphAssets())
                doc.version = "3.0";
            else if (doc.version == "3.0")
                doc.version = "2.0";
            return doc;
        }

        /// <summary>
        /// Writes linked <c>.pcgsubgraph</c> files that were edited in this window.
        /// Call before saving the consumer graph so in-window edits persist to the asset.
        /// Skips assets that were only navigated into (not dirtied) to avoid clobbering
        /// newer disk saves from other windows.
        /// </summary>
        public bool TryFlushExternalNavigationAssets(out string error)
        {
            error = null;
            SaveVisibleScope();
            if (m_DirtyExternalNavDefIds.Count == 0)
                return true;

            foreach (var definitionId in m_DirtyExternalNavDefIds.ToList())
            {
                if (!m_ExternalNavRootGuidByDefId.TryGetValue(definitionId, out var guid))
                {
                    m_DirtyExternalNavDefIds.Remove(definitionId);
                    continue;
                }

                if (!TryBuildAssetDocumentFromExternalNav(definitionId, out var assetDoc, out var buildError))
                {
                    error = buildError;
                    return false;
                }

                var path = AssetDatabase.GUIDToAssetPath(guid);
                if (string.IsNullOrEmpty(path))
                {
                    error = $"SubgraphAsset GUID not found while flushing: {guid}";
                    return false;
                }

                var projectRoot = System.IO.Path.GetDirectoryName(Application.dataPath);
                var fullPath = System.IO.Path.GetFullPath(System.IO.Path.Combine(projectRoot, path));
                if (System.IO.File.Exists(fullPath))
                {
                    var diskHash = HashUtf8Content(System.IO.File.ReadAllText(fullPath));
                    if (m_ExternalNavLoadedContentHashByDefId.TryGetValue(definitionId, out var loadedHash) &&
                        !string.Equals(diskHash, loadedHash, StringComparison.Ordinal))
                    {
                        error =
                            $"SubgraphAsset '{path}' changed on disk since it was loaded in this window. " +
                            "Reload the asset (or discard local edits) before saving to avoid overwriting.";
                        return false;
                    }
                }

                LogSaveRepair(path, PcgGraphIntegrityRepair.RepairForSave(assetDoc));
                var json = PcgSubgraphAssetSerializer.ToJson(assetDoc);
                System.IO.File.WriteAllText(fullPath, json);
                m_ExternalNavLoadedContentHashByDefId[definitionId] = HashUtf8Content(json);
                m_DirtyExternalNavDefIds.Remove(definitionId);
            }

            return true;
        }

        /// <summary>
        /// Returns the in-memory source for a linked SubgraphAsset opened through this graph.
        /// This keeps preview cooks consistent with unsaved interface edits.
        /// </summary>
        internal bool TryGetLiveExternalSubgraphSourceJson(
            string assetGuid,
            out string sourceJson,
            out string error)
        {
            sourceJson = null;
            error = null;
            var canonical = PcgAssetGuidUtility.Canonicalize(assetGuid);
            var rootDefinitionId = m_ExternalNavRootGuidByDefId
                .FirstOrDefault(pair =>
                    string.Equals(
                        PcgAssetGuidUtility.Canonicalize(pair.Value),
                        canonical,
                        StringComparison.Ordinal))
                .Key;
            if (string.IsNullOrEmpty(rootDefinitionId))
                return false;

            SaveVisibleScope();
            if (!TryBuildAssetDocumentFromExternalNav(rootDefinitionId, out var assetDoc, out error))
                return false;

            sourceJson = PcgSubgraphAssetSerializer.ToJson(assetDoc, pretty: false);
            return true;
        }

        public static void StripExternalNavigationDefinitions(PcgGraphDocument doc)
        {
            if (doc?.subgraphs == null)
                return;
            doc.subgraphs.RemoveAll(sg =>
                sg != null &&
                !string.IsNullOrEmpty(sg.id) &&
                sg.id.StartsWith("__ext_", StringComparison.Ordinal));
        }

        private void OnDragUpdated(DragUpdatedEvent evt)
        {
            if (HasSubgraphAssetDrag())
            {
                DragAndDrop.visualMode = DragAndDropVisualMode.Copy;
                evt.StopPropagation();
            }
        }

        private void OnDragPerform(DragPerformEvent evt)
        {
            if (!HasSubgraphAssetDrag())
                return;

            DragAndDrop.AcceptDrag();
            var graphPos = contentViewContainer.WorldToLocal(evt.mousePosition);
            var offset = 0f;
            foreach (var obj in DragAndDrop.objectReferences)
            {
                if (obj is not PcgSubgraphAsset asset)
                    continue;
                var path = AssetDatabase.GetAssetPath(asset);
                var guid = AssetDatabase.AssetPathToGUID(path);
                if (!PcgAssetGuidUtility.IsValid(guid))
                    continue;
                CreateExternalSubgraphNode(guid, asset, graphPos + new Vector2(offset, 0f));
                offset += 40f;
            }

            evt.StopPropagation();
        }

        private static bool HasSubgraphAssetDrag()
        {
            if (DragAndDrop.objectReferences == null)
                return false;
            foreach (var obj in DragAndDrop.objectReferences)
            {
                if (obj is PcgSubgraphAsset)
                    return true;
            }
            return false;
        }

        public void CreateLibrarySubgraphNode(string libraryId, Vector2 graphPosition)
        {
            if (!PcgBuiltinLibrary.TryFind(libraryId, out var item))
            {
                Debug.LogWarning($"[PCG] Unknown builtin library entry: {libraryId}");
                return;
            }
            if (!PcgBuiltinLibrary.TryLoadAsset(item, out var guid, out var asset))
            {
                Debug.LogWarning($"[PCG] Builtin library asset missing: {PcgBuiltinLibrary.AssetPathFor(item)}");
                return;
            }
            CreateExternalSubgraphNode(guid, asset, graphPosition);
        }

        public void CreateLibraryNodeAndConnect(
            PcgBuiltinLibrary.LibraryItem item, Vector2 position, Port draggedPort)
        {
            if (!PcgBuiltinLibrary.TryLoadAsset(item, out var guid, out var asset))
            {
                Debug.LogWarning($"[PCG] Builtin library asset missing: {PcgBuiltinLibrary.AssetPathFor(item)}");
                return;
            }

            var view = CreateExternalSubgraphNode(guid, asset, position);
            if (view == null || draggedPort == null)
                return;

            var sourceNode = draggedPort.node as PcgGraphNodeBase;
            if (sourceNode == null)
                return;

            var isOutputDrag = draggedPort.direction == Direction.Output;
            var sourceHandle = draggedPort.userData as string ?? draggedPort.portName;
            var pinType = ResolveNodePinType(sourceNode, sourceHandle, isOutputDrag);

            RecordUndo("Connect Library Subgraph");
            if (isOutputDrag)
            {
                foreach (var pin in item.inputs)
                {
                    if (pin.pinType != "Any" && !PcgNodeManifest.PinTypesCompatible(pinType, pin.pinType))
                        continue;
                    var input = view.FindInputPort(pin.id);
                    if (input == null)
                        continue;
                    AddElement(draggedPort.ConnectTo(input));
                    break;
                }
            }
            else
            {
                foreach (var pin in item.outputs)
                {
                    if (pin.pinType != "Any" && !PcgNodeManifest.PinTypesCompatible(pin.pinType, pinType))
                        continue;
                    var output = view.FindOutputPort(pin.id);
                    if (output == null)
                        continue;
                    AddElement(output.ConnectTo(draggedPort));
                    break;
                }
            }
            CommitState();
        }

        internal PcgExternalSubgraphNodeView CreateExternalSubgraphNode(string assetGuid, PcgSubgraphAsset asset, Vector2 graphPosition)
        {
            if (asset == null || !asset.ImportSucceeded)
            {
                Debug.LogWarning("[PCG] Cannot drop an invalid SubgraphAsset.");
                return null;
            }

            var guid = PcgAssetGuidUtility.Canonicalize(assetGuid);
            if (!PcgSubgraphAssetSerializer.TryFromJson(asset.SourceJson, out var assetDoc, out var error))
            {
                Debug.LogError($"[PCG] Failed to read SubgraphAsset: {error}");
                return null;
            }

            if (m_HostWindow is PcgGraphEditorWindow host &&
                host.IsSubgraphAssetMode &&
                string.Equals(host.selectedGuid, guid, StringComparison.OrdinalIgnoreCase))
            {
                EditorUtility.DisplayDialog("Cannot Drop Subgraph Asset",
                    "A Subgraph Asset cannot reference itself.", "OK");
                return null;
            }

            RecordUndo("Add Subgraph Asset");
            var snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = PcgSubgraphAssetNaming.ResolveDisplayName(asset, assetDoc),
                inputs = assetDoc.inputs.Select(port => new PcgSubgraphPort
                {
                    id = port.id, name = port.name, pinType = port.pinType,
                }).ToList(),
                outputs = assetDoc.outputs.Select(port => new PcgSubgraphPort
                {
                    id = port.id, name = port.name, pinType = port.pinType,
                }).ToList(),
            };

            var data = new PcgNodeData();
            data.SetRaw("assetGuid", guid);
            var nodeId = PcgGraphNodeFactory.NextNodeId();
            var view = PcgGraphNodeFactory.Create(
                PcgStructuralNodeTypes.SubgraphAsset,
                nodeId,
                graphPosition,
                data,
                externalSnapshot: snapshot);
            AttachSubgraphNavigation(view);
            AddElement(view);
            ClearSelection();
            AddToSelection(view);
            view.BringToFront();
            if (m_RootDocument != null)
                m_RootDocument.version = "3.0";
            CommitState();
            NotifyDocumentChanged();
            return view;
        }

        private void CreateSubgraphAssetFromSelection()
        {
            var selectedViews = selection.OfType<PcgGraphNodeBase>()
                .Where(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput" &&
                               node.NodeType != "Output" &&
                               node.NodeType != PcgStructuralNodeTypes.SubgraphParentRef)
                .ToList();
            if (selectedViews.Count == 0)
                return;

            var selectedIds = new HashSet<string>(selectedViews.Select(node => node.NodeId));

            var savePath = EditorUtility.SaveFilePanelInProject(
                "Create Subgraph Asset",
                "NewSubgraph",
                "pcgsubgraph",
                "Choose a location inside Assets/ for the linked Subgraph asset.");
            if (string.IsNullOrEmpty(savePath))
                return;

            var scope = CaptureVisibleDocument();
            var selectedRecords = scope.nodes.Where(node => selectedIds.Contains(node.id)).Select(node => node.Clone()).ToList();
            var internalEdges = scope.edges
                .Where(edge => selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target))
                .Select(edge => edge.Clone()).ToList();
            var incoming = scope.edges
                .Where(edge => !selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var outgoing = scope.edges
                .Where(edge => selectedIds.Contains(edge.source) && !selectedIds.Contains(edge.target)).ToList();
            var outputSources = outgoing
                .Select(edge => (edge.source, edge.sourceHandle))
                .Distinct()
                .ToList();
            if (outputSources.Count > 1)
            {
                EditorUtility.DisplayDialog(
                    "Create Subgraph Asset",
                    "A Subgraph exposes one Output. Select a network with one distinct outgoing value, or merge the values before creating the Subgraph.",
                    "OK");
                return;
            }

            var assetDoc = new PcgSubgraphAssetDocument
            {
                version = "1.0",
                name = System.IO.Path.GetFileNameWithoutExtension(savePath),
                nodes = selectedRecords,
                edges = internalEdges,
            };

            var minY = selectedRecords.Min(node => node.position.y);
            var maxY = selectedRecords.Max(node => node.position.y);
            var centerX = selectedRecords.Average(node => node.position.x);
            var centerY = selectedRecords.Average(node => node.position.y);
            var inputNodeId = PcgGraphNodeFactory.NextNodeId();
            var outputNodeId = PcgGraphNodeFactory.NextNodeId();
            assetDoc.nodes.Add(new PcgGraphNodeRecord
            {
                id = inputNodeId,
                type = PcgStructuralNodeTypes.SubgraphInput,
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, minY - 160f)),
                data = new PcgNodeData(),
            });
            assetDoc.nodes.Add(new PcgGraphNodeRecord
            {
                id = outputNodeId,
                type = "Output",
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, maxY + 160f)),
                data = PcgNodeManifest.DefaultDataFor("Output"),
            });

            for (var i = 0; i < incoming.Count; i++)
            {
                var edge = incoming[i];
                var portId = Guid.NewGuid().ToString("N");
                assetDoc.inputs.Add(new PcgSubgraphPort
                {
                    id = portId,
                    name = string.IsNullOrEmpty(edge.targetHandle) ? $"in_{i + 1}" : edge.targetHandle,
                    pinType = PcgSubgraphInputUtility.AnyPinType,
                });
                assetDoc.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}",
                    source = inputNodeId,
                    target = edge.target,
                    sourceHandle = portId,
                    targetHandle = edge.targetHandle,
                });
            }

            var outputPortId = Guid.NewGuid().ToString("N");
            var outputSource = outputSources.FirstOrDefault();
            assetDoc.outputs.Add(new PcgSubgraphPort
            {
                id = outputPortId,
                name = string.IsNullOrEmpty(outputSource.sourceHandle) ? "Output" : outputSource.sourceHandle,
                pinType = outputSources.Count == 0
                    ? "Any"
                    : ResolveVisiblePinType(outputSource.source, outputSource.sourceHandle, output: true),
            });
            if (outputSources.Count == 1)
            {
                assetDoc.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}",
                    source = outputSource.source,
                    target = outputNodeId,
                    sourceHandle = outputSource.sourceHandle,
                    targetHandle = "in",
                });
            }

            PromoteBindingsIntoAsset(assetDoc, selectedIds);
            PopulateNestedSubgraphDefinitions(assetDoc);
            var normalizedDefinition = new PcgSubgraphDefinition
            {
                id = "__root__",
                name = assetDoc.name,
                inputs = assetDoc.inputs,
                outputs = assetDoc.outputs,
                nodes = assetDoc.nodes,
                edges = assetDoc.edges,
            };
            PcgSubgraphContractUtility.Synchronize(normalizedDefinition);
            assetDoc.inputs = normalizedDefinition.inputs;
            assetDoc.outputs = normalizedDefinition.outputs;
            assetDoc.nodes = normalizedDefinition.nodes;
            assetDoc.edges = normalizedDefinition.edges;

            var json = PcgSubgraphAssetSerializer.ToJson(assetDoc);
            try
            {
                System.IO.File.WriteAllText(savePath, json);
            }
            catch (Exception ex)
            {
                EditorUtility.DisplayDialog("Create Subgraph Asset Failed", ex.Message, "OK");
                return;
            }

            AssetDatabase.ImportAsset(savePath);
            var imported = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(savePath);
            if (imported == null || !imported.ImportSucceeded)
            {
                EditorUtility.DisplayDialog(
                    "Create Subgraph Asset Failed",
                    imported?.ImportError ?? "Import failed.",
                    "OK");
                return;
            }

            var assetGuid = AssetDatabase.AssetPathToGUID(savePath);
            RecordUndo("Create Subgraph Asset");

            var parentEdges = scope.edges
                .Where(edge => !selectedIds.Contains(edge.source) && !selectedIds.Contains(edge.target))
                .Select(edge => edge.Clone()).ToList();
            var instanceId = PcgGraphNodeFactory.NextNodeId();
            var snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = assetDoc.name,
                inputs = assetDoc.inputs.Select(port => new PcgSubgraphPort
                {
                    id = port.id, name = port.name, pinType = port.pinType,
                }).ToList(),
                outputs = assetDoc.outputs.Select(port => new PcgSubgraphPort
                {
                    id = port.id, name = port.name, pinType = port.pinType,
                }).ToList(),
            };

            for (var i = 0; i < incoming.Count; i++)
            {
                var edge = incoming[i];
                parentEdges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id,
                    source = edge.source,
                    target = instanceId,
                    sourceHandle = edge.sourceHandle,
                    targetHandle = assetDoc.inputs[i].id,
                });
            }

            foreach (var edge in outgoing)
            {
                parentEdges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id,
                    source = instanceId,
                    target = edge.target,
                    sourceHandle = assetDoc.outputs[0].id,
                    targetHandle = edge.targetHandle,
                });
            }

            var instanceData = new PcgNodeData();
            instanceData.SetRaw("assetGuid", PcgAssetGuidUtility.Canonicalize(assetGuid));
            var parentNodes = scope.nodes
                .Where(node => !selectedIds.Contains(node.id))
                .Select(node => node.Clone()).ToList();
            parentNodes.Add(new PcgGraphNodeRecord
            {
                id = instanceId,
                type = PcgStructuralNodeTypes.SubgraphAsset,
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, centerY)),
                data = instanceData,
                subgraphInterface = snapshot,
            });

            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                parent.nodes = parentNodes;
                parent.edges = parentEdges;
                m_RootDocument.version = "3.0";
                LoadScope(parent.nodes, parent.edges, ScopeParameters(parent), parent, clearUndo: false);
            }
            else
            {
                m_RootDocument.nodes = parentNodes;
                m_RootDocument.edges = parentEdges;
                m_RootDocument.version = "3.0";
                LoadScope(parentNodes, parentEdges, m_RootDocument.parameters, null, clearUndo: false);
            }

            CommitState();
            NotifyDocumentChanged();
        }

        private void PromoteInlineSubgraphToAsset(PcgSubgraphNodeView instance)
        {
            if (instance == null || instance.NodeType != PcgStructuralNodeTypes.Subgraph)
                return;

            var definitionId = instance.SubgraphDefinitionId;
            var definition = FindSubgraph(definitionId);
            if (definition == null)
            {
                EditorUtility.DisplayDialog(
                    "Promote Subgraph to Asset",
                    "Subgraph definition was not found in this graph.",
                    "OK");
                return;
            }

            if (CountInlineSubgraphInstanceReferences(definitionId) > 1)
            {
                EditorUtility.DisplayDialog(
                    "Promote Subgraph to Asset",
                    "This subgraph definition is referenced by more than one instance. " +
                    "Remove duplicate instances or give each a unique inline subgraph before promoting.",
                    "OK");
                return;
            }

            var internalNodeIds = new HashSet<string>(
                definition.nodes.Where(node => node != null).Select(node => node.id));

            var defaultName = string.IsNullOrEmpty(definition.name) ? "NewSubgraph" : definition.name;
            var savePath = EditorUtility.SaveFilePanelInProject(
                "Promote Subgraph to Asset",
                defaultName,
                "pcgsubgraph",
                "Choose a location inside Assets/ for the linked Subgraph asset.");
            if (string.IsNullOrEmpty(savePath))
                return;

            var assetDoc = PcgSubgraphAssetDocument.FromDefinition(definition);
            assetDoc.name = Path.GetFileNameWithoutExtension(savePath);
            PromoteBindingsIntoAsset(assetDoc, internalNodeIds);
            PopulateNestedSubgraphDefinitions(assetDoc);

            var json = PcgSubgraphAssetSerializer.ToJson(assetDoc);
            try
            {
                File.WriteAllText(savePath, json);
            }
            catch (Exception ex)
            {
                EditorUtility.DisplayDialog("Promote Subgraph to Asset", ex.Message, "OK");
                return;
            }

            AssetDatabase.ImportAsset(savePath);
            var imported = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(savePath);
            if (imported == null || !imported.ImportSucceeded)
            {
                EditorUtility.DisplayDialog(
                    "Promote Subgraph to Asset",
                    imported?.ImportError ?? "Import failed.",
                    "OK");
                return;
            }

            var assetGuid = PcgAssetGuidUtility.Canonicalize(AssetDatabase.AssetPathToGUID(savePath));
            var snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = assetDoc.name,
                inputs = assetDoc.inputs.Select(port => new PcgSubgraphPort
                {
                    id = port.id, name = port.name, pinType = port.pinType,
                }).ToList(),
                outputs = assetDoc.outputs.Select(port => new PcgSubgraphPort
                {
                    id = port.id, name = port.name, pinType = port.pinType,
                }).ToList(),
            };

            RecordUndo("Promote Subgraph to Asset");
            SaveVisibleScope();

            var scope = CaptureVisibleDocument();
            var instanceRecord = scope.nodes.FirstOrDefault(node => node.id == instance.NodeId);
            if (instanceRecord == null)
                return;

            var userTitle = instanceRecord.data?.GetRaw("__nodeTitle")?.ToString();
            instanceRecord.type = PcgStructuralNodeTypes.SubgraphAsset;
            instanceRecord.data = new PcgNodeData();
            instanceRecord.data.SetRaw("assetGuid", assetGuid);
            if (!string.IsNullOrEmpty(userTitle))
                instanceRecord.data.SetRaw("__nodeTitle", userTitle);
            instanceRecord.subgraphInterface = snapshot;

            m_RootDocument.subgraphs?.RemoveAll(subgraph =>
                subgraph != null && string.Equals(subgraph.id, definitionId, StringComparison.Ordinal));
            m_RootDocument.version = "3.0";

            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                parent.nodes = scope.nodes;
                parent.edges = scope.edges;
                LoadScope(parent.nodes, parent.edges, ScopeParameters(parent), parent, clearUndo: false);
            }
            else
            {
                m_RootDocument.nodes = scope.nodes;
                m_RootDocument.edges = scope.edges;
                LoadScope(scope.nodes, scope.edges, m_RootDocument.parameters, null, clearUndo: false);
            }

            CommitState();
            NotifyDocumentChanged();
        }

        private void PopulateNestedSubgraphDefinitions(PcgSubgraphAssetDocument assetDoc)
        {
            if (assetDoc == null || m_RootDocument?.subgraphs == null)
                return;

            var needed = new HashSet<string>(StringComparer.Ordinal);
            CollectReferencedSubgraphIds(assetDoc.nodes, needed);
            foreach (var nested in m_RootDocument.subgraphs)
            {
                if (nested != null && needed.Contains(nested.id))
                    assetDoc.subgraphs.Add(nested.Clone());
            }
        }

        private int CountInlineSubgraphInstanceReferences(string definitionId)
        {
            if (string.IsNullOrEmpty(definitionId))
                return 0;

            var count = 0;
            foreach (var node in EnumerateAllGraphNodeRecords())
            {
                if (node?.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var id = node.data?.GetRaw("subgraphId")?.ToString();
                if (string.Equals(id, definitionId, StringComparison.Ordinal))
                    count++;
            }

            return count;
        }

        private IEnumerable<PcgGraphNodeRecord> EnumerateAllGraphNodeRecords()
        {
            if (m_RootDocument?.nodes != null)
            {
                foreach (var node in m_RootDocument.nodes)
                    yield return node;
            }

            if (m_RootDocument?.subgraphs == null)
                yield break;

            foreach (var definition in m_RootDocument.subgraphs)
            {
                if (definition?.nodes == null)
                    continue;
                foreach (var node in definition.nodes)
                    yield return node;
            }
        }

        private void CollectReferencedSubgraphIds(List<PcgGraphNodeRecord> nodes, HashSet<string> output)
        {
            if (nodes == null)
                return;
            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var id = node.data?.GetRaw("subgraphId")?.ToString();
                if (!string.IsNullOrEmpty(id))
                    output.Add(id);
            }
        }

        internal void ReconcileExternalNodes(HashSet<string> changedGuids)
        {
            ReconcileExternalNodeRecords(changedGuids);
            foreach (var node in nodes.OfType<PcgExternalSubgraphNodeView>())
            {
                var guid = node.AssetGuid;
                if (changedGuids != null && changedGuids.Count > 0 && !changedGuids.Contains(guid))
                    continue;

                var path = AssetDatabase.GUIDToAssetPath(guid);
                if (string.IsNullOrEmpty(path))
                {
                    node.SetStatusError("Source SubgraphAsset is missing.");
                    continue;
                }

                var asset = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(path);
                if (asset == null || !asset.ImportSucceeded ||
                    !PcgSubgraphAssetSerializer.TryFromJson(asset.SourceJson, out var assetDoc, out _))
                {
                    node.SetStatusError(asset?.ImportError ?? "Source SubgraphAsset failed to load.");
                    continue;
                }

                var sourceSnapshot = new PcgSubgraphInterfaceSnapshot
                {
                    name = PcgSubgraphAssetNaming.ResolveDisplayName(asset, assetDoc),
                    inputs = assetDoc.inputs,
                    outputs = assetDoc.outputs,
                };
                bool IsConnected(string handle) =>
                    edges.Any(edge =>
                        (edge.input?.node == node && (edge.input.userData as string) == handle) ||
                        (edge.output?.node == node && (edge.output.userData as string) == handle));

                var reconcile = PcgExternalSubgraphInterfaceSync.Reconcile(
                    node.Snapshot, sourceSnapshot, IsConnected);
                node.SetSnapshot(reconcile.Snapshot, reconcile.GhostHandles);
                node.SetStatusError(reconcile.Compatible ? "" : reconcile.Error);
            }
        }

        internal PcgExternalSubgraphInterfaceSync.DocumentReconcileReport
            ReconcileExternalNodeRecords(HashSet<string> changedGuids = null)
        {
            SaveVisibleScope();
            return PcgExternalSubgraphInterfaceSync.ReconcileDocument(
                m_RootDocument,
                TryLoadExternalInterfaceSnapshot,
                changedGuids);
        }

        private bool TryLoadExternalInterfaceSnapshot(
            string assetGuid,
            out PcgSubgraphInterfaceSnapshot snapshot,
            out string contentHash,
            out string schemaVersion,
            out string error)
        {
            snapshot = null;
            contentHash = "";
            schemaVersion = PcgSubgraphAssetMigration.Version10;
            error = null;
            string sourceJson;
            if (!TryGetLiveExternalSubgraphSourceJson(assetGuid, out sourceJson, out error))
            {
                if (!string.IsNullOrEmpty(error))
                    return false;
                if (!TryLoadSubgraphAssetDocument(
                        assetGuid,
                        out var diskDocument,
                        out var assetPath,
                        out error))
                {
                    return false;
                }

                snapshot = new PcgSubgraphInterfaceSnapshot
                {
                    name = PcgSubgraphAssetNaming.ResolveDisplayName(assetPath, diskDocument),
                    inputs = diskDocument.inputs,
                    outputs = diskDocument.outputs,
                };
                contentHash = diskDocument.contentHash ?? "";
                schemaVersion = diskDocument.version ?? PcgSubgraphAssetMigration.Version10;
                return true;
            }

            if (!PcgSubgraphAssetSerializer.TryFromJson(
                    sourceJson,
                    out var liveDocument,
                    out error))
            {
                return false;
            }

            var path = AssetDatabase.GUIDToAssetPath(
                PcgAssetGuidUtility.Canonicalize(assetGuid));
            snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = PcgSubgraphAssetNaming.ResolveDisplayName(path, liveDocument),
                inputs = liveDocument.inputs,
                outputs = liveDocument.outputs,
            };
            contentHash = liveDocument.contentHash ?? "";
            schemaVersion = liveDocument.version ?? PcgSubgraphAssetMigration.Version10;
            return true;
        }

        private PcgExternalSubgraphInterfaceSync.ReconcileResult
            SyncExternalNavigationRootInterfaceToParentRecord(
                string definitionId,
                string instanceNodeId)
        {
            if (string.IsNullOrEmpty(definitionId) ||
                string.IsNullOrEmpty(instanceNodeId) ||
                !m_ExternalNavRootGuidByDefId.ContainsKey(definitionId))
            {
                return null;
            }

            var sourceDefinition = FindSubgraph(definitionId);
            if (sourceDefinition == null)
                return null;

            List<PcgGraphNodeRecord> parentNodes;
            List<PcgGraphEdgeRecord> parentEdges;
            if (m_SubgraphParents.Count > 0)
            {
                var parentDefinition = FindSubgraph(m_SubgraphParents[^1]);
                if (parentDefinition == null)
                    return null;
                parentNodes = parentDefinition.nodes;
                parentEdges = parentDefinition.edges;
            }
            else
            {
                parentNodes = m_RootDocument.nodes;
                parentEdges = m_RootDocument.edges;
            }

            var instanceRecord = parentNodes?.FirstOrDefault(node =>
                node != null &&
                node.id == instanceNodeId &&
                node.type == PcgStructuralNodeTypes.SubgraphAsset);
            if (instanceRecord == null)
                return null;

            return PcgExternalSubgraphInterfaceSync.ReconcileNodeRecord(
                instanceRecord,
                parentEdges,
                PcgSubgraphInterfaceSnapshot.FromDefinition(sourceDefinition));
        }

        private void ApplyExternalInterfaceSyncToVisibleNode(
            string instanceNodeId,
            PcgExternalSubgraphInterfaceSync.ReconcileResult sync)
        {
            if (sync == null || string.IsNullOrEmpty(instanceNodeId))
                return;

            var instance = nodes
                .OfType<PcgExternalSubgraphNodeView>()
                .FirstOrDefault(node => node.NodeId == instanceNodeId);
            if (instance == null)
                return;

            instance.SetSnapshot(sync.Snapshot, sync.GhostHandles);
            instance.SetStatusError(sync.Compatible ? "" : sync.Error);
        }

        private void SaveVisibleScope()
        {
            if (m_RootDocument == null)
                m_RootDocument = new PcgGraphDocument();
            var visible = CaptureVisibleDocument();
            if (IsEditingSubgraphInterface())
            {
                var definition = GetActiveInterfaceDefinition();
                if (definition != null)
                {
                    SyncInterfaceAnchorPositions(definition);
                    MergeVisibleScopeIntoDefinition(definition, visible);
                }
            }
            else if (IsInsideSubgraph)
            {
                var definition = FindSubgraph(m_CurrentSubgraphId);
                if (definition != null)
                {
                    MergeVisibleScopeIntoDefinition(definition, visible);
                    if (m_Blackboard != null)
                        definition.parameters = m_Blackboard.CollectParameters();
                }
            }
            else
            {
                m_RootDocument.nodes = visible.nodes;
                m_RootDocument.edges = visible.edges;
                m_RootDocument.parameters = visible.parameters;
            }
        }

        private static void MergeVisibleScopeIntoDefinition(
            PcgSubgraphDefinition definition,
            PcgGraphDocument visible)
        {
            var existingNodes = definition.nodes ?? new List<PcgGraphNodeRecord>();
            var hiddenNodes = existingNodes
                .Where(node => node != null && IsHiddenSubgraphInterfaceNodeType(node.type))
                .Select(node => node.Clone())
                .ToList();
            var hiddenNodeIds = new HashSet<string>(hiddenNodes.Select(node => node.id), StringComparer.Ordinal);

            var existingEdges = definition.edges ?? new List<PcgGraphEdgeRecord>();
            var interfaceEdges = existingEdges
                .Where(edge => edge != null &&
                               (hiddenNodeIds.Contains(edge.source) || hiddenNodeIds.Contains(edge.target)))
                .Select(edge => edge.Clone())
                .ToList();

            var mergedNodes = visible.nodes.Select(node => node.Clone()).ToList();
            var visibleIds = new HashSet<string>(mergedNodes.Select(node => node.id), StringComparer.Ordinal);
            foreach (var hidden in hiddenNodes)
            {
                if (!visibleIds.Contains(hidden.id))
                    mergedNodes.Add(hidden);
            }
            var mergedNodeIds = new HashSet<string>(
                mergedNodes.Where(node => node != null).Select(node => node.id),
                StringComparer.Ordinal);

            var mergedEdges = visible.edges
                .Where(edge => edge != null &&
                               mergedNodeIds.Contains(edge.source) &&
                               mergedNodeIds.Contains(edge.target))
                .Select(edge => edge.Clone())
                .ToList();
            var visibleEdgeIds = new HashSet<string>(
                mergedEdges.Where(edge => !string.IsNullOrEmpty(edge.id)).Select(edge => edge.id),
                StringComparer.Ordinal);
            foreach (var edge in interfaceEdges)
            {
                if (mergedNodeIds.Contains(edge.source) &&
                    mergedNodeIds.Contains(edge.target) &&
                    (string.IsNullOrEmpty(edge.id) || !visibleEdgeIds.Contains(edge.id)))
                {
                    mergedEdges.Add(edge);
                }
            }

            definition.nodes = mergedNodes;
            definition.edges = mergedEdges;
        }

        private PcgGraphDocument CaptureVisibleDocument()
        {
            var doc = new PcgGraphDocument { version = "1.0" };

            foreach (var node in nodes.OfType<PcgGraphNodeBase>())
            {
                if (!node.SavesToGraphDocument)
                    continue;

                var rect = node.GetPosition();
                var record = new PcgGraphNodeRecord
                {
                    id = node.NodeId,
                    type = node.NodeType,
                    position = PcgGraphPosition.FromVector2(rect.position),
                    data = node.CollectData(),
                };
                if (node is PcgExternalSubgraphNodeView external)
                    record.subgraphInterface = external.CollectInterfaceSnapshot();
                doc.nodes.Add(record);
            }

            foreach (var edge in edges)
            {
                if (edge.output?.node is not PcgGraphNodeBase source ||
                    edge.input?.node is not PcgGraphNodeBase target)
                {
                    continue;
                }
                if (!source.SavesToGraphDocument || !target.SavesToGraphDocument)
                    continue;

                var id = edge.userData as string;
                if (string.IsNullOrEmpty(id))
                    id = $"e{++m_EdgeCounter}";

                doc.edges.Add(new PcgGraphEdgeRecord
                {
                    id = id,
                    source = source.NodeId,
                    target = target.NodeId,
                    sourceHandle = edge.output.userData as string ?? edge.output.portName,
                    targetHandle = edge.input.userData as string ?? edge.input.portName,
                });
            }

            if (m_Blackboard != null)
                doc.parameters = m_Blackboard.CollectParameters();

            return doc;
        }

        private static void LogSaveRepair(string target, PcgGraphRepairReport report)
        {
            if (report?.Changed != true)
                return;
            Debug.LogWarning(
                $"[PCG] Save repair removed {report.RemovedOrphanEdges} orphan edge(s) " +
                $"from '{target}': {string.Join(", ", report.RemovedEdgePaths)}");
        }

        private void AttachSubgraphNavigation(PcgGraphNodeBase node)
        {
            if (node is PcgSubgraphNodeView subgraph && subgraph.NodeType == "Subgraph")
                subgraph.OpenRequested += EnterSubgraph;
            if (node is PcgExternalSubgraphNodeView external)
            {
                external.OpenSourceRequested += EnterExternalSubgraph;
                external.OpenSourceInNewWindowRequested += OpenExternalSubgraphAssetInNewWindow;
            }
        }

        private void EnterExternalSubgraph(string instanceNodeId, string assetGuid)
        {
            var canonical = PcgAssetGuidUtility.Canonicalize(assetGuid);
            if (!PcgAssetGuidUtility.IsValid(canonical))
            {
                Debug.LogError($"[PCG] Invalid SubgraphAsset GUID: {assetGuid}");
                return;
            }

            string definitionId;
            try
            {
                definitionId = PcgAssetGuidUtility.DefinitionIdForGuid(canonical);
            }
            catch (Exception ex)
            {
                Debug.LogError($"[PCG] {ex.Message}");
                return;
            }

            if (!EnsureExternalNavigationLoaded(canonical, definitionId, out var error))
            {
                Debug.LogError($"[PCG] {error}");
                return;
            }

            var externalInterfaceSync = SyncExternalNavigationRootInterfaceToParentRecord(
                definitionId,
                instanceNodeId);
            ApplyExternalInterfaceSyncToVisibleNode(instanceNodeId, externalInterfaceSync);
            EnterSubgraph(instanceNodeId, definitionId);
        }

        private void OpenExternalSubgraphAssetInNewWindow(string assetGuid)
        {
            var canonical = PcgAssetGuidUtility.Canonicalize(assetGuid);
            var path = AssetDatabase.GUIDToAssetPath(canonical);
            if (string.IsNullOrEmpty(path))
            {
                Debug.LogError($"[PCG] SubgraphAsset GUID not found: {canonical}");
                return;
            }

            PcgGraphEditorWindow.ShowGraphEditWindow(path);
        }

        private bool EnsureExternalNavigationLoaded(string guid, string definitionId, out string error)
        {
            error = null;
            if (m_ExternalNavRootGuidByDefId.ContainsKey(definitionId) && FindSubgraph(definitionId) != null)
                return true;

            var path = AssetDatabase.GUIDToAssetPath(guid);
            if (string.IsNullOrEmpty(path))
            {
                error = $"SubgraphAsset GUID not found: {guid}";
                return false;
            }

            var asset = AssetDatabase.LoadAssetAtPath<PcgSubgraphAsset>(path);
            if (asset == null || string.IsNullOrEmpty(asset.SourceJson))
            {
                error = $"Failed to load SubgraphAsset at '{path}'.";
                return false;
            }

            if (!PcgSubgraphAssetSerializer.TryFromJson(asset.SourceJson, out var assetDoc, out var parseError))
            {
                error = $"Failed to parse SubgraphAsset '{path}': {parseError}";
                return false;
            }

            UpsertExternalNavigationDefinitions(guid, definitionId, assetDoc);
            m_ExternalNavLoadedContentHashByDefId[definitionId] = HashUtf8Content(asset.SourceJson);
            m_DirtyExternalNavDefIds.Remove(definitionId);
            return true;
        }

        private void UpsertExternalNavigationDefinitions(
            string guid,
            string definitionId,
            PcgSubgraphAssetDocument assetDoc)
        {
            if (m_RootDocument == null)
                m_RootDocument = new PcgGraphDocument();
            m_RootDocument.subgraphs ??= new List<PcgSubgraphDefinition>();

            // Drop previous session copies for this asset before re-inserting.
            m_RootDocument.subgraphs.RemoveAll(sg =>
                sg != null &&
                !string.IsNullOrEmpty(sg.id) &&
                (sg.id == definitionId || sg.id.StartsWith(definitionId + "__", StringComparison.Ordinal)));

            var root = assetDoc.ToRootDefinition(definitionId);
            RemapInlineSubgraphIdsForExternalNav(root.nodes, definitionId);
            m_RootDocument.subgraphs.Add(root);

            foreach (var nested in assetDoc.subgraphs ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (nested == null)
                    continue;
                var remapped = nested.Clone();
                remapped.id = definitionId + "__" + nested.id;
                RemapInlineSubgraphIdsForExternalNav(remapped.nodes, definitionId);
                m_RootDocument.subgraphs.Add(remapped);
            }

            m_ExternalNavRootGuidByDefId[definitionId] = guid;
        }

        private bool TryBuildAssetDocumentFromExternalNav(
            string rootDefinitionId,
            out PcgSubgraphAssetDocument assetDoc,
            out string error)
        {
            assetDoc = null;
            error = null;
            var root = FindSubgraph(rootDefinitionId);
            if (root == null)
            {
                error = $"External navigation definition '{rootDefinitionId}' is missing.";
                return false;
            }

            assetDoc = PcgSubgraphAssetDocument.FromDefinition(root);
            UnremapInlineSubgraphIdsForExternalNav(assetDoc.nodes, rootDefinitionId);
            var prefix = rootDefinitionId + "__";
            assetDoc.subgraphs = (m_RootDocument.subgraphs ?? new List<PcgSubgraphDefinition>())
                .Where(sg => sg != null && sg.id != null && sg.id.StartsWith(prefix, StringComparison.Ordinal))
                .Select(sg =>
                {
                    var clone = sg.Clone();
                    clone.id = sg.id.Substring(prefix.Length);
                    UnremapInlineSubgraphIdsForExternalNav(clone.nodes, rootDefinitionId);
                    return clone;
                })
                .ToList();
            return true;
        }

        private static void RemapInlineSubgraphIdsForExternalNav(
            List<PcgGraphNodeRecord> nodes,
            string assetDefinitionId)
        {
            if (nodes == null)
                return;
            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var localId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (string.IsNullOrEmpty(localId) || localId.StartsWith("__ext_", StringComparison.Ordinal))
                    continue;
                node.data.SetRaw("subgraphId", assetDefinitionId + "__" + localId);
            }
        }

        private static void UnremapInlineSubgraphIdsForExternalNav(
            List<PcgGraphNodeRecord> nodes,
            string assetDefinitionId)
        {
            if (nodes == null)
                return;
            var prefix = assetDefinitionId + "__";
            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var id = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (id.StartsWith(prefix, StringComparison.Ordinal))
                    node.data.SetRaw("subgraphId", id.Substring(prefix.Length));
            }
        }

        private void ResetEdgeCounterFromDocument(PcgGraphDocument doc)
        {
            var max = 100;
            foreach (var edge in doc.edges)
            {
                if (edge.id != null && edge.id.StartsWith("e", System.StringComparison.Ordinal) &&
                    int.TryParse(edge.id.Substring(1), out var num) && num > max)
                {
                    max = num;
                }
            }

            m_EdgeCounter = max;
        }

        private GraphViewChange OnGraphViewChanged(GraphViewChange change)
        {
            if (change.elementsToRemove != null)
            {
                foreach (var element in change.elementsToRemove)
                {
                    if (element is PcgGraphNodeBase node)
                        node.DetachOverlays();
                }
            }

            if (m_SuppressUndo) return change;

            if (change.edgesToCreate != null && change.edgesToCreate.Count > 0)
            {
                var remaining = new List<Edge>();
                foreach (var edge in change.edgesToCreate)
                {
                    if (TryCommitInterfaceAnchorEdge(edge))
                        continue;
                    remaining.Add(edge);
                }

                change.edgesToCreate = remaining;
            }

            if (change.elementsToRemove != null && IsEditingSubgraphInterface())
            {
                var definition = GetActiveInterfaceDefinition();
                if (definition != null)
                {
                    foreach (var element in change.elementsToRemove)
                    {
                        if (element is Edge edge)
                            HandleInterfaceAnchorEdgeRemoved(definition, edge);
                    }
                }
            }

            // Filter valid edges to create
            if (change.edgesToCreate != null)
            {
                var valid = new List<Edge>();
                foreach (var edge in change.edgesToCreate)
                {
                    var others = edges.Where(e => e != edge);
                    if (PcgConnectionValidator.IsValidEdge(edge, others))
                        valid.Add(edge);
                }
                change.edgesToCreate = valid;
            }

            // Record undo for edge creation and/or element removal
            // (movedElements are handled separately by BeginDrag/EndDrag)
            if (m_PendingSnapshot == null && !m_PendingCommit)
            {
                bool hasEdgesToCreate = change.edgesToCreate != null && change.edgesToCreate.Count > 0;
                bool hasElementsToRemove = change.elementsToRemove != null && change.elementsToRemove.Count > 0;

                if (hasEdgesToCreate || hasElementsToRemove)
                {
                    string actionName;
                    if (hasEdgesToCreate && hasElementsToRemove)
                        actionName = "Reconnect Edge";
                    else if (hasEdgesToCreate)
                        actionName = "Connect Edge";
                    else if (change.elementsToRemove.Any(e => e is Edge))
                        actionName = "Disconnect Edge";
                    else
                        actionName = "Delete";

                    RecordUndo(actionName);
                    m_PendingCommit = true;
                    schedule.Execute(() =>
                    {
                        CommitState();
                        m_PendingCommit = false;
                        RefreshInspector();
                    });
                }
            }

            return change;
        }
    }

    public struct PcgNodeMeshStats
    {
        public int pointCount;
        public int faceCount;
        public int vertexCount;
        public int triangleCount;
        public bool hasBBox;
        public Vector3 bboxMin;
        public Vector3 bboxMax;
    }

    [System.Serializable]
    public class NodeStatsWrapper
    {
        public NodeStatEntry[] node_stats;
        public NodeGroupEntry[] node_groups;
        public NodeAttrEntry[] node_attrs;
    }

    [System.Serializable]
    public class NodeStatEntry
    {
        public string node_id;
        public string node_type;
        public int point_count;
        public int face_count;
        public int vertex_count;
        public int triangle_count;
        public bool has_bbox;
        public float bbox_min_x;
        public float bbox_min_y;
        public float bbox_min_z;
        public float bbox_max_x;
        public float bbox_max_y;
        public float bbox_max_z;
    }

    [System.Serializable]
    public class NodeAttrEntry
    {
        public string node_id;
        public string owner;
        public string name;
        public string type;
        public int tuple_size;
    }

    [System.Serializable]
    public class NodeGroupEntry
    {
        public string node_id;
        public string name;
        public string domain;
        public int count;
        public int[] members;
        public float[] edgeEndpoints;
        /// <summary>
        /// Packed face rings from the source node geometry:
        /// [vertCount, x,y,z * vertCount, ...] — used for Scene View highlight
        /// when MeshFilter is the final merge, not this node's mesh.
        /// </summary>
        public float[] facePolygons;
        /// <summary>
        /// Packed point XYZ from the source node geometry: [x,y,z, ...] —
        /// used for Scene View highlight so point groups do not index the
        /// final MeshFilter / PolygonPreview.
        /// </summary>
        public float[] pointPositions;
    }
}
