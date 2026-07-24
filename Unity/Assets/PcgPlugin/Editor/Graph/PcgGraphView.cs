using System;
using System.Collections.Generic;
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
            if (json != m_LastStatsJson)
            {
                m_LastStatsJson = json;
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
            }
            return m_NodeMeshStats.TryGetValue(nodeId, out stats);
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
                if (nodeView != null)
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
                    node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput"))
            {
                evt.menu.AppendSeparator();
                evt.menu.AppendAction("Create Subgraph from Selection", _ => CreateSubgraphFromSelection());
                evt.menu.AppendAction("Create Subgraph Asset from Selection", _ => CreateSubgraphAssetFromSelection());
            }
        }

        public PcgSubgraphDefinition FindSubgraph(string id) =>
            m_RootDocument?.subgraphs?.FirstOrDefault(subgraph => subgraph.id == id);

        public PcgSubgraphDefinition FindSubgraphDefinition(string subgraphId) => FindSubgraph(subgraphId);

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
                LoadScope(parent.nodes, parent.edges, new List<PcgGraphParameter>(), parent, clearUndo: false);
            }
            else
            {
                m_CurrentSubgraphId = null;
                m_CurrentSubgraphInstanceId = null;
                LoadScope(m_RootDocument.nodes, m_RootDocument.edges, m_RootDocument.parameters, null, clearUndo: false);
            }
            SubgraphNavigationChanged?.Invoke(m_CurrentSubgraphId);
            FrameAll();
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
            LoadScope(definition.nodes, definition.edges, new List<PcgGraphParameter>(), definition, clearUndo: false);
            SubgraphNavigationChanged?.Invoke(definitionId);
            FrameAll();
        }

        private void CreateSubgraphFromSelection()
        {
            var selectedViews = selection.OfType<PcgGraphNodeBase>()
                .Where(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput")
                .ToList();
            if (selectedViews.Count == 0)
                return;

            var selectedIds = new HashSet<string>(selectedViews.Select(node => node.NodeId));
            IEnumerable<PcgGraphParameter> parameterSource = IsInsideSubgraph
                ? m_RootDocument?.parameters
                : m_Blackboard?.Parameters;
            var affectedBindings = PcgGraphParameterUtility.FindBindingsTargetingNodes(
                parameterSource, selectedIds);
            if (affectedBindings.Count > 0)
            {
                var names = string.Join(", ", affectedBindings.Select(parameter =>
                    string.IsNullOrEmpty(parameter.name) ? parameter.id : parameter.name));
                EditorUtility.DisplayDialog(
                    "Cannot Create Subgraph",
                    $"The selection contains node properties bound to graph parameter(s): {names}. " +
                    "Clear or move those bindings before creating a subgraph; otherwise the " +
                    "parameters would no longer target this graph scope.",
                    "OK");
                return;
            }

            RecordUndo("Create Subgraph");
            var scope = CaptureVisibleDocument();
            var selectedRecords = scope.nodes.Where(node => selectedIds.Contains(node.id)).ToList();
            var internalEdges = scope.edges.Where(edge =>
                selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var incoming = scope.edges.Where(edge =>
                !selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var outgoing = scope.edges.Where(edge =>
                selectedIds.Contains(edge.source) && !selectedIds.Contains(edge.target)).ToList();

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
                type = "SubgraphOutput",
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, maxY + 160f)),
                data = new PcgNodeData(),
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
                    pinType = ResolveVisiblePinType(edge.target, edge.targetHandle, output: false),
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

            for (var i = 0; i < outgoing.Count; i++)
            {
                var edge = outgoing[i];
                var portId = $"out_{i + 1}";
                definition.outputs.Add(new PcgSubgraphPort
                {
                    id = portId,
                    name = string.IsNullOrEmpty(edge.sourceHandle) ? portId : edge.sourceHandle,
                    pinType = ResolveVisiblePinType(edge.source, edge.sourceHandle, output: true),
                });
                definition.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}", source = edge.source, target = outputNodeId,
                    sourceHandle = edge.sourceHandle, targetHandle = portId,
                });
                parentEdges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id, source = instanceId, target = edge.target,
                    sourceHandle = portId, targetHandle = edge.targetHandle,
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

            m_RootDocument.subgraphs.Add(definition);
            if (IsInsideSubgraph)
            {
                var parent = FindSubgraph(m_CurrentSubgraphId);
                parent.nodes = parentNodes;
                parent.edges = parentEdges;
                LoadScope(parent.nodes, parent.edges, new List<PcgGraphParameter>(), parent, clearUndo: false);
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

        private string ResolveVisiblePinType(string nodeId, string handle, bool output)
        {
            var node = nodes.OfType<PcgGraphNodeBase>().FirstOrDefault(item => item.NodeId == nodeId);
            if (node == null)
                return "Any";
            return ResolveNodePinType(node, handle, output);
        }

        private static string ResolveNodePinType(PcgGraphNodeBase node, string handle, bool output)
        {
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
            LoadScope(definition.nodes, definition.edges, new List<PcgGraphParameter>(), definition, clearUndo: false);
            CommitState();
            NotifyDocumentChanged();
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

        internal void RefreshInspector() => m_Inspector?.OnSelectionChanged();

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
                else if (manifestNode.NodeType == "GroupCreate" || manifestNode.NodeType == "GroupCombine")
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
                m_SceneEditContext = PcgSceneEditContext.ObjectMode;
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
            LoadScope(definition.nodes, definition.edges, new List<PcgGraphParameter>(), definition, clearUndo: false);
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
            RecordUndo("Create Node");
            var node = PcgGraphNodeFactory.Create(
                type, PcgGraphNodeFactory.NextNodeId(), position, null,
                FindSubgraph(m_CurrentSubgraphId), FindSubgraph);
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
            m_Inspector?.OnSelectionChanged();
        }

        public override void RemoveFromSelection(ISelectable selectable)
        {
            base.RemoveFromSelection(selectable);
            RefreshAllNodeSelectionVisuals();
            UpdateSceneEditContext();
            m_Inspector?.OnSelectionChanged();
        }

        public override void ClearSelection()
        {
            base.ClearSelection();
            RefreshAllNodeSelectionVisuals();
            UpdateSceneEditContext();
            m_Inspector?.OnSelectionChanged();
        }

        public override EventPropagation DeleteSelection()
        {
            return base.DeleteSelection();
        }

        private bool CanDuplicateSelectedNodes() =>
            selection.OfType<PcgGraphNodeBase>()
                .Any(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput");

        private void DuplicateSelectedNodes()
        {
            if (m_DuplicateInProgress)
                return;

            var selectedViews = selection.OfType<PcgGraphNodeBase>()
                .Where(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput")
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

            if (compatible.Count == 0)
                return;

            // Convert screen position to graph position for node spawn
            Vector2 windowPos = m_HostWindow != null ? (Vector2)m_HostWindow.position.position : Vector2.zero;
            Vector2 panelPos = screenPosition - windowPos;
            Vector2 graphPos = PanelToGraphPosition(panelPos);

            m_SearchWindow.SetSpawnPosition(graphPos);
            m_SearchWindow.SetPortDragContext(port, compatible);

            SearchWindow.Open(new SearchWindowContext(screenPosition), m_SearchWindow);
        }

        public void CreateNodeAndConnect(string type, Vector2 position, Port draggedPort)
        {
            RecordUndo("Create Node from Port");

            m_SuppressUndo = true;

            var node = PcgGraphNodeFactory.Create(type, PcgGraphNodeFactory.NextNodeId(), position);
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

        public void LoadDocument(PcgGraphDocument doc, bool clearUndo = true)
        {
            m_RootDocument = doc ?? new PcgGraphDocument();
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
                rootDefinition.name = assetName ?? rootDefinition.name;
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
            return true;
        }

        private void LoadScope(
            List<PcgGraphNodeRecord> scopeNodes,
            List<PcgGraphEdgeRecord> scopeEdges,
            List<PcgGraphParameter> scopeParameters,
            PcgSubgraphDefinition interfaceDefinition,
            bool clearUndo)
        {
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
            var doc = ExportDocument().Clone();
            StripExternalNavigationDefinitions(doc);
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

                var json = PcgSubgraphAssetSerializer.ToJson(assetDoc);
                System.IO.File.WriteAllText(fullPath, json);
                m_ExternalNavLoadedContentHashByDefId[definitionId] = HashUtf8Content(json);
                m_DirtyExternalNavDefIds.Remove(definitionId);
            }

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

        internal void CreateExternalSubgraphNode(string assetGuid, PcgSubgraphAsset asset, Vector2 graphPosition)
        {
            if (asset == null || !asset.ImportSucceeded)
            {
                Debug.LogWarning("[PCG] Cannot drop an invalid SubgraphAsset.");
                return;
            }

            var guid = PcgAssetGuidUtility.Canonicalize(assetGuid);
            if (!PcgSubgraphAssetSerializer.TryFromJson(asset.SourceJson, out var assetDoc, out var error))
            {
                Debug.LogError($"[PCG] Failed to read SubgraphAsset: {error}");
                return;
            }

            if (m_HostWindow is PcgGraphEditorWindow host &&
                host.IsSubgraphAssetMode &&
                string.Equals(host.selectedGuid, guid, StringComparison.OrdinalIgnoreCase))
            {
                EditorUtility.DisplayDialog("Cannot Drop Subgraph Asset",
                    "A Subgraph Asset cannot reference itself.", "OK");
                return;
            }

            RecordUndo("Add Subgraph Asset");
            var snapshot = new PcgSubgraphInterfaceSnapshot
            {
                name = assetDoc.name ?? asset.AssetName,
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
        }

        private void CreateSubgraphAssetFromSelection()
        {
            var selectedViews = selection.OfType<PcgGraphNodeBase>()
                .Where(node => node.NodeType != "SubgraphInput" && node.NodeType != "SubgraphOutput")
                .ToList();
            if (selectedViews.Count == 0)
                return;

            var selectedIds = new HashSet<string>(selectedViews.Select(node => node.NodeId));
            IEnumerable<PcgGraphParameter> parameterSource = IsInsideSubgraph
                ? m_RootDocument?.parameters
                : m_Blackboard?.Parameters;
            var affectedBindings = PcgGraphParameterUtility.FindBindingsTargetingNodes(
                parameterSource, selectedIds);
            if (affectedBindings.Count > 0)
            {
                var names = string.Join(", ", affectedBindings.Select(parameter =>
                    string.IsNullOrEmpty(parameter.name) ? parameter.id : parameter.name));
                EditorUtility.DisplayDialog(
                    "Cannot Create Subgraph Asset",
                    $"The selection contains node properties bound to graph parameter(s): {names}.",
                    "OK");
                return;
            }

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
                type = PcgStructuralNodeTypes.SubgraphOutput,
                position = PcgGraphPosition.FromVector2(new Vector2(centerX, maxY + 160f)),
                data = new PcgNodeData(),
            });

            for (var i = 0; i < incoming.Count; i++)
            {
                var edge = incoming[i];
                var portId = Guid.NewGuid().ToString("N");
                assetDoc.inputs.Add(new PcgSubgraphPort
                {
                    id = portId,
                    name = string.IsNullOrEmpty(edge.targetHandle) ? $"in_{i + 1}" : edge.targetHandle,
                    pinType = ResolveVisiblePinType(edge.target, edge.targetHandle, output: false),
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

            for (var i = 0; i < outgoing.Count; i++)
            {
                var edge = outgoing[i];
                var portId = Guid.NewGuid().ToString("N");
                assetDoc.outputs.Add(new PcgSubgraphPort
                {
                    id = portId,
                    name = string.IsNullOrEmpty(edge.sourceHandle) ? $"out_{i + 1}" : edge.sourceHandle,
                    pinType = ResolveVisiblePinType(edge.source, edge.sourceHandle, output: true),
                });
                assetDoc.edges.Add(new PcgGraphEdgeRecord
                {
                    id = $"e{++m_EdgeCounter}",
                    source = edge.source,
                    target = outputNodeId,
                    sourceHandle = edge.sourceHandle,
                    targetHandle = portId,
                });
            }

            if (m_RootDocument?.subgraphs != null)
            {
                var needed = new HashSet<string>(StringComparer.Ordinal);
                CollectReferencedSubgraphIds(assetDoc.nodes, needed);
                foreach (var definition in m_RootDocument.subgraphs)
                {
                    if (definition != null && needed.Contains(definition.id))
                        assetDoc.subgraphs.Add(definition.Clone());
                }
            }

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

            for (var i = 0; i < outgoing.Count; i++)
            {
                var edge = outgoing[i];
                parentEdges.Add(new PcgGraphEdgeRecord
                {
                    id = edge.id,
                    source = instanceId,
                    target = edge.target,
                    sourceHandle = assetDoc.outputs[i].id,
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
                LoadScope(parent.nodes, parent.edges, new List<PcgGraphParameter>(), parent, clearUndo: false);
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
                    name = assetDoc.name,
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

        private void SaveVisibleScope()
        {
            if (m_RootDocument == null)
                m_RootDocument = new PcgGraphDocument();
            var visible = CaptureVisibleDocument();
            if (IsInsideSubgraph)
            {
                var definition = FindSubgraph(m_CurrentSubgraphId);
                if (definition != null)
                {
                    definition.nodes = visible.nodes;
                    definition.edges = visible.edges;
                }
            }
            else
            {
                m_RootDocument.nodes = visible.nodes;
                m_RootDocument.edges = visible.edges;
                m_RootDocument.parameters = visible.parameters;
            }
        }

        private PcgGraphDocument CaptureVisibleDocument()
        {
            var doc = new PcgGraphDocument { version = "1.0" };

            foreach (var node in nodes.OfType<PcgGraphNodeBase>())
            {
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
