using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEditor.UIElements;
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
        private ScrollView m_Body;
        private PcgGraphNodeBase m_CurrentNode;
        private bool m_IsRebuilding;

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
            style.flexDirection = FlexDirection.Column;
            style.flexShrink = 0;

            var header = new Label("Inspector")
            {
                style =
                {
                    flexShrink = 0,
                    paddingLeft = 8,
                    paddingRight = 8,
                    paddingTop = 6,
                    paddingBottom = 6,
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.9f, 0.9f, 0.9f),
                },
            };
            Add(header);

            m_Body = new ScrollView(ScrollViewMode.Vertical)
            {
                style =
                {
                    flexGrow = 1,
                    flexShrink = 1,
                    paddingLeft = 6,
                    paddingRight = 8,
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
            if (m_IsRebuilding)
                return;

            m_IsRebuilding = true;
            try
            {
                m_CurrentNode = node;
                m_Body.Clear();
                style.display = DisplayStyle.Flex;

                var titleLabel = new Label(node.GetDisplayTitle())
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

                if (node is PcgManifestNodeView meshDataNode && meshDataNode.NodeType == "GetMeshData")
                    m_Body.Add(CreateGetMeshDataPreviewRow(meshDataNode));

                if (node is PcgManifestNodeView { NodeType: "CreateSpline" })
                {
                    m_Body.Add(new Label("Drag control points in Scene View.")
                    {
                        style =
                        {
                            color = new Color(0.55f, 0.85f, 1f),
                            fontSize = 10,
                            marginBottom = 6,
                            whiteSpace = WhiteSpace.Normal,
                        },
                    });
                }

                if (node is PcgManifestNodeView manifestNode)
                    ShowManifestProperties(manifestNode);
            }
            finally
            {
                m_IsRebuilding = false;
            }
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
            var container = new VisualElement
            {
                style =
                {
                    marginBottom = 8,
                    flexShrink = 0,
                },
            };

            // Row 1: label + promote button + bind dropdown
            var headerRow = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                },
            };

            var label = new Label(key)
            {
                style =
                {
                    flexGrow = 1,
                    flexShrink = 1,
                    overflow = Overflow.Hidden,
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
            promoteBtn.style.flexShrink = 0;
            headerRow.Add(promoteBtn);

            // Bind dropdown
            var (bindOptions, paramIds, currentIdx) = BuildBindOptions(node.NodeId, key, prop.type);
            var bindPopup = new PopupField<string>(bindOptions, currentIdx);
            bindPopup.style.width = 90;
            bindPopup.style.flexShrink = 0;
            bindPopup.style.marginLeft = 4;
            bindPopup.RegisterValueChangedCallback(evt =>
            {
                var idx = bindOptions.IndexOf(evt.newValue);
                var paramId = idx >= 0 && idx < paramIds.Count ? paramIds[idx] : "";
                var currentBinding = m_Blackboard.FindBinding(node.NodeId, key);
                var currentId = currentBinding?.id ?? "";
                if (paramId == currentId)
                    return;

                m_GraphView.WithUndo("Bind Parameter", () =>
                {
                    if (string.IsNullOrEmpty(paramId))
                        m_Blackboard.ClearBindingForNode(node.NodeId, key);
                    else
                        m_Blackboard.SetBinding(paramId, node.NodeId, key);
                });

                // Refresh value row only — full ShowNode() on BevelMesh retriggers PopupFields and can stack-overflow.
                if (container.childCount > 1)
                    container.RemoveAt(container.childCount - 1);
                var binding = m_Blackboard.FindBinding(node.NodeId, key);
                container.Add(CreateValueField(key, prop, node, binding));
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
                wrapper.Add(new Label($"→ {binding.name}")
                {
                    style = { color = new Color(0.4f, 0.7f, 1.0f), unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 2 },
                });

                if (binding.hasRange && (binding.type == "integer" || binding.type == "number"))
                {
                    var isInteger = binding.type == "integer";
                    var current = isInteger
                        ? (float.TryParse(binding.defaultValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var iv) ? iv : binding.minValue)
                        : (float.TryParse(binding.defaultValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var fv) ? fv : binding.minValue);

                    wrapper.Add(PcgInspectorWidgets.CreateSliderRow(
                        isInteger,
                        binding.minValue,
                        binding.maxValue,
                        current,
                        newValue =>
                        {
                            var text = isInteger
                                ? Mathf.RoundToInt(newValue).ToString(CultureInfo.InvariantCulture)
                                : PcgInspectorWidgets.FormatFloat(newValue);
                            m_GraphView.WithUndo("Change Parameter", () =>
                                m_Blackboard.SetParameterDefault(binding.id, text));
                        },
                        onDragBegin: () => m_GraphView.BeginDrag("Change Parameter"),
                        onDragEnd: () =>
                        {
                            m_GraphView.EndDrag();
                            NotifyGraphChanged();
                        }));
                }
                else
                {
                    wrapper.Add(new Label(binding.defaultValue)
                    {
                        style = { color = new Color(0.4f, 0.7f, 1.0f), paddingTop = 2, paddingBottom = 2 },
                    });
                }
                return wrapper;
            }

            var currentVal = node.CollectData().GetRaw(key);

            if (prop.hasRange && (prop.type == "integer" || prop.type == "number"))
            {
                var isInteger = prop.type == "integer";
                var val = Convert.ToSingle(currentVal ?? prop.defaultValue ?? 0f, CultureInfo.InvariantCulture);
                wrapper.Add(PcgInspectorWidgets.CreateSliderRow(
                    isInteger,
                    prop.minimum,
                    prop.maximum,
                    val,
                    newValue =>
                    {
                        if (isInteger)
                            node.SetPropertyValue(key, Mathf.RoundToInt(newValue));
                        else
                            node.SetPropertyValue(key, newValue);
                        NotifyGraphChanged();
                    },
                    onDragBegin: () => m_GraphView.BeginDrag("Change Property"),
                    onDragEnd: () =>
                    {
                        m_GraphView.EndDrag();
                        NotifyGraphChanged();
                    },
                    onFieldCommit: newValue =>
                    {
                        m_GraphView.WithUndo("Change Property", () =>
                        {
                            if (isInteger)
                                node.SetPropertyValue(key, Mathf.RoundToInt(newValue));
                            else
                                node.SetPropertyValue(key, newValue);
                        });
                        NotifyGraphChanged();
                    }));
                return wrapper;
            }

            VisualElement field = prop.type switch
            {
                "integer" => MakeIntField(key, currentVal, v => node.SetPropertyValue(key, v)),
                "number" => MakeFloatField(key, currentVal, v => node.SetPropertyValue(key, v)),
                "boolean" => MakeToggleField(key, currentVal, v => node.SetPropertyValue(key, v)),
                "enum" => MakeEnumField(key, prop, currentVal, v => node.SetPropertyValue(key, v)),
                "texture2d" => MakeTextureField(key, currentVal, v => node.SetPropertyValue(key, v)),
                _ => MakeTextField(key, currentVal, v => node.SetPropertyValue(key, v)),
            };
            wrapper.Add(field);
            return wrapper;
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

        // ─── Shared helpers ─────────────────────────────────────────

        private VisualElement CreateGetMeshDataPreviewRow(PcgManifestNodeView node)
        {
            var container = new VisualElement { style = { marginBottom = 8 } };
            container.Add(new Label("Preview Mesh Binding")
            {
                style = { color = new Color(0.75f, 0.75f, 0.75f), fontSize = 10, marginBottom = 2 },
            });

            var nodeData = node.CollectData();
            var bindingKey = nodeData.GetRaw("bindingKey")?.ToString() ?? "targetMesh";
            MeshFilter current = null;
            PcgGraphEditorWindow editorWindow = null;
            if (m_GraphView.HostWindow is PcgGraphEditorWindow window)
            {
                editorWindow = window;
                foreach (var binding in window.PreviewMeshBindings)
                {
                    if (binding != null && binding.bindingKey == bindingKey)
                    {
                        current = binding.previewMeshFilter;
                        break;
                    }
                }
            }

            var field = new ObjectField("MeshFilter")
            {
                objectType = typeof(MeshFilter),
                value = current,
            };
            field.RegisterValueChangedCallback(evt =>
            {
                if (m_GraphView.HostWindow is PcgGraphEditorWindow w)
                    w.SetPreviewMeshFilter(bindingKey, evt.newValue as MeshFilter);
                NotifyGraphChanged();
                ShowNode(node);
            });
            container.Add(field);

            TryFindSceneComponentBindings(editorWindow?.CurrentAssetPath, out var host, out var componentBindings);
            var previewBindings = editorWindow?.PreviewMeshBindings;
            var mesh = PcgMeshResolver.TryResolveGetMeshData(
                nodeData, host, componentBindings, previewBindings);

            if (mesh == null)
            {
                container.Add(new Label(
                    "No mesh resolved. Assign MeshFilter above, or configure Mesh Bindings " +
                    "(bindingKey = targetMesh, Scene Object) on a scene PcgGraphComponent.")
                {
                    style =
                    {
                        color = new Color(1f, 0.72f, 0.25f),
                        fontSize = 10,
                        whiteSpace = WhiteSpace.Normal,
                        marginTop = 4,
                    },
                });
            }
            else
            {
                container.Add(new Label($"Resolved: {mesh.name} ({mesh.vertexCount} verts)")
                {
                    style =
                    {
                        color = new Color(0.55f, 0.85f, 0.55f),
                        fontSize = 10,
                        marginTop = 4,
                    },
                });
            }

            return container;
        }

        private static void TryFindSceneComponentBindings(
            string graphAssetPath,
            out GameObject host,
            out IReadOnlyList<PcgMeshBinding> bindings)
        {
            host = null;
            bindings = null;
            if (string.IsNullOrEmpty(graphAssetPath))
                return;

            var graphAsset = AssetDatabase.LoadAssetAtPath<PcgGraphAsset>(graphAssetPath);
            if (graphAsset == null)
                return;

            foreach (var component in UnityEngine.Object.FindObjectsByType<PcgGraphComponent>(
                         FindObjectsInactive.Include, FindObjectsSortMode.None))
            {
                if (component == null || component.GraphAsset != graphAsset)
                    continue;

                host = component.gameObject;
                bindings = component.MeshBindings;
                return;
            }
        }

        private void NotifyGraphChanged() => m_GraphView?.NotifyDocumentChanged();

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

        private VisualElement MakeTextureField(string key, object val, Action<string> onSet)
        {
            var stored = val?.ToString() ?? "";
            var tex = PcgTextureAssetUtil.LoadTextureFromStorage(stored);

            var field = new ObjectField
            {
                objectType = typeof(Texture2D),
                allowSceneObjects = false,
                value = tex,
            };
            field.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () =>
                {
                    var t = evt.newValue as Texture2D;
                    var path = PcgTextureAssetUtil.TextureToStorageValue(t);
                    onSet(path);
                    if (t != null && string.IsNullOrEmpty(path))
                        Debug.LogWarning("[PCG] ImageTexture: could not bind texture — use a Project asset.");
                    else if (!string.IsNullOrEmpty(path))
                        Debug.Log($"[PCG] ImageTexture '{key}' bound to {path}");
                });
            });
            return field;
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
