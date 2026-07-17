using System;
using System.Collections.Generic;
using System.Linq;
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
        private string m_LastStatsJson;
        private PcgGraphDocument m_RootDocument;
        private string m_CurrentSubgraphId;
        private string m_CurrentSubgraphInstanceId;
        private readonly List<string> m_SubgraphParents = new();
        private readonly List<string> m_SubgraphParentInstances = new();

        public static event Action<PcgGraphEditorWindow> GraphDocumentChanged;
        public event Action<string> SubgraphNavigationChanged;

        private static readonly SceneEditDomain[] s_SplineDomains = { SceneEditDomain.SplineControlPoint };
        private static readonly SceneEditDomain[] s_GroupDomains = { SceneEditDomain.Vertex, SceneEditDomain.Edge, SceneEditDomain.Face };

        private PcgSceneEditContext m_SceneEditContext = PcgSceneEditContext.ObjectMode;

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
                                    triangleCount = entry.triangle_count,
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
            SetupZoom(ContentZoomer.DefaultMinScale, 4f);
            this.AddManipulator(new ContentDragger());
            this.AddManipulator(new SelectionDragger());
            this.AddManipulator(new RectangleSelector());

            var grid = new GridBackground();
            Insert(0, grid);
            grid.StretchToParentSize();

            graphViewChanged = OnGraphViewChanged;

            RegisterCallback<MouseMoveEvent>(OnMouseMove);
            RegisterCallback<KeyDownEvent>(OnKeyDown);
            RegisterCallback<PointerDownEvent>(OnPointerDown, TrickleDown.TrickleDown);
            RegisterCallback<PointerUpEvent>(OnPointerUp);
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
            }
        }

        private PcgSubgraphDefinition FindSubgraph(string id) =>
            m_RootDocument?.subgraphs?.FirstOrDefault(subgraph => subgraph.id == id);

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

            RecordUndo("Create Subgraph");
            var scope = CaptureVisibleDocument();
            var selectedIds = new HashSet<string>(selectedViews.Select(node => node.NodeId));
            var selectedRecords = scope.nodes.Where(node => selectedIds.Contains(node.id)).ToList();
            var internalEdges = scope.edges.Where(edge =>
                selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var incoming = scope.edges.Where(edge =>
                !selectedIds.Contains(edge.source) && selectedIds.Contains(edge.target)).ToList();
            var outgoing = scope.edges.Where(edge =>
                selectedIds.Contains(edge.source) && !selectedIds.Contains(edge.target)).ToList();

            var subgraphId = NextSubgraphId();
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

        private string NextSubgraphId()
        {
            var counter = m_RootDocument.subgraphs.Count + 1;
            string id;
            do id = $"subgraph_{counter++}";
            while (FindSubgraph(id) != null);
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
            var state = EnsureUndoState();
            if (PcgGraphSerializer.TryFromJson(state.GraphJson, out var doc, out _))
                LoadDocument(doc, clearUndo: false);
            state.HandleUndoRedo();
            m_SuppressUndo = false;

            RefreshInspector();
            NotifyDocumentChanged();
            if (m_HostWindow is PcgGraphEditorWindow window)
                PcgGraphEditorCookBridge.NotifyGraphChanged(window, immediate: true);
            SceneView.RepaintAll();
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
            LoadScope(m_RootDocument.nodes, m_RootDocument.edges, m_RootDocument.parameters, null, clearUndo);
            SubgraphNavigationChanged?.Invoke(null);
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
                    FindSubgraph);
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
            return m_RootDocument ?? new PcgGraphDocument();
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
                doc.nodes.Add(new PcgGraphNodeRecord
                {
                    id = node.NodeId,
                    type = node.NodeType,
                    position = PcgGraphPosition.FromVector2(rect.position),
                    data = node.CollectData(),
                });
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
        public int triangleCount;
    }

    [System.Serializable]
    public class NodeStatsWrapper
    {
        public NodeStatEntry[] node_stats;
        public NodeGroupEntry[] node_groups;
    }

    [System.Serializable]
    public class NodeStatEntry
    {
        public string node_id;
        public string node_type;
        public int point_count;
        public int face_count;
        public int triangle_count;
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
    }
}
