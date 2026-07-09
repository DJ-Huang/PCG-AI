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
    /// Group properties (groupSelect/groupMultiSelect) resolve available
    /// groups from upstream SpatialMesh connections.
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

                if (node is PcgManifestNodeView createSplineNode && createSplineNode.NodeType == "CreateSpline")
                {
                    var data = createSplineNode.CollectData();
                    var editPlane = data?.GetRaw("editPlane")?.ToString() ?? "none";
                    var hint = editPlane != "none"
                        ? $"Scene View: select point, drag on selection, I/Del insert/delete, Shift+click segment (locked to {editPlane.ToUpperInvariant()})."
                        : "Scene View: select point, drag on selection, I insert, Del delete, Shift+click segment to insert.";
                    m_Body.Add(new Label(hint)
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

            // Split properties into group-related and regular
            var groupProps = new List<(string key, ManifestPropertyDef prop)>();
            var regularProps = new List<(string key, ManifestPropertyDef prop)>();
            foreach (var (key, prop) in def.properties)
            {
                if (prop.type == "groupSelect" || prop.type == "groupMultiSelect" || prop.isGroupOutput)
                    groupProps.Add((key, prop));
                else
                    regularProps.Add((key, prop));
            }

            // Groups section
            if (groupProps.Count > 0)
            {
                AddSectionHeader("Groups");
                foreach (var (key, prop) in groupProps)
                    m_Body.Add(CreatePropertyRow(node, key, prop));

                var available = ResolveUpstreamGroups(node.NodeId);
                if (available.Count > 0)
                {
                    m_Body.Add(new Label($"{available.Count} group{(available.Count != 1 ? "s" : "")} available from upstream")
                    {
                        style = { color = new Color(0.4f, 0.66f, 0.4f), fontSize = 10, unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 4 },
                    });
                }
                else
                {
                    var hasConsumers = groupProps.Any(p => p.prop.type is "groupSelect" or "groupMultiSelect");
                    if (hasConsumers)
                    {
                        m_Body.Add(new Label("No groups from upstream — connect a Group Create or Sweep node")
                        {
                            style = { color = new Color(0.6f, 0.5f, 0.3f), fontSize = 10, unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 4, whiteSpace = WhiteSpace.Normal },
                        });
                    }
                }
            }

            // Parameters section
            if (groupProps.Count > 0 && regularProps.Count > 0)
                AddSectionHeader("Parameters");
            foreach (var (key, prop) in regularProps)
                m_Body.Add(CreatePropertyRow(node, key, prop));
        }

        private void AddSectionHeader(string title)
        {
            m_Body.Add(new Label(title)
            {
                style =
                {
                    unityFontStyleAndWeight = FontStyle.Bold,
                    fontSize = 10,
                    color = new Color(0.55f, 0.55f, 0.55f),
                    marginTop = 6,
                    marginBottom = 2,
                    paddingTop = 4,
                    paddingBottom = 2,
                    borderTopWidth = 1,
                    borderTopColor = new Color(0.2f, 0.2f, 0.2f),
                },
            });
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
                "groupSelect" => MakeGroupSelectField(key, prop, currentVal, node, v => node.SetPropertyValue(key, v)),
                "groupMultiSelect" => MakeGroupMultiSelectField(key, prop, currentVal, node, v => node.SetPropertyValue(key, v)),
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

                var paramType = prop.type is "enum" or "groupSelect" or "groupMultiSelect" ? "string" : prop.type;
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
                "groupSelect" => paramType == "string",
                "groupMultiSelect" => paramType == "string",
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

        // ─── Group Resolution ──────────────────────────────────────

        private struct AvailableGroup
        {
            public string name;
            public string domain;
            public string sourceNodeId;
            public string sourceNodeType;
            public string label;
        }

        /// <summary>
        /// Walks upstream SpatialMesh connections to collect all named groups
        /// produced by upstream nodes (Houdini-style group resolution).
        /// </summary>
        private List<AvailableGroup> ResolveUpstreamGroups(string startNodeId)
        {
            var result = new List<AvailableGroup>();
            var seen = new HashSet<string>();
            var visited = new HashSet<string>();
            FindUpstreamMeshGroups(startNodeId, result, seen, visited);
            return result;
        }

        private void FindUpstreamMeshGroups(
            string nodeId,
            List<AvailableGroup> result,
            HashSet<string> seen,
            HashSet<string> visited)
        {
            if (visited.Contains(nodeId))
                return;
            visited.Add(nodeId);

            // Walk GraphView edges to find incoming connections
            foreach (var edge in m_GraphView.edges)
            {
                if (edge.input?.node is not PcgGraphNodeBase targetNode)
                    continue;
                if (targetNode.NodeId != nodeId)
                    continue;
                if (edge.output?.node is not PcgGraphNodeBase sourceNode)
                    continue;

                // Check if this edge carries SpatialMesh data
                var sourceHandle = edge.output.userData as string ?? edge.output.portName;
                var outputPinType = PcgNodeManifest.GetOutputPinType(sourceNode.NodeType, sourceHandle);
                if (outputPinType != "SpatialMesh")
                    continue;

                CollectNodeGroups(sourceNode, result, seen);
                FindUpstreamMeshGroups(sourceNode.NodeId, result, seen, visited);
            }
        }

        private static void CollectNodeGroups(
            PcgGraphNodeBase node,
            List<AvailableGroup> result,
            HashSet<string> seen)
        {
            if (!PcgNodeManifest.TryGet(node.NodeType, out var def))
                return;

            var data = node.CollectData();

            // 1. Static output groups from manifest (e.g. SweepAlongSpline)
            foreach (var og in def.outputGroups)
            {
                // Check condition property
                if (!string.IsNullOrEmpty(og.condition))
                {
                    var condVal = data.GetRaw(og.condition);
                    if (condVal is bool b && !b)
                        continue;
                    if (condVal is string s && s != "true")
                        continue;
                }

                string groupName = og.name;
                if (og.dynamic)
                {
                    // Dynamic: group name comes from a property value
                    var propValue = data.GetRaw(og.name)?.ToString();
                    if (string.IsNullOrWhiteSpace(propValue))
                        continue;
                    groupName = propValue;
                }

                var dedupKey = $"{groupName}:{og.domain}";
                if (seen.Contains(dedupKey))
                    continue;
                seen.Add(dedupKey);

                result.Add(new AvailableGroup
                {
                    name = groupName,
                    domain = og.domain,
                    sourceNodeId = node.NodeId,
                    sourceNodeType = node.NodeType,
                    label = og.label,
                });
            }

            // 2. Dynamic output groups from properties with isGroupOutput flag
            if (def.outputGroups.Count == 0)
            {
                foreach (var (key, prop) in def.properties)
                {
                    if (!prop.isGroupOutput)
                        continue;

                    var groupName = data.GetRaw(key)?.ToString();
                    if (string.IsNullOrWhiteSpace(groupName))
                        continue;

                    var domain = string.IsNullOrEmpty(prop.groupDomain) ? "edge" : prop.groupDomain;
                    var dedupKey = $"{groupName}:{domain}";
                    if (seen.Contains(dedupKey))
                        continue;
                    seen.Add(dedupKey);

                    result.Add(new AvailableGroup
                    {
                        name = groupName,
                        domain = domain,
                        sourceNodeId = node.NodeId,
                        sourceNodeType = node.NodeType,
                    });
                }
            }
        }

        // ─── Group Field Factories ──────────────────────────────────

        private VisualElement MakeGroupSelectField(
            string key, ManifestPropertyDef prop, object val,
            PcgManifestNodeView node, Action<string> onSet)
        {
            var container = new VisualElement();

            var available = ResolveUpstreamGroups(node.NodeId);

            // Filter by domain if specified
            if (!string.IsNullOrEmpty(prop.groupDomain))
                available = available.Where(g => g.domain == prop.groupDomain).ToList();

            var currentVal = val?.ToString() ?? "";

            // TextField for manual entry (always visible)
            var textField = new TextField { value = currentVal };
            textField.style.marginBottom = 2;
            textField.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Group", () => onSet(evt.newValue)));
            container.Add(textField);

            // Quick-pick chips for available groups
            if (available.Count > 0)
            {
                var chipsRow = new VisualElement
                {
                    style = { flexDirection = FlexDirection.Row, flexWrap = Wrap.Wrap },
                };

                foreach (var g in available)
                {
                    var chip = new Button
                    {
                        text = g.name,
                        tooltip = $"{g.label ?? g.name} ({g.domain}) from {g.sourceNodeType}",
                    };
                    chip.style.fontSize = 9;
                    chip.style.paddingLeft = 6;
                    chip.style.paddingRight = 6;
                    chip.style.paddingTop = 1;
                    chip.style.paddingBottom = 1;
                    chip.style.marginRight = 2;
                    chip.style.marginBottom = 2;
                    chip.style.unityFontStyleAndWeight = currentVal == g.name ? FontStyle.Bold : FontStyle.Normal;

                    var capturedName = g.name;
                    chip.clicked += () =>
                    {
                        m_GraphView.WithUndo("Pick Group", () =>
                        {
                            onSet(capturedName);
                            textField.value = capturedName;
                            NotifyGraphChanged();
                        });
                    };
                    chipsRow.Add(chip);
                }

                container.Add(chipsRow);
            }

            return container;
        }

        private VisualElement MakeGroupMultiSelectField(
            string key, ManifestPropertyDef prop, object val,
            PcgManifestNodeView node, Action<string> onSet)
        {
            var container = new VisualElement();

            var available = ResolveUpstreamGroups(node.NodeId);

            // Filter by domain if specified
            if (!string.IsNullOrEmpty(prop.groupDomain))
                available = available.Where(g => g.domain == prop.groupDomain).ToList();

            var currentStr = val?.ToString() ?? "";
            var selected = currentStr.Split(',', StringSplitOptions.RemoveEmptyEntries)
                .Select(s => s.Trim())
                .Where(s => !string.IsNullOrEmpty(s))
                .ToList();

            // Checkboxes for available groups
            if (available.Count > 0)
            {
                var availableNames = new HashSet<string>(available.Select(g => g.name));

                foreach (var g in available)
                {
                    var groupName = g.name;
                    var toggle = new Toggle
                    {
                        value = selected.Contains(groupName),
                        text = $"{groupName} ({g.domain})",
                        tooltip = g.label ?? groupName,
                    };
                    toggle.style.fontSize = 10;
                    toggle.style.marginBottom = 1;

                    toggle.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Toggle Group", () =>
                        {
                            if (evt.newValue && !selected.Contains(groupName))
                                selected.Add(groupName);
                            else if (!evt.newValue)
                                selected.Remove(groupName);

                            onSet(string.Join(",", selected));
                            NotifyGraphChanged();
                        });
                    });
                    container.Add(toggle);
                }

                // Show custom entries that aren't in available groups
                foreach (var custom in selected.Where(s => !availableNames.Contains(s)))
                {
                    var toggle = new Toggle
                    {
                        value = true,
                        text = $"{custom} (custom)",
                    };
                    toggle.style.fontSize = 10;
                    toggle.style.marginBottom = 1;

                    var capturedCustom = custom;
                    toggle.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Toggle Group", () =>
                        {
                            selected.Remove(capturedCustom);
                            onSet(string.Join(",", selected));
                            NotifyGraphChanged();
                        });
                    });
                    container.Add(toggle);
                }
            }

            // TextField for manual comma-separated entry
            var textField = new TextField { value = currentStr };
            textField.style.marginTop = 2;
            textField.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Groups", () => onSet(evt.newValue)));
            container.Add(textField);

            return container;
        }
    }
}
