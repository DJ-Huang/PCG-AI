using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgGraphView : GraphView
    {
        private readonly PcgGraphSearchWindow m_SearchWindow;
        private readonly PcgGraphState m_State;
        private int m_EdgeCounter = 100;
        private EditorWindow m_HostWindow;
        private Vector2 m_LastMousePos;
        private PcgGraphBlackboard m_Blackboard;
        private PcgNodeInspector m_Inspector;
        private bool m_SuppressUndo;
        private string m_PendingSnapshot;
        private string m_PendingAction;
        private bool m_PendingCommit;

        public PcgGraphState State => m_State;

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

        public PcgGraphView()
        {
            m_SearchWindow = ScriptableObject.CreateInstance<PcgGraphSearchWindow>();
            m_SearchWindow.Initialize(this);
            m_State = PcgGraphState.Create();

            style.flexGrow = 1;
            SetupZoom(ContentZoomer.DefaultMinScale, ContentZoomer.DefaultMaxScale);
            this.AddManipulator(new ContentDragger());
            this.AddManipulator(new SelectionDragger());
            this.AddManipulator(new RectangleSelector());

            var grid = new GridBackground();
            Insert(0, grid);
            grid.StretchToParentSize();

            graphViewChanged = OnGraphViewChanged;

            RegisterCallback<MouseMoveEvent>(evt => m_LastMousePos = evt.mousePosition);
            RegisterCallback<KeyDownEvent>(OnKeyDown);
            RegisterCallback<PointerDownEvent>(OnPointerDown, TrickleDown.TrickleDown);
            RegisterCallback<PointerUpEvent>(OnPointerUp);
            RegisterCallback<DetachFromPanelEvent>(_ => m_State?.Destroy());
        }

        public void SetHostWindow(EditorWindow window) => m_HostWindow = window;

        private void OnKeyDown(KeyDownEvent evt)
        {
            if (evt.target is not PcgGraphView)
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
                Blackboard?.ToggleVisible();
                evt.StopPropagation();
            }
            else if (evt.keyCode == KeyCode.I)
            {
                m_Inspector?.ToggleVisible();
                evt.StopPropagation();
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

                    if (!PcgNodeManifest.CanConnect(outType, inType, outHandle, inHandle))
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
            m_State.SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
            m_State.RegisterCompleteObjectUndo(actionName);
        }

        /// <summary>Update the proxy with the post-mutation state.
        /// Call AFTER applying a mutation.</summary>
        public void CommitState()
        {
            if (m_SuppressUndo) return;
            m_State.SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
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
            if (m_SuppressUndo || m_PendingSnapshot != null) return;
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
                m_State.SetGraphJson(m_PendingSnapshot);
                m_State.RegisterCompleteObjectUndo(m_PendingAction);
                m_State.SetGraphJson(currentJson);
            }
            m_PendingSnapshot = null;
            m_PendingAction = null;
        }

        /// <summary>Called by EditorWindow.Update() when version mismatch is detected.
        /// Restores the graph from the proxy's serialized JSON.</summary>
        public void RestoreFromUndoState()
        {
            m_SuppressUndo = true;
            if (PcgGraphSerializer.TryFromJson(m_State.GraphJson, out var doc, out _))
                LoadDocument(doc, clearUndo: false);
            m_State.HandleUndoRedo();
            m_SuppressUndo = false;
        }

        // ─── Pointer Events (node drag only; edge drag handled by EdgeConnector) ──

        private void OnPointerDown(PointerDownEvent evt)
        {
            if (m_SuppressUndo) return;

            if (evt.target is VisualElement ve &&
                ve.GetFirstAncestorOfType<PcgGraphNodeBase>() != null)
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
            var node = PcgGraphNodeFactory.Create(type, PcgGraphNodeFactory.NextNodeId(), position);
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
            m_Inspector?.OnSelectionChanged();
        }

        public override void RemoveFromSelection(ISelectable selectable)
        {
            base.RemoveFromSelection(selectable);
            m_Inspector?.OnSelectionChanged();
        }

        public override void ClearSelection()
        {
            base.ClearSelection();
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
            var pinType = isOutput
                ? PcgNodeManifest.GetOutputPinType(sourceNode.NodeType, handle)
                : PcgNodeManifest.GetInputPinType(sourceNode.NodeType, handle);

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
                var pinType = isOutputDrag
                    ? PcgNodeManifest.GetOutputPinType(sourceNode.NodeType, sourceHandle)
                    : PcgNodeManifest.GetInputPinType(sourceNode.NodeType, sourceHandle);

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
            m_SuppressUndo = true;

            DeleteElements(graphElements.ToList());
            PcgGraphNodeFactory.ResetCounterFromDocument(doc);

            var nodeViews = new Dictionary<string, PcgGraphNodeBase>();
            foreach (var record in doc.nodes)
            {
                var view = PcgGraphNodeFactory.Create(
                    record.type,
                    record.id,
                    record.position.ToVector2(),
                    record.data);
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
                m_State.ClearUndo();
                m_State.SetGraphJson(PcgGraphSerializer.ToJson(ExportDocument(), pretty: false));
            }
            m_Inspector?.OnSelectionChanged();
        }

        public PcgGraphDocument ExportDocument()
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
}
