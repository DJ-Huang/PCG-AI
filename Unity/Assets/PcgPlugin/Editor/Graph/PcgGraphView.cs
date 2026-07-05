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
        private int m_EdgeCounter = 100;
        private EditorWindow m_HostWindow;
        private Vector2 m_LastMousePos;
        private PcgGraphBlackboard m_Blackboard;

        public PcgGraphBlackboard Blackboard
        {
            get => m_Blackboard;
            set => m_Blackboard = value;
        }

        public PcgGraphView()
        {
            m_SearchWindow = ScriptableObject.CreateInstance<PcgGraphSearchWindow>();
            m_SearchWindow.Initialize(this);

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
            evt.StopPropagation();
        }

        public override void BuildContextualMenu(ContextualMenuPopulateEvent evt)
        {
            base.BuildContextualMenu(evt);
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

        public PcgGraphNodeBase CreateNode(string type, Vector2 position)
        {
            var node = PcgGraphNodeFactory.Create(type, PcgGraphNodeFactory.NextNodeId(), position);
            AddElement(node);
            ClearSelection();
            AddToSelection(node);
            node.BringToFront();
            return node;
        }

        public void LoadDocument(PcgGraphDocument doc)
        {
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

            return change;
        }
    }
}
