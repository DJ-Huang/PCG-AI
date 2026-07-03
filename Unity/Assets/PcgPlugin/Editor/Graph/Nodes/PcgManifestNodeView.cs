using System;
using System.Collections.Generic;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>Manifest-driven node view for Phase 4.1 UE primitive nodes.</summary>
    public sealed class PcgManifestNodeView : PcgGraphNodeBase
    {
        private readonly ManifestNodeDef _def;
        private readonly Dictionary<string, VisualElement> _fields = new();
        private readonly Dictionary<string, Port> _inputPorts = new();

        public override string NodeType => _def.type;

        public PcgManifestNodeView(ManifestNodeDef def)
        {
            _def = def;
            title = def.displayName ?? def.type;
            BuildManifestBody();
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

        private void BuildManifestBody()
        {
            var body = new VisualElement();
            foreach (var (key, prop) in _def.properties)
            {
                VisualElement field = prop.type switch
                {
                    "integer" => CreateIntField(key, prop.defaultValue),
                    "number" => CreateFloatField(key, prop.defaultValue),
                    "boolean" => CreateToggleField(key, prop.defaultValue),
                    _ => CreateTextField(key, prop.defaultValue),
                };
                _fields[key] = field;
                body.Add(field);
            }

            if (body.childCount > 0)
                extensionContainer.Add(body);
        }

        private IntegerField CreateIntField(string key, object defaultValue)
        {
            var field = new IntegerField(key) { value = Convert.ToInt32(defaultValue ?? 0) };
            return field;
        }

        private FloatField CreateFloatField(string key, object defaultValue)
        {
            var field = new FloatField(key) { value = Convert.ToSingle(defaultValue ?? 0f) };
            return field;
        }

        private Toggle CreateToggleField(string key, object defaultValue)
        {
            var field = new Toggle(key) { value = Convert.ToBoolean(defaultValue ?? false) };
            return field;
        }

        private TextField CreateTextField(string key, object defaultValue)
        {
            var field = new TextField(key) { value = defaultValue?.ToString() ?? "" };
            return field;
        }

        private Port CreatePort(Direction direction, string portName, string label)
        {
            var capacity = Port.Capacity.Single;
            var port = InstantiatePort(Orientation.Horizontal, direction, capacity, typeof(float));
            port.portName = string.IsNullOrEmpty(label) ? portName : label;
            port.userData = portName;
            return port;
        }

        public override void ApplyData(PcgNodeData data)
        {
            foreach (var (key, field) in _fields)
            {
                var value = data.GetRaw(key);
                switch (field)
                {
                    case IntegerField intField:
                        intField.SetValueWithoutNotify(Convert.ToInt32(value ?? 0));
                        break;
                    case FloatField floatField:
                        floatField.SetValueWithoutNotify(Convert.ToSingle(value ?? 0f));
                        break;
                    case Toggle toggle:
                        toggle.SetValueWithoutNotify(Convert.ToBoolean(value ?? false));
                        break;
                    case TextField textField:
                        textField.SetValueWithoutNotify(value?.ToString() ?? "");
                        break;
                }
            }
        }

        public override PcgNodeData CollectData()
        {
            var data = new PcgNodeData();
            foreach (var (key, field) in _fields)
            {
                switch (field)
                {
                    case IntegerField intField:
                        data.SetRaw(key, intField.value);
                        break;
                    case FloatField floatField:
                        data.SetRaw(key, floatField.value);
                        break;
                    case Toggle toggle:
                        data.SetRaw(key, toggle.value);
                        break;
                    case TextField textField:
                        data.SetRaw(key, textField.value ?? "");
                        break;
                }
            }

            return data;
        }
    }
}
