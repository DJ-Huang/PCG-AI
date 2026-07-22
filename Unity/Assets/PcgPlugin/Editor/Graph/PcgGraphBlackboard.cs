using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Parameter definition panel (left side). Houdini-style: define parameters here,
    /// bind them to node properties in the Node Inspector.
    /// </summary>
    public sealed class PcgGraphBlackboard : VisualElement
    {
        private readonly List<PcgGraphParameter> m_Parameters = new();
        private readonly PcgGraphView m_GraphView;
        private VisualElement m_ParameterList;
        private int m_IdCounter = 1;

        public IReadOnlyList<PcgGraphParameter> Parameters => m_Parameters;

        public event Action OnParametersChanged;

        public PcgGraphBlackboard(PcgGraphView graphView)
        {
            m_GraphView = graphView;
            BuildUI();
        }

        private void BuildUI()
        {
            style.width = 300;
            style.minWidth = 240;
            style.borderRightWidth = 1;
            style.borderRightColor = new Color(0.15f, 0.15f, 0.15f);
            style.backgroundColor = new Color(0.22f, 0.22f, 0.22f);
            style.flexDirection = FlexDirection.Column;
            style.flexShrink = 1;

            var header = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    paddingLeft = 8,
                    paddingRight = 8,
                    paddingTop = 6,
                    paddingBottom = 6,
                    flexShrink = 0,
                },
            };

            var title = new Label("Parameters")
            {
                style =
                {
                    flexGrow = 1,
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.9f, 0.9f, 0.9f),
                },
            };
            header.Add(title);

            var addButton = new Button(AddParameter)
            {
                text = "+",
                style = { width = 24 },
            };
            header.Add(addButton);

            Add(header);

            m_ParameterList = new ScrollView
            {
                style =
                {
                    paddingLeft = 4,
                    paddingRight = 4,
                    flexGrow = 1,
                    flexShrink = 1,
                },
            };
            Add(m_ParameterList);
        }

        public void ToggleVisible()
        {
            style.display = style.display.value == DisplayStyle.Flex ? DisplayStyle.None : DisplayStyle.Flex;
        }

        public void LoadParameters(List<PcgGraphParameter> parameters)
        {
            m_Parameters.Clear();
            if (parameters != null)
            {
                foreach (var param in parameters)
                    m_Parameters.Add(param);
            }

            UpdateIdCounter();
            RebuildUI();
        }

        public List<PcgGraphParameter> CollectParameters()
        {
            return m_Parameters.Select(p => new PcgGraphParameter
            {
                id = p.id,
                name = p.name,
                type = p.type,
                defaultValue = p.defaultValue,
                exposed = p.exposed,
                targetNode = p.targetNode,
                targetProperty = p.targetProperty,
                hasRange = p.hasRange,
                minValue = p.minValue,
                maxValue = p.maxValue,
            }).ToList();
        }

        /// <summary>Find parameter bound to a specific node property, or null.</summary>
        public PcgGraphParameter FindBinding(string nodeId, string propertyKey)
        {
            return m_Parameters.FirstOrDefault(p =>
                p.targetNode == nodeId && p.targetProperty == propertyKey);
        }

        /// <summary>Bind a parameter to a node property.</summary>
        public void SetBinding(string parameterId, string nodeId, string propertyKey)
        {
            foreach (var p in m_Parameters)
            {
                if (p.targetNode == nodeId && p.targetProperty == propertyKey)
                {
                    p.targetNode = "";
                    p.targetProperty = "";
                }
            }

            var param = m_Parameters.FirstOrDefault(p => p.id == parameterId);
            if (param != null)
            {
                param.targetNode = nodeId;
                param.targetProperty = propertyKey;
            }

            OnParametersChanged?.Invoke();
        }

        /// <summary>Remove binding from a parameter.</summary>
        public void ClearBinding(string parameterId)
        {
            var param = m_Parameters.FirstOrDefault(p => p.id == parameterId);
            if (param != null)
            {
                param.targetNode = "";
                param.targetProperty = "";
                OnParametersChanged?.Invoke();
            }
        }

        /// <summary>Remove any binding targeting a specific node property.</summary>
        public void ClearBindingForNode(string nodeId, string propertyKey)
        {
            var changed = false;
            foreach (var p in m_Parameters)
            {
                if (p.targetNode == nodeId && p.targetProperty == propertyKey)
                {
                    p.targetNode = "";
                    p.targetProperty = "";
                    changed = true;
                }
            }
            if (changed)
                OnParametersChanged?.Invoke();
        }

        private void AddParameter()
        {
            m_GraphView.WithUndo("Add Parameter", () =>
            {
                var param = new PcgGraphParameter
                {
                    id = $"p{m_IdCounter++}",
                    name = "New Parameter",
                    type = "number",
                    defaultValue = "0",
                    exposed = true,
                };
                m_Parameters.Add(param);
                RebuildUI();
            });
            OnParametersChanged?.Invoke();
        }

        /// <summary>Create a new parameter with the given name/type/default and return it.</summary>
        public PcgGraphParameter CreateParameter(string name, string type, string defaultValue)
        {
            var param = new PcgGraphParameter
            {
                id = $"p{m_IdCounter++}",
                name = name,
                type = type,
                defaultValue = defaultValue,
                exposed = true,
            };
            m_Parameters.Add(param);
            RebuildUI();
            OnParametersChanged?.Invoke();
            return param;
        }

        private void RemoveParameter(PcgGraphParameter param)
        {
            m_GraphView.WithUndo("Remove Parameter", () =>
            {
                m_Parameters.Remove(param);
                RebuildUI();
            });
            OnParametersChanged?.Invoke();
        }

        /// <summary>Update a parameter default value (caller should wrap in WithUndo when needed).</summary>
        public void SetParameterDefault(string parameterId, string defaultValue)
        {
            var param = m_Parameters.FirstOrDefault(p => p.id == parameterId);
            if (param == null || param.defaultValue == defaultValue)
                return;

            param.defaultValue = defaultValue;
            OnParametersChanged?.Invoke();
        }

        private void RebuildUI()
        {
            m_ParameterList.Clear();

            foreach (var param in m_Parameters)
                m_ParameterList.Add(CreateParameterRow(param));
        }

        private VisualElement CreateParameterRow(PcgGraphParameter param)
        {
            var row = new VisualElement
            {
                style =
                {
                    marginBottom = 4,
                    paddingBottom = 4,
                    borderBottomWidth = 1,
                    borderBottomColor = new Color(0.3f, 0.3f, 0.3f),
                },
            };

            // Row 1: Name + Remove
            var nameRow = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row },
            };

            var nameField = new TextField { value = param.name };
            nameField.style.flexGrow = 1;
            nameField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Rename Parameter", () => param.name = evt.newValue);
                OnParametersChanged?.Invoke();
            });
            nameRow.Add(nameField);

            var removeBtn = new Button(() => RemoveParameter(param)) { text = "−" };
            removeBtn.style.width = 24;
            nameRow.Add(removeBtn);
            row.Add(nameRow);

            // Row 2: Type + Exposed
            var typeRow = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row, marginTop = 2 },
            };

            var typeChoices = new List<string> { "integer", "number", "boolean", "string" };
            var typeField = new PopupField<string>(typeChoices, param.type);
            typeField.style.flexGrow = 1;
            typeField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Parameter Type", () =>
                {
                    param.type = evt.newValue;
                    param.defaultValue = evt.newValue switch
                    {
                        "integer" => "0",
                        "number" => "0",
                        "boolean" => "false",
                        _ => "",
                    };
                });
                RebuildUI();
                OnParametersChanged?.Invoke();
            });
            typeRow.Add(typeField);

            var exposedToggle = new Toggle("Exposed") { value = param.exposed };
            exposedToggle.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Toggle Exposed", () => param.exposed = evt.newValue);
                OnParametersChanged?.Invoke();
            });
            typeRow.Add(exposedToggle);
            row.Add(typeRow);

            // Row 3: Default value
            var valueField = CreateValueField(param);
            if (valueField != null)
            {
                valueField.style.marginTop = 2;
                row.Add(valueField);
            }

            // Row 4: Range toggle + min/max (integer/number only)
            if (param.type == "integer" || param.type == "number")
            {
                var rangeRow = CreateRangeRow(param);
                if (rangeRow != null)
                    row.Add(rangeRow);
            }

            return row;
        }

        private VisualElement CreateRangeRow(PcgGraphParameter param)
        {
            var container = new VisualElement { style = { marginTop = 2 } };

            var toggleRow = new VisualElement { style = { flexDirection = FlexDirection.Row } };

            var rangeToggle = new Toggle("Range") { value = param.hasRange };
            rangeToggle.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Toggle Range", () => param.hasRange = evt.newValue);
                RebuildUI();
                OnParametersChanged?.Invoke();
            });
            toggleRow.Add(rangeToggle);
            container.Add(toggleRow);

            if (!param.hasRange)
                return container;

            var minMaxRow = new VisualElement
            {
                style = { marginTop = 2 },
            };

            var minField = new FloatField("Min") { value = param.minValue };
            minField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Range", () => param.minValue = evt.newValue);
                OnParametersChanged?.Invoke();
            });
            minMaxRow.Add(minField);

            var maxField = new FloatField("Max") { value = param.maxValue };
            maxField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Range", () => param.maxValue = evt.newValue);
                OnParametersChanged?.Invoke();
            });
            minMaxRow.Add(maxField);

            container.Add(minMaxRow);
            return container;
        }

        private VisualElement CreateValueField(PcgGraphParameter param)
        {
            if (param.hasRange && (param.type == "integer" || param.type == "number"))
                return CreateRangedParameterRow(param);

            switch (param.type)
            {
                case "integer":
                    var intField = new IntegerField("Default") { value = int.TryParse(param.defaultValue, out var iv) ? iv : 0 };
                    intField.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Change Default", () => param.defaultValue = evt.newValue.ToString());
                        OnParametersChanged?.Invoke();
                    });
                    return intField;

                case "number":
                    var floatField = new FloatField("Default") { value = float.TryParse(param.defaultValue, out var fv) ? fv : 0f };
                    floatField.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Change Default", () =>
                            param.defaultValue = evt.newValue.ToString(CultureInfo.InvariantCulture));
                        OnParametersChanged?.Invoke();
                    });
                    return floatField;

                case "boolean":
                    var toggle = new Toggle("Default") { value = bool.TryParse(param.defaultValue, out var bv) && bv };
                    toggle.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Change Default", () =>
                            param.defaultValue = evt.newValue ? "true" : "false");
                        OnParametersChanged?.Invoke();
                    });
                    return toggle;

                default:
                    var textField = new TextField("Default") { value = param.defaultValue };
                    textField.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Change Default", () => param.defaultValue = evt.newValue);
                        OnParametersChanged?.Invoke();
                    });
                    return textField;
            }
        }

        private VisualElement CreateRangedParameterRow(PcgGraphParameter param)
        {
            var isInteger = param.type == "integer";
            var current = isInteger
                ? (float.TryParse(param.defaultValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var iv) ? iv : 0f)
                : (float.TryParse(param.defaultValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var fv) ? fv : 0f);

            return PcgInspectorWidgets.CreateSliderRow(
                isInteger,
                param.minValue,
                param.maxValue,
                current,
                newValue =>
                {
                    var text = isInteger
                        ? Mathf.RoundToInt(newValue).ToString(CultureInfo.InvariantCulture)
                        : PcgInspectorWidgets.FormatFloat(newValue);
                    param.defaultValue = text;
                    OnParametersChanged?.Invoke();
                },
                label: "Default",
                onDragBegin: () => m_GraphView?.BeginDrag("Change Default"),
                onDragEnd: () => m_GraphView?.EndDrag(),
                onFieldCommit: newValue =>
                {
                    var text = isInteger
                        ? Mathf.RoundToInt(newValue).ToString(CultureInfo.InvariantCulture)
                        : PcgInspectorWidgets.FormatFloat(newValue);
                    m_GraphView.WithUndo("Change Default", () => param.defaultValue = text);
                    OnParametersChanged?.Invoke();
                });
        }

        private void UpdateIdCounter()
        {
            foreach (var param in m_Parameters)
            {
                if (param.id != null && param.id.StartsWith("p", StringComparison.Ordinal) &&
                    int.TryParse(param.id.Substring(1), out var num) && num >= m_IdCounter)
                {
                    m_IdCounter = num + 1;
                }
            }
        }
    }
}
