using System;
using System.Collections.Generic;
using System.Linq;
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
        private Label _groupBadgeLabel;

        public override string NodeType => _def.type;

        /// <summary>Manifest definition for this node (read-only access for Info panel).</summary>
        public ManifestNodeDef NodeDef => _def;

        /// <summary>Current node data (read-only access for Info panel).</summary>
        public PcgNodeData NodeData => _data;

        public PcgManifestNodeView(ManifestNodeDef def)
        {
            _def = def;
            title = def.displayName ?? def.type;
            BuildGroupBadge();
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
            _data = data ?? new PcgNodeData();
            SetUserTitle(_data.GetRaw("__nodeTitle")?.ToString() ?? "");
            UpdateGroupTooltip();
            UpdateGroupBadge();
        }

        public override PcgNodeData CollectData()
        {
            _data.SetRaw("__nodeTitle", GetUserTitle());
            return _data;
        }

        public override void SetPropertyValue(string key, object value)
        {
            _data.SetRaw(key, value);
            if (_def.properties.TryGetValue(key, out var prop) && prop.isGroupOutput)
            {
                UpdateGroupTooltip();
                UpdateGroupBadge();
            }
        }

        private void UpdateGroupTooltip()
        {
            var groups = new List<string>();
            foreach (var og in _def.outputGroups)
            {
                if (!string.IsNullOrEmpty(og.condition))
                {
                    var condVal = _data.GetRaw(og.condition);
                    if (condVal is bool b && !b)
                        continue;
                }

                string name = og.name;
                if (og.dynamic)
                {
                    var val = _data.GetRaw(og.name)?.ToString();
                    if (string.IsNullOrWhiteSpace(val))
                        continue;
                    name = val;
                }
                groups.Add($"{name} ({og.domain})");
            }

            tooltip = groups.Count > 0
                ? $"{_def.displayName} — outputs: {string.Join(", ", groups)}"
                : _def.displayName;
        }

        private void BuildGroupBadge()
        {
            _groupBadgeLabel = new Label
            {
                style =
                {
                    fontSize = 9,
                    color = new Color(0.55f, 0.75f, 0.55f),
                    unityTextAlign = TextAnchor.MiddleCenter,
                    paddingTop = 1,
                    paddingBottom = 1,
                    paddingLeft = 4,
                    paddingRight = 4,
                    borderTopWidth = 1,
                    borderTopColor = new Color(0.2f, 0.35f, 0.2f, 0.6f),
                    backgroundColor = new Color(0.12f, 0.18f, 0.12f, 1f),
                    display = DisplayStyle.None,
                },
            };
            extensionContainer.Add(_groupBadgeLabel);
        }

        private void UpdateGroupBadge()
        {
            if (_groupBadgeLabel == null)
                return;

            var count = 0;
            foreach (var og in _def.outputGroups)
            {
                if (!string.IsNullOrEmpty(og.condition))
                {
                    var condVal = _data.GetRaw(og.condition);
                    if (condVal is bool b && !b)
                        continue;
                }

                if (og.dynamic)
                {
                    var val = _data.GetRaw(og.name)?.ToString();
                    if (string.IsNullOrWhiteSpace(val))
                        continue;
                }
                ++count;
            }

            if (count > 0)
            {
                _groupBadgeLabel.text = $"🔗 {count} group{(count != 1 ? "s" : "")}";
                _groupBadgeLabel.style.display = DisplayStyle.Flex;
            }
            else
            {
                _groupBadgeLabel.style.display = DisplayStyle.None;
            }
        }

        private Port CreatePort(Direction direction, string portName, string label)
        {
            var capacity = direction == Direction.Input ? Port.Capacity.Single : Port.Capacity.Multi;
            var port = PcgPort.Create(Orientation.Vertical, direction, capacity, typeof(float));
            port.portName = "";
            port.tooltip = string.IsNullOrEmpty(label) ? portName : label;
            port.userData = portName;
            StyleHoudiniPin(port, direction);
            return port;
        }
    }
}
