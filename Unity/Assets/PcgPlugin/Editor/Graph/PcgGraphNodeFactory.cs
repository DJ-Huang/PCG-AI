using System;
using System.Collections.Generic;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public static class PcgGraphNodeFactory
    {
        private static int _nodeCounter = 100;

        public static void ResetCounterFromDocument(PcgGraphDocument doc)
        {
            var max = 0;
            foreach (var node in doc.nodes)
            {
                if (node.id != null && node.id.StartsWith("n", StringComparison.Ordinal) &&
                    int.TryParse(node.id.Substring(1), out var num) && num > max)
                {
                    max = num;
                }
            }

            _nodeCounter = Math.Max(_nodeCounter, max);
        }

        public static string NextNodeId() => $"n{++_nodeCounter}";

        public static PcgGraphNodeBase Create(
            string type,
            string id,
            Vector2 position,
            PcgNodeData data = null,
            PcgSubgraphDefinition interfaceDefinition = null,
            System.Func<string, PcgSubgraphDefinition> subgraphLookup = null,
            PcgSubgraphInterfaceSnapshot externalSnapshot = null)
        {
            if (type == "Subgraph")
            {
                var subgraphId = data?.GetRaw("subgraphId")?.ToString() ?? "";
                var definition = subgraphLookup?.Invoke(subgraphId);
                if (definition == null)
                    throw new ArgumentException($"Unknown subgraph: {subgraphId}");
                var subgraphNode = new PcgSubgraphNodeView(definition, PcgSubgraphNodeKind.Instance);
                subgraphNode.Initialize(id, position);
                subgraphNode.ApplyData(data);
                return subgraphNode;
            }

            if (type == PcgStructuralNodeTypes.SubgraphAsset)
            {
                var snapshot = externalSnapshot
                    ?? (interfaceDefinition != null
                        ? PcgSubgraphInterfaceSnapshot.FromDefinition(interfaceDefinition)
                        : new PcgSubgraphInterfaceSnapshot());
                var external = new PcgExternalSubgraphNodeView(snapshot);
                external.Initialize(id, position);
                external.ApplyData(data);
                return external;
            }

            if (type == PcgStructuralNodeTypes.SubgraphParentRef)
            {
                var parentRef = new PcgSubgraphParentRefNodeView();
                parentRef.Initialize(id, position);
                parentRef.ApplyData(data);
                return parentRef;
            }

            if (type == "SubgraphInput" || type == "SubgraphOutput")
            {
                if (interfaceDefinition == null)
                    throw new ArgumentException($"{type} can only exist inside a subgraph");
                var kind = type == "SubgraphInput"
                    ? PcgSubgraphNodeKind.Input
                    : PcgSubgraphNodeKind.Output;
                var interfaceNode = new PcgSubgraphNodeView(interfaceDefinition, kind);
                interfaceNode.Initialize(id, position);
                interfaceNode.ApplyData(data);
                return interfaceNode;
            }

            if (!PcgNodeManifest.TryGet(type, out var def))
                throw new ArgumentException($"Unknown node type: {type}");

            var node = new PcgManifestNodeView(def);
            node.Initialize(id, position);
            node.ApplyData(data ?? PcgNodeManifest.DefaultDataFor(type));
            return node;
        }
    }

    public enum PcgSubgraphNodeKind
    {
        Instance,
        Input,
        Output,
    }

    public sealed class PcgSubgraphNodeView : PcgGraphNodeBase
    {
        private readonly PcgSubgraphDefinition m_Definition;
        private readonly PcgSubgraphNodeKind m_Kind;
        private readonly Dictionary<string, Port> m_Inputs = new();
        private readonly Dictionary<string, Port> m_Outputs = new();
        private PcgNodeData m_Data = new();

        public override string NodeType => m_Kind switch
        {
            PcgSubgraphNodeKind.Input => "SubgraphInput",
            PcgSubgraphNodeKind.Output => "SubgraphOutput",
            _ => "Subgraph",
        };

        /// <summary>Args: instanceNodeId, definitionId.</summary>
        public event Action<string, string> OpenRequested;

        public string SubgraphDefinitionId => m_Definition.id;

        public string GetInputPinType(string handle) =>
            PortType(m_Kind == PcgSubgraphNodeKind.Output ? m_Definition.outputs : m_Definition.inputs, handle);

        public string GetOutputPinType(string handle) =>
            PortType(m_Kind == PcgSubgraphNodeKind.Input ? m_Definition.inputs : m_Definition.outputs, handle);

        private static string PortType(IEnumerable<PcgSubgraphPort> ports, string handle)
        {
            foreach (var port in ports)
                if (port.id == handle)
                    return string.IsNullOrEmpty(port.pinType) ? "Any" : port.pinType;
            return "Any";
        }

        public PcgSubgraphNodeView(PcgSubgraphDefinition definition, PcgSubgraphNodeKind kind)
        {
            m_Definition = definition ?? throw new ArgumentNullException(nameof(definition));
            m_Kind = kind;
            title = kind switch
            {
                PcgSubgraphNodeKind.Input => "Subgraph Input",
                PcgSubgraphNodeKind.Output => "Subgraph Output",
                _ => definition.name,
            };
        }

        protected override void BuildPorts()
        {
            if (m_Kind == PcgSubgraphNodeKind.Instance || m_Kind == PcgSubgraphNodeKind.Output)
            {
                foreach (var pin in m_Kind == PcgSubgraphNodeKind.Output ? m_Definition.outputs : m_Definition.inputs)
                {
                    var port = CreatePort(Direction.Input, pin.id, pin.pinType);
                    m_Inputs[pin.id] = port;
                    inputContainer.Add(port);
                }
            }

            if (m_Kind == PcgSubgraphNodeKind.Instance || m_Kind == PcgSubgraphNodeKind.Input)
            {
                foreach (var pin in m_Kind == PcgSubgraphNodeKind.Input ? m_Definition.inputs : m_Definition.outputs)
                {
                    var port = CreatePort(Direction.Output, pin.id, pin.pinType);
                    m_Outputs[pin.id] = port;
                    OutputPort = port;
                    outputContainer.Add(port);
                }
            }
        }

        public override Port FindInputPort(string handle = "in") =>
            m_Inputs.TryGetValue(handle ?? "in", out var port) ? port : InputPort;

        public override Port FindOutputPort(string handle = "out") =>
            m_Outputs.TryGetValue(handle ?? "out", out var port) ? port : OutputPort;

        public override void ApplyData(PcgNodeData data)
        {
            m_Data = data ?? new PcgNodeData();
            SetUserTitle(m_Data.GetRaw("__nodeTitle")?.ToString() ?? "");
        }

        public override PcgNodeData CollectData()
        {
            if (m_Kind == PcgSubgraphNodeKind.Instance)
                m_Data.SetRaw("subgraphId", m_Definition.id);
            m_Data.SetRaw("__nodeTitle", GetUserTitle());
            return m_Data;
        }

        protected override bool HandleDoubleClick()
        {
            if (m_Kind != PcgSubgraphNodeKind.Instance)
                return false;
            OpenRequested?.Invoke(NodeId, m_Definition.id);
            return true;
        }

        protected override void ApplyRenamedTitle(string newTitle)
        {
            if (m_Kind != PcgSubgraphNodeKind.Instance)
            {
                base.ApplyRenamedTitle(newTitle);
                return;
            }

            var name = string.IsNullOrEmpty(newTitle) ? m_Definition.id : newTitle;
            m_Definition.name = name;
            title = name;
            SetUserTitle("");

            var graphView = GetFirstAncestorOfType<PcgGraphView>();
            graphView?.RefreshSubgraphInstanceTitles(m_Definition.id);
        }

        internal void RefreshDefinitionTitle()
        {
            if (m_Kind != PcgSubgraphNodeKind.Instance)
                return;
            title = string.IsNullOrEmpty(m_Definition.name) ? m_Definition.id : m_Definition.name;
            UpdateDisplayedTitle();
        }
    }
}
