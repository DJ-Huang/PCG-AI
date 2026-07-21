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
    /// <summary>
    /// GraphView node for linked <c>SubgraphAsset</c> instances.
    /// Ports are built from the persisted interface snapshot; incompatible ports become ghosts.
    /// </summary>
    public sealed class PcgExternalSubgraphNodeView : PcgGraphNodeBase
    {
        /// <summary>Type subtitle shown under the instance name (same pattern as renamed manifest nodes).</summary>
        private const string TypeSubtitle = "subgraph";

        private readonly Dictionary<string, Port> m_Inputs = new();
        private readonly Dictionary<string, Port> m_Outputs = new();
        private readonly HashSet<string> m_GhostHandles = new(StringComparer.Ordinal);
        private PcgNodeData m_Data = new();
        private PcgSubgraphInterfaceSnapshot m_Snapshot = new();
        private string m_StatusError = "";

        public override string NodeType => PcgStructuralNodeTypes.SubgraphAsset;

        public string AssetGuid =>
            PcgAssetGuidUtility.Canonicalize(m_Data.GetRaw("assetGuid")?.ToString() ?? "");

        public PcgSubgraphInterfaceSnapshot Snapshot => m_Snapshot?.Clone();

        /// <summary>instanceNodeId, assetGuid — enter linked asset in the same GraphView.</summary>
        public event Action<string, string> OpenSourceRequested;

        /// <summary>assetGuid — open the source asset in a dedicated Graph Editor window.</summary>
        public event Action<string> OpenSourceInNewWindowRequested;

        public PcgExternalSubgraphNodeView(PcgSubgraphInterfaceSnapshot snapshot)
        {
            m_Snapshot = snapshot?.Clone() ?? new PcgSubgraphInterfaceSnapshot();
            title = TypeSubtitle;
            RefreshDisplayedTitle();
        }

        protected override void BuildPorts()
        {
            RebuildPorts(m_Snapshot, m_GhostHandles);
        }

        public override Port FindInputPort(string handle = "in") =>
            m_Inputs.TryGetValue(handle ?? "in", out var port) ? port : InputPort;

        public override Port FindOutputPort(string handle = "out") =>
            m_Outputs.TryGetValue(handle ?? "out", out var port) ? port : OutputPort;

        public override void ApplyData(PcgNodeData data)
        {
            m_Data = data?.Clone() ?? new PcgNodeData();
            title = TypeSubtitle;
            RefreshDisplayedTitle();
        }

        public override PcgNodeData CollectData()
        {
            var data = m_Data?.Clone() ?? new PcgNodeData();
            var assetName = string.IsNullOrEmpty(m_Snapshot?.name) ? "Subgraph Asset" : m_Snapshot.name;
            var userTitle = GetUserTitle();
            // Only persist a rename when it differs from the asset display name so
            // source renames still refresh the default title.
            data.SetRaw(
                "__nodeTitle",
                string.IsNullOrEmpty(userTitle) || string.Equals(userTitle, assetName, StringComparison.Ordinal)
                    ? ""
                    : userTitle);
            return data;
        }

        public PcgSubgraphInterfaceSnapshot CollectInterfaceSnapshot() => m_Snapshot?.Clone();

        public void SetSnapshot(PcgSubgraphInterfaceSnapshot snapshot, IEnumerable<string> ghostHandles = null)
        {
            m_Snapshot = snapshot?.Clone() ?? new PcgSubgraphInterfaceSnapshot();
            title = TypeSubtitle;
            m_GhostHandles.Clear();
            if (ghostHandles != null)
            {
                foreach (var handle in ghostHandles)
                {
                    if (!string.IsNullOrEmpty(handle))
                        m_GhostHandles.Add(handle);
                }
            }
            RefreshDisplayedTitle();
            RebuildPorts(m_Snapshot, m_GhostHandles);
        }

        public void SetStatusError(string error)
        {
            m_StatusError = error ?? "";
            style.borderLeftColor = string.IsNullOrEmpty(m_StatusError)
                ? StyleKeyword.Null
                : new StyleColor(new Color(0.85f, 0.2f, 0.2f));
            style.borderLeftWidth = string.IsNullOrEmpty(m_StatusError) ? 0f : 3f;
            tooltip = string.IsNullOrEmpty(m_StatusError) ? "" : m_StatusError;
        }

        public string GetInputPinType(string handle) => PortType(m_Snapshot.inputs, handle);

        public string GetOutputPinType(string handle) => PortType(m_Snapshot.outputs, handle);

        public bool IsGhostHandle(string handle) => m_GhostHandles.Contains(handle);

        protected override bool HandleDoubleClick()
        {
            OpenSourceRequested?.Invoke(NodeId, AssetGuid);
            return true;
        }

        public override void BuildContextualMenu(ContextualMenuPopulateEvent evt)
        {
            base.BuildContextualMenu(evt);
            var guid = AssetGuid;
            if (string.IsNullOrEmpty(guid))
                return;
            evt.menu.AppendAction(
                "Open Subgraph Asset in New Window",
                _ => OpenSourceInNewWindowRequested?.Invoke(guid),
                DropdownMenuAction.AlwaysEnabled);
        }

        private void RebuildPorts(PcgSubgraphInterfaceSnapshot snapshot, IEnumerable<string> ghostHandles)
        {
            inputContainer.Clear();
            outputContainer.Clear();
            m_Inputs.Clear();
            m_Outputs.Clear();
            BuildPortList(snapshot, ghostHandles);
        }

        private void BuildPortList(PcgSubgraphInterfaceSnapshot snapshot, IEnumerable<string> ghostHandles)
        {
            var ghosts = new HashSet<string>(StringComparer.Ordinal);
            if (ghostHandles != null)
            {
                foreach (var handle in ghostHandles)
                {
                    if (!string.IsNullOrEmpty(handle))
                        ghosts.Add(handle);
                }
            }

            foreach (var pin in snapshot.inputs ?? Enumerable.Empty<PcgSubgraphPort>())
            {
                if (pin == null || string.IsNullOrEmpty(pin.id))
                    continue;
                var port = InstantiatePort(Direction.Input, pin.id, pin.name, pin.pinType);
                if (ghosts.Contains(pin.id))
                    MarkGhost(port);
                inputContainer.Add(port);
                m_Inputs[pin.id] = port;
            }

            foreach (var pin in snapshot.outputs ?? Enumerable.Empty<PcgSubgraphPort>())
            {
                if (pin == null || string.IsNullOrEmpty(pin.id))
                    continue;
                var port = InstantiatePort(Direction.Output, pin.id, pin.name, pin.pinType);
                if (ghosts.Contains(pin.id))
                    MarkGhost(port);
                outputContainer.Add(port);
                m_Outputs[pin.id] = port;
            }

            expanded = true;
            RefreshExpandedState();
        }

        private Port InstantiatePort(Direction direction, string handle, string displayName, string pinType)
        {
            // Match manifest / inline subgraph nodes: empty portName + Houdini pin styling
            // (GraphView's default portName would render a "Mesh" pill next to the connector).
            var port = PcgPort.Create(Orientation.Vertical, direction, Port.Capacity.Multi, typeof(float));
            port.portName = "";
            port.userData = handle;
            var label = string.IsNullOrEmpty(displayName) ? handle : displayName;
            port.tooltip = $"{label} ({pinType ?? "Any"})";
            StyleHoudiniPin(port, direction, pinType);
            return port;
        }

        private void RefreshDisplayedTitle()
        {
            var custom = m_Data?.GetRaw("__nodeTitle")?.ToString()?.Trim() ?? "";
            var assetName = string.IsNullOrEmpty(m_Snapshot?.name) ? "Subgraph Asset" : m_Snapshot.name;
            SetUserTitle(string.IsNullOrEmpty(custom) ? assetName : custom);
        }

        private static void MarkGhost(Port port)
        {
            if (port == null)
                return;
            port.portColor = new Color(0.85f, 0.25f, 0.25f);
            port.tooltip = (port.tooltip ?? "") + " [ghost — source interface changed]";
        }

        private static string PortType(IEnumerable<PcgSubgraphPort> ports, string handle)
        {
            if (ports == null || string.IsNullOrEmpty(handle))
                return "Any";
            foreach (var port in ports)
            {
                if (port != null && port.id == handle)
                    return string.IsNullOrEmpty(port.pinType) ? "Any" : port.pinType;
            }
            return "Any";
        }
    }

    /// <summary>
    /// Reconciles persisted SubgraphAsset snapshots against the saved source asset interface.
    /// </summary>
    public static class PcgExternalSubgraphInterfaceSync
    {
        public sealed class ReconcileResult
        {
            public PcgSubgraphInterfaceSnapshot Snapshot;
            public List<string> GhostHandles = new();
            public string Error;
            public bool Compatible = true;
        }

        public static ReconcileResult Reconcile(
            PcgSubgraphInterfaceSnapshot persisted,
            PcgSubgraphInterfaceSnapshot source,
            Func<string, bool> isHandleConnected)
        {
            var result = new ReconcileResult
            {
                Snapshot = persisted?.Clone() ?? new PcgSubgraphInterfaceSnapshot(),
            };

            if (source == null)
            {
                result.Compatible = false;
                result.Error = "Source SubgraphAsset is missing.";
                if (result.Snapshot.inputs != null)
                    result.GhostHandles.AddRange(result.Snapshot.inputs.Select(port => port.id));
                if (result.Snapshot.outputs != null)
                    result.GhostHandles.AddRange(result.Snapshot.outputs.Select(port => port.id));
                return result;
            }

            result.Snapshot.name = source.name ?? result.Snapshot.name;
            result.Snapshot.inputs = MergePorts(result.Snapshot.inputs, source.inputs, isHandleConnected, result);
            result.Snapshot.outputs = MergePorts(result.Snapshot.outputs, source.outputs, isHandleConnected, result);
            return result;
        }

        private static List<PcgSubgraphPort> MergePorts(
            List<PcgSubgraphPort> persisted,
            List<PcgSubgraphPort> source,
            Func<string, bool> isHandleConnected,
            ReconcileResult result)
        {
            persisted ??= new List<PcgSubgraphPort>();
            source ??= new List<PcgSubgraphPort>();
            var sourceById = source
                .Where(port => port != null && !string.IsNullOrEmpty(port.id))
                .ToDictionary(port => port.id, StringComparer.Ordinal);
            var merged = new List<PcgSubgraphPort>();
            var seen = new HashSet<string>(StringComparer.Ordinal);

            foreach (var port in persisted)
            {
                if (port == null || string.IsNullOrEmpty(port.id))
                    continue;
                seen.Add(port.id);
                if (!sourceById.TryGetValue(port.id, out var sourcePort))
                {
                    if (isHandleConnected != null && isHandleConnected(port.id))
                    {
                        merged.Add(ClonePort(port));
                        result.GhostHandles.Add(port.id);
                        result.Compatible = false;
                        result.Error = $"Source removed connected port '{port.id}'.";
                    }
                    continue;
                }

                if (!string.Equals(port.pinType ?? "Any", sourcePort.pinType ?? "Any", StringComparison.Ordinal))
                {
                    if (isHandleConnected != null && isHandleConnected(port.id))
                    {
                        merged.Add(ClonePort(port));
                        result.GhostHandles.Add(port.id);
                        result.Compatible = false;
                        result.Error = $"Port '{port.id}' pinType changed.";
                        continue;
                    }
                }

                merged.Add(ClonePort(sourcePort));
            }

            foreach (var port in source)
            {
                if (port == null || string.IsNullOrEmpty(port.id) || seen.Contains(port.id))
                    continue;
                merged.Add(ClonePort(port));
            }

            return merged;
        }

        private static PcgSubgraphPort ClonePort(PcgSubgraphPort port)
        {
            return new PcgSubgraphPort
            {
                id = port.id,
                name = port.name,
                pinType = port.pinType,
            };
        }
    }
}
