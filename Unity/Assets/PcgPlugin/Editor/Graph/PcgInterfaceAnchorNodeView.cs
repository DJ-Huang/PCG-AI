using DJTechRuntime.PCG;
using UnityEditor.Experimental.GraphView;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Shared chrome for subgraph interface anchors — same Houdini pill + right title as manifest nodes.
    /// </summary>
    internal abstract class PcgInterfaceAnchorNodeBase : PcgGraphNodeBase
    {
        protected string PinType { get; private set; }

        public string PortId { get; private set; }

        public Port InterfacePort { get; protected set; }

        public override bool SavesToGraphDocument => false;

        protected override bool SupportsHoverRadialMenu => false;

        protected override bool SupportsRename => false;

        protected void InitializeInterfaceAnchor(PcgSubgraphPort port, Vector2 position, string kindLabel)
        {
            PortId = port.id;
            PinType = kindLabel == "Input"
                ? PcgSubgraphInputUtility.AnyPinType
                : string.IsNullOrEmpty(port.pinType) ? "Any" : port.pinType;
            viewDataKey = $"iface_{kindLabel.ToLowerInvariant()}_{port.id}";
            capabilities &= ~Capabilities.Deletable;
            title = kindLabel;
            Initialize($"iface_{kindLabel}_{port.id}", position);
            SetUserTitle(string.IsNullOrEmpty(port.name) ? port.id : port.name);
        }

        protected override string GetDefaultTitle() => title;

        public override void ApplyData(PcgNodeData data) { }

        public override PcgNodeData CollectData() => new();
    }

    /// <summary>
    /// Subgraph interface input stub (data edge lives on hidden SubgraphInput).
    /// </summary>
    internal sealed class PcgInterfaceInputAnchorView : PcgInterfaceAnchorNodeBase
    {
        public new Port OutputPort => InterfacePort;

        public override string NodeType => "SubgraphInterfaceInput";

        public PcgInterfaceInputAnchorView(PcgSubgraphPort port, Vector2 position = default)
        {
            InitializeInterfaceAnchor(port, position, "Input");
        }

        protected override void BuildPorts()
        {
            var port = CreatePort(Direction.Output, PortId, PinType);
            InterfacePort = port;
            base.OutputPort = port;
            outputContainer.Add(port);
        }

        public void PlaceLeftOf(PcgGraphNodeBase target, float offsetX = 100f)
        {
            if (target == null)
                return;
            var rect = target.GetPosition();
            SetPosition(new Rect(rect.x - offsetX - NodeWidth, rect.y, NodeWidth, NodeHeight));
        }
    }

    /// <summary>
    /// Subgraph interface output stub (data edge lives on hidden SubgraphOutput).
    /// </summary>
    internal sealed class PcgInterfaceOutputAnchorView : PcgInterfaceAnchorNodeBase
    {
        public new Port InputPort => InterfacePort;

        public override string NodeType => "SubgraphInterfaceOutput";

        public PcgInterfaceOutputAnchorView(PcgSubgraphPort port, Vector2 position = default)
        {
            InitializeInterfaceAnchor(port, position, "Output");
        }

        protected override void BuildPorts()
        {
            var port = CreatePort(Direction.Input, PortId, PinType);
            InterfacePort = port;
            base.InputPort = port;
            inputContainer.Add(port);
        }

        public void PlaceRightOf(PcgGraphNodeBase source, float offsetX = 100f)
        {
            if (source == null)
                return;
            var rect = source.GetPosition();
            SetPosition(new Rect(rect.xMax + offsetX, rect.y, NodeWidth, NodeHeight));
        }
    }
}
