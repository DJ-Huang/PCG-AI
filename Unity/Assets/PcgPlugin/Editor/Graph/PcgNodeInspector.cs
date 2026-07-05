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
    /// Houdini-style Node Inspector (right panel).
    /// Shows the selected node's properties with value fields,
    /// a bind-to-parameter dropdown, and a promote-to-parameter button.
    /// </summary>
    public sealed class PcgNodeInspector : VisualElement
    {
        private readonly PcgGraphView m_GraphView;
        private readonly PcgGraphBlackboard m_Blackboard;
        private VisualElement m_Body;
        private PcgGraphNodeBase m_CurrentNode;

        public PcgNodeInspector(PcgGraphView graphView, PcgGraphBlackboard blackboard)
        {
            m_GraphView = graphView;
            m_Blackboard = blackboard;
            BuildUI();
        }

        private void BuildUI()
        {
            style.width = 280;
            style.minWidth = 240;
            style.borderLeftWidth = 1;
            style.borderLeftColor = new Color(0.15f, 0.15f, 0.15f);
            style.backgroundColor = new Color(0.22f, 0.22f, 0.22f);
            style.flexShrink = 0;

            var header = new Label("Inspector")
            {
                style =
                {
                    paddingLeft = 8,
                    paddingRight = 8,
                    paddingTop = 6,
                    paddingBottom = 6,
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.9f, 0.9f, 0.9f),
                },
            };
            Add(header);

            m_Body = new VisualElement
            {
                style =
                {
                    paddingLeft = 6,
                    paddingRight = 6,
                    paddingBottom = 6,
                },
            };
            Add(m_Body);

            ShowEmpty();
            style.display = DisplayStyle.None;
        }

        public void ToggleVisible()
        {
            style.display = style.display.value == DisplayStyle.Flex ? DisplayStyle.None : DisplayStyle.Flex;
        }

        public void OnSelectionChanged()
        {
            var selected = m_GraphView.selection.OfType<PcgGraphNodeBase>().FirstOrDefault();
            if (selected == null)
            {
                ShowEmpty();
                return;
            }

            ShowNode(selected);
        }

        private void ShowEmpty()
        {
            m_CurrentNode = null;
            m_Body.Clear();
            if (style.display.value == DisplayStyle.Flex)
                style.display = DisplayStyle.None;
        }

        private void ShowNode(PcgGraphNodeBase node)
        {
            m_CurrentNode = node;
            m_Body.Clear();
            style.display = DisplayStyle.Flex;

            var titleLabel = new Label(node.title)
            {
                style =
                {
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.85f, 0.85f, 0.85f),
                    paddingBottom = 4,
                    borderBottomWidth = 1,
                    borderBottomColor = new Color(0.3f, 0.3f, 0.3f),
                    marginBottom = 4,
                },
            };
            m_Body.Add(titleLabel);

            var typeLabel = new Label($"Type: {node.NodeType}")
            {
                style = { color = new Color(0.6f, 0.6f, 0.6f), fontSize = 10, marginBottom = 6 },
            };
            m_Body.Add(typeLabel);

            if (node is PcgManifestNodeView manifestNode)
                ShowManifestProperties(manifestNode);
            else
                ShowLegacyProperties(node);
        }

        // ─── Manifest nodes ──────────────────────────────────────────

        private void ShowManifestProperties(PcgManifestNodeView node)
        {
            if (!PcgNodeManifest.TryGet(node.NodeType, out var def) || def.properties.Count == 0)
            {
                m_Body.Add(new Label("(no parameters)") { style = { color = new Color(0.5f, 0.5f, 0.5f) } });
                return;
            }

            foreach (var (key, prop) in def.properties)
                m_Body.Add(CreatePropertyRow(node, key, prop));
        }

        private VisualElement CreatePropertyRow(PcgManifestNodeView node, string key, ManifestPropertyDef prop)
        {
            var container = new VisualElement { style = { marginBottom = 6 } };

            // Row 1: label + promote button + bind dropdown
            var headerRow = new VisualElement { style = { flexDirection = FlexDirection.Row } };

            var label = new Label(key)
            {
                style =
                {
                    flexGrow = 1,
                    color = new Color(0.8f, 0.8f, 0.8f),
                    unityFontStyleAndWeight = FontStyle.Bold,
                },
            };
            headerRow.Add(label);

            // Promote-to-parameter button
            var promoteBtn = new Button(() => PromoteToParameter(node, key, prop))
            {
                text = "+",
                tooltip = "Promote to parameter",
            };
            promoteBtn.style.width = 22;
            headerRow.Add(promoteBtn);

            // Bind dropdown
            var (bindOptions, paramIds, currentIdx) = BuildBindOptions(node.NodeId, key, prop.type);
            var bindPopup = new PopupField<string>(bindOptions, currentIdx);
            bindPopup.style.width = 90;
            bindPopup.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Bind Parameter", () =>
                {
                    var idx = bindOptions.IndexOf(evt.newValue);
                    var paramId = idx >= 0 && idx < paramIds.Count ? paramIds[idx] : "";
                    if (string.IsNullOrEmpty(paramId))
                        m_Blackboard.ClearBindingForNode(node.NodeId, key);
                    else
                        m_Blackboard.SetBinding(paramId, node.NodeId, key);
                });
                ShowNode(node);
            });
            headerRow.Add(bindPopup);
            container.Add(headerRow);

            // Row 2: value or bound label
            var binding = m_Blackboard.FindBinding(node.NodeId, key);
            container.Add(CreateValueField(key, prop, node, binding));
            return container;
        }

        private VisualElement CreateValueField(string key, ManifestPropertyDef prop, PcgManifestNodeView node, PcgGraphParameter binding)
        {
            var wrapper = new VisualElement { style = { marginTop = 2 } };

            if (binding != null)
            {
                if (binding.hasRange && (binding.type == "integer" || binding.type == "number"))
                {
                    var val = float.TryParse(binding.defaultValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var fv) ? fv : binding.minValue;
                    var slider = new Slider(binding.minValue, binding.maxValue) { value = Mathf.Clamp(val, binding.minValue, binding.maxValue) };
                    var valLabel = new Label(binding.defaultValue) { style = { color = new Color(0.4f, 0.7f, 1.0f), fontSize = 10 } };
                    slider.RegisterValueChangedCallback(_ => { });
                    var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                    slider.style.flexGrow = 1;
                    row.Add(slider);
                    row.Add(valLabel);
                    wrapper.Add(new Label($"→ {binding.name}") { style = { color = new Color(0.4f, 0.7f, 1.0f), unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 2 } });
                    wrapper.Add(row);
                }
                else
                {
                    wrapper.Add(new Label($"→ {binding.name} ({binding.defaultValue})")
                    {
                        style = { color = new Color(0.4f, 0.7f, 1.0f), unityFontStyleAndWeight = FontStyle.Italic, paddingTop = 2, paddingBottom = 2 },
                    });
                }
                return wrapper;
            }

            var currentVal = node.CollectData().GetRaw(key);

            // Use slider if property or manifest defines range
            if (prop.hasRange && (prop.type == "integer" || prop.type == "number"))
            {
                var val = Convert.ToSingle(currentVal ?? prop.defaultValue ?? 0f, CultureInfo.InvariantCulture);
                var clamped = Mathf.Clamp(val, prop.minimum, prop.maximum);
                var slider = new Slider(prop.minimum, prop.maximum) { value = clamped };
                var valLabel = new Label(clamped.ToString(CultureInfo.InvariantCulture)) { style = { color = new Color(0.8f, 0.8f, 0.8f), fontSize = 10, marginLeft = 4, minWidth = 40 } };
                slider.style.flexGrow = 1;
                slider.RegisterCallback<PointerDownEvent>(_ => m_GraphView.BeginDrag("Change Property"));
                slider.RegisterValueChangedCallback(evt =>
                {
                    node.SetPropertyValue(key, evt.newValue);
                    valLabel.text = evt.newValue.ToString(CultureInfo.InvariantCulture);
                });
                slider.RegisterCallback<PointerUpEvent>(_ => m_GraphView.EndDrag());
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                row.Add(slider);
                row.Add(valLabel);
                wrapper.Add(row);
                return wrapper;
            }

            VisualElement field = prop.type switch
            {
                "integer" => MakeIntField(key, currentVal, v => node.SetPropertyValue(key, v)),
                "number" => MakeFloatField(key, currentVal, v => node.SetPropertyValue(key, v)),
                "boolean" => MakeToggleField(key, currentVal, v => node.SetPropertyValue(key, v)),
                "enum" => MakeEnumField(key, prop, currentVal, v => node.SetPropertyValue(key, v)),
                _ => MakeTextField(key, currentVal, v => node.SetPropertyValue(key, v)),
            };
            wrapper.Add(field);
            return wrapper;
        }

        // ─── Legacy nodes ────────────────────────────────────────────

        private void ShowLegacyProperties(PcgGraphNodeBase node)
        {
            var props = node.NodeType switch
            {
                PcgNodeTypes.ParseConfig => new[] { ("seed", "integer", "Seed"), ("density", "number", "Density") },
                PcgNodeTypes.SpawnPoints => new[] { ("count", "integer", "Count"), ("radius", "number", "Radius") },
                PcgNodeTypes.PlaceInScene => new[] { ("prefab", "string", "Prefab"), ("scale", "number", "Scale") },
                _ => Array.Empty<(string, string, string)>(),
            };

            if (props.Length == 0)
            {
                m_Body.Add(new Label("(no parameters)") { style = { color = new Color(0.5f, 0.5f, 0.5f) } });
                return;
            }

            foreach (var (key, type, displayName) in props)
                m_Body.Add(CreateLegacyPropertyRow(node, key, type, displayName));
        }

        private VisualElement CreateLegacyPropertyRow(PcgGraphNodeBase node, string key, string type, string displayName)
        {
            var container = new VisualElement { style = { marginBottom = 6 } };

            var headerRow = new VisualElement { style = { flexDirection = FlexDirection.Row } };

            var label = new Label(displayName)
            {
                style = { flexGrow = 1, color = new Color(0.8f, 0.8f, 0.8f), unityFontStyleAndWeight = FontStyle.Bold },
            };
            headerRow.Add(label);

            // Promote button
            var promoteBtn = new Button(() => PromoteToParameterLegacy(node, key, type, displayName))
            {
                text = "+",
                tooltip = "Promote to parameter",
            };
            promoteBtn.style.width = 22;
            headerRow.Add(promoteBtn);

            // Bind dropdown
            var (bindOptions, paramIds, currentIdx) = BuildBindOptions(node.NodeId, key, type);
            var bindPopup = new PopupField<string>(bindOptions, currentIdx);
            bindPopup.style.width = 90;
            bindPopup.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Bind Parameter", () =>
                {
                    var idx = bindOptions.IndexOf(evt.newValue);
                    var paramId = idx >= 0 && idx < paramIds.Count ? paramIds[idx] : "";
                    if (string.IsNullOrEmpty(paramId))
                        m_Blackboard.ClearBindingForNode(node.NodeId, key);
                    else
                        m_Blackboard.SetBinding(paramId, node.NodeId, key);
                });
                ShowNode(node);
            });
            headerRow.Add(bindPopup);
            container.Add(headerRow);

            // Value field or bound label
            var binding = m_Blackboard.FindBinding(node.NodeId, key);
            if (binding != null)
            {
                var wrapper = new VisualElement { style = { marginTop = 2 } };
                if (binding.hasRange && (binding.type == "integer" || binding.type == "number"))
                {
                    var val = float.TryParse(binding.defaultValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var fv) ? fv : binding.minValue;
                    var slider = new Slider(binding.minValue, binding.maxValue) { value = Mathf.Clamp(fv, binding.minValue, binding.maxValue) };
                    var valLabel = new Label(binding.defaultValue) { style = { color = new Color(0.4f, 0.7f, 1.0f), fontSize = 10 } };
                    slider.style.flexGrow = 1;
                    var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                    row.Add(slider);
                    row.Add(valLabel);
                    wrapper.Add(new Label($"→ {binding.name}") { style = { color = new Color(0.4f, 0.7f, 1.0f), unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 2 } });
                    wrapper.Add(row);
                }
                else
                {
                    wrapper.Add(new Label($"→ {binding.name} ({binding.defaultValue})")
                    {
                        style = { color = new Color(0.4f, 0.7f, 1.0f), unityFontStyleAndWeight = FontStyle.Italic },
                    });
                }
                container.Add(wrapper);
            }
            else
            {
                var currentVal = node.CollectData().GetRaw(key);
                VisualElement field = type switch
                {
                    "integer" => MakeIntField(key, currentVal, v => node.SetPropertyValue(key, v)),
                    "number" => MakeFloatField(key, currentVal, v => node.SetPropertyValue(key, v)),
                    _ => MakeTextField(key, currentVal, v => node.SetPropertyValue(key, v)),
                };
                field.style.marginTop = 2;
                container.Add(field);
            }

            return container;
        }

        // ─── Promote to parameter ────────────────────────────────────

        private void PromoteToParameter(PcgManifestNodeView node, string key, ManifestPropertyDef prop)
        {
            m_GraphView.WithUndo("Promote to Parameter", () =>
            {
                var currentVal = node.CollectData().GetRaw(key);
                var defaultStr = prop.type switch
                {
                    "integer" => Convert.ToInt32(currentVal ?? prop.defaultValue ?? 0, CultureInfo.InvariantCulture).ToString(),
                    "number" => Convert.ToSingle(currentVal ?? prop.defaultValue ?? 0f, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
                    "boolean" => currentVal switch
                    {
                        bool b => b ? "true" : "false",
                        string s => s,
                        _ => "false",
                    },
                    _ => currentVal?.ToString() ?? prop.defaultValue?.ToString() ?? "",
                };

                var paramType = prop.type == "enum" ? "string" : prop.type;
                var param = m_Blackboard.CreateParameter(key, paramType, defaultStr);

                if (prop.hasRange)
                {
                    param.hasRange = true;
                    param.minValue = prop.minimum;
                    param.maxValue = prop.maximum;
                }

                m_Blackboard.SetBinding(param.id, node.NodeId, key);
            });
            ShowNode(node);
        }

        private void PromoteToParameterLegacy(PcgGraphNodeBase node, string key, string type, string displayName)
        {
            m_GraphView.WithUndo("Promote to Parameter", () =>
            {
                var currentVal = node.CollectData().GetRaw(key);
                var defaultStr = type switch
                {
                    "integer" => Convert.ToInt32(currentVal ?? 0, CultureInfo.InvariantCulture).ToString(),
                    "number" => Convert.ToSingle(currentVal ?? 0f, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
                    _ => currentVal?.ToString() ?? "",
                };

                var param = m_Blackboard.CreateParameter(displayName, type, defaultStr);
                m_Blackboard.SetBinding(param.id, node.NodeId, key);
            });
            ShowNode(node);
        }

        // ─── Shared helpers ─────────────────────────────────────────

        private (List<string> options, List<string> paramIds, int currentIdx) BuildBindOptions(
            string nodeId, string propertyKey, string propType)
        {
            var options = new List<string> { "(none)" };
            var paramIds = new List<string> { "" };

            foreach (var p in m_Blackboard.Parameters)
            {
                if (IsTypeCompatible(p.type, propType))
                {
                    options.Add(p.name);
                    paramIds.Add(p.id);
                }
            }

            var currentIdx = 0;
            var binding = m_Blackboard.FindBinding(nodeId, propertyKey);
            if (binding != null)
            {
                var idx = paramIds.IndexOf(binding.id);
                if (idx >= 0) currentIdx = idx;
            }

            return (options, paramIds, currentIdx);
        }

        private static bool IsTypeCompatible(string paramType, string propType)
        {
            return propType switch
            {
                "integer" => paramType == "integer",
                "number" => paramType == "number" || paramType == "integer",
                "boolean" => paramType == "boolean",
                "enum" => paramType == "string",
                _ => paramType == "string",
            };
        }

        // ─── Field factories ────────────────────────────────────────

        private IntegerField MakeIntField(string key, object val, Action<int> onSet)
        {
            var field = new IntegerField { value = Convert.ToInt32(val ?? 0, CultureInfo.InvariantCulture) };
            field.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue)));
            return field;
        }

        private FloatField MakeFloatField(string key, object val, Action<float> onSet)
        {
            var field = new FloatField { value = Convert.ToSingle(val ?? 0f, CultureInfo.InvariantCulture) };
            field.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue)));
            return field;
        }

        private Toggle MakeToggleField(string key, object val, Action<bool> onSet)
        {
            var b = val switch
            {
                bool bv => bv,
                string s => string.Equals(s, "true", StringComparison.OrdinalIgnoreCase),
                _ => false,
            };
            var field = new Toggle { value = b };
            field.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue)));
            return field;
        }

        private VisualElement MakeEnumField(string key, ManifestPropertyDef prop, object val, Action<string> onSet)
        {
            var labels = new List<string>();
            var values = new List<string>();
            var currentStr = val?.ToString() ?? prop.defaultValue?.ToString() ?? "";
            var selectedIdx = 0;

            for (var i = 0; i < prop.options.Count; i++)
            {
                var opt = prop.options[i];
                values.Add(opt.value);
                labels.Add(string.IsNullOrEmpty(opt.label) ? opt.value : opt.label);
                if (opt.value == currentStr) selectedIdx = i;
            }

            if (labels.Count == 0)
            {
                labels.Add(currentStr);
                values.Add(currentStr);
            }

            var popup = new PopupField<string>(labels, selectedIdx);
            popup.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () =>
                {
                    var idx = labels.IndexOf(evt.newValue);
                    if (idx >= 0 && idx < values.Count)
                        onSet(values[idx]);
                });
            });
            return popup;
        }

        private TextField MakeTextField(string key, object val, Action<string> onSet)
        {
            var field = new TextField { value = val?.ToString() ?? "" };
            field.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue)));
            return field;
        }
    }
}
