using System;
using System.Collections.Generic;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>Manifest-driven node view for Phase 4.1 UE primitive nodes.</summary>
    public sealed class PcgManifestNodeView : PcgGraphNodeBase
    {
        private readonly ManifestNodeDef _def;
        private readonly Dictionary<string, VisualElement> _fields = new();
        private readonly Dictionary<string, string> _enumValues = new();
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
                    "enum" => CreateEnumField(key, prop),
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

        private VisualElement CreateEnumField(string key, ManifestPropertyDef prop)
        {
            var choices = new List<string>();
            var labels = new List<string>();
            var defaultValue = prop.defaultValue?.ToString() ?? "";
            var selectedIndex = 0;

            for (var i = 0; i < prop.options.Count; i++)
            {
                var option = prop.options[i];
                choices.Add(option.value);
                labels.Add(string.IsNullOrEmpty(option.label) ? option.value : option.label);
                if (option.value == defaultValue)
                    selectedIndex = i;
            }

            if (choices.Count == 0)
            {
                choices.Add(defaultValue);
                labels.Add(defaultValue);
            }

            var popup = new PopupField<string>(key, labels, selectedIndex);
            popup.RegisterValueChangedCallback(evt =>
            {
                var index = labels.IndexOf(evt.newValue);
                if (index >= 0 && index < choices.Count)
                    _enumValues[key] = choices[index];
            });
            _enumValues[key] = choices[Mathf.Clamp(selectedIndex, 0, choices.Count - 1)];
            return popup;
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

        private void SetEnumPopup(string key, PopupField<string> popup, string value)
        {
            if (!_def.properties.TryGetValue(key, out var prop) || prop.options.Count == 0)
            {
                if (!string.IsNullOrEmpty(value))
                    popup.SetValueWithoutNotify(value);
                return;
            }

            var labels = new List<string>();
            var choices = new List<string>();
            var selectedIndex = 0;
            for (var i = 0; i < prop.options.Count; i++)
            {
                var option = prop.options[i];
                choices.Add(option.value);
                labels.Add(string.IsNullOrEmpty(option.label) ? option.value : option.label);
                if (option.value == value)
                    selectedIndex = i;
            }

            popup.SetValueWithoutNotify(labels[selectedIndex]);
            _enumValues[key] = choices[selectedIndex];
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
                    case PopupField<string> popup:
                        SetEnumPopup(key, popup, value?.ToString());
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
                    case PopupField<string>:
                        if (_enumValues.TryGetValue(key, out var enumValue))
                            data.SetRaw(key, enumValue);
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
