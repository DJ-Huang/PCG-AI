using System;
using System.Collections.Generic;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>Manifest-driven node view for Phase 4.1+ UE primitive nodes.</summary>
    public sealed class PcgManifestNodeView : PcgGraphNodeBase
    {
        private readonly ManifestNodeDef _def;
        private readonly Dictionary<string, VisualElement> _fields = new();
        private readonly Dictionary<string, string> _enumValues = new();
        private readonly Dictionary<string, Port> _inputPorts = new();
        private PcgNodeData _data = new();

        public override string NodeType => _def.type;

        public PcgManifestNodeView(ManifestNodeDef def)
        {
            _def = def;
            title = def.displayName ?? def.type;
        }

        public Port GetInputPort(string pinId) =>
            _inputPorts.TryGetValue(pinId, out var port) ? port : InputPort;

        public override Port FindInputPort(string handle = "in") => GetInputPort(handle ?? "in");

        public override Port FindOutputPort(string handle = "out")
        {
            if (handle == "out" || string.IsNullOrEmpty(handle))
                return OutputPort;
            return OutputPort;
        }

        protected override void BuildPorts()
        {
            foreach (var pin in _def.inputs)
            {
                var port = CreatePort(Direction.Input, pin.id, pin.label);
                _inputPorts[pin.id] = port;
                inputContainer.Add(port);
            }

            foreach (var pin in _def.outputs)
            {
                OutputPort = CreatePort(Direction.Output, pin.id, pin.label);
                OutputPort.userData = pin.id;
                outputContainer.Add(OutputPort);
            }
        }

        public override void ApplyData(PcgNodeData data)
        {
            _data = data;
        }

        public override PcgNodeData CollectData()
        {
            return _data;
        }

        public override void SetPropertyValue(string key, object value)
        {
            _data.SetRaw(key, value);
        }

        private Port CreatePort(Direction direction, string portName, string label)
        {
            var capacity = Port.Capacity.Single;
            var port = InstantiatePort(Orientation.Horizontal, direction, capacity, typeof(float));
            port.portName = string.IsNullOrEmpty(label) ? portName : label;
            port.userData = portName;
            return port;
        }
    }
}
