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
        private bool m_PendingSelectionDirty;
        private PcgGraphNodeBase m_PendingNode;
        // Foldout state keyed by "nodeId|sectionId" — survives Inspector rebuild within session.
        private static readonly Dictionary<string, bool> s_SectionExpanded = new();
        // Active tab keyed by node id for manifest tabs layout.
        private static readonly Dictionary<string, string> s_ActiveTabSection = new();

        public PcgNodeInspector(PcgGraphView graphView, PcgGraphBlackboard blackboard)
        {
            m_GraphView = graphView;
            m_Blackboard = blackboard;
            BuildUI();
        }

        private void BuildUI()
        {
            // Floating panel docked to the right edge of the graph canvas.
            // Content-sized height; clamp to window so tall forms still scroll.
            style.position = Position.Absolute;
            style.right = 0;
            style.top = 0;
            style.width = 400;
            style.minWidth = 340;
            style.borderLeftWidth = 1;
            style.borderLeftColor = new Color(0.15f, 0.15f, 0.15f);
            style.backgroundColor = new Color(0.22f, 0.22f, 0.22f);
            style.flexDirection = FlexDirection.Column;
            style.height = StyleKeyword.Auto;
            style.maxHeight = Length.Percent(100);

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
                    minHeight = 0,
                    paddingLeft = 6,
                    paddingRight = 8,
                    paddingBottom = 6,
                },
            };
            Add(m_Body);

            Add(new PcgPanelResizer(
                this,
                PcgPanelResizer.Edge.Left,
                minSize: 340f,
                maxSize: () => PcgPanelResizer.GetRowWidth(this, 1600f) - 320f,
                onBeforeApply: null,
                onReset: ResetPanelSize));
            Add(new PcgPanelResizer(
                this,
                PcgPanelResizer.Edge.Bottom,
                minSize: 160f,
                maxSize: () => PcgPanelResizer.GetRowHeight(this, 2000f),
                onBeforeApply: null,
                onReset: ResetPanelSize));

            ShowEmpty();
            style.display = DisplayStyle.None;
        }

        private void ResetPanelSize()
        {
            style.width = 400;
            style.minWidth = 340;
            style.height = StyleKeyword.Auto;
            style.maxHeight = Length.Percent(100);
        }

        public void ToggleVisible()
        {
            var opening = style.display.value != DisplayStyle.Flex;
            style.display = opening ? DisplayStyle.Flex : DisplayStyle.None;
            if (opening && m_PendingSelectionDirty)
            {
                m_PendingSelectionDirty = false;
                if (m_PendingNode != null)
                    ShowNode(m_PendingNode);
                else
                    OnSelectionChanged();
            }
        }

        public void OnSelectionChanged()
        {
            var selected = m_GraphView.selection.OfType<PcgGraphNodeBase>().FirstOrDefault();
            if (selected == null)
            {
                m_PendingSelectionDirty = false;
                m_PendingNode = null;
                ShowEmpty();
                return;
            }

            if (style.display.value != DisplayStyle.Flex)
            {
                m_PendingSelectionDirty = true;
                m_PendingNode = selected;
                return;
            }

            m_PendingSelectionDirty = false;
            m_PendingNode = null;
            ShowNode(selected);
        }

        private void ShowEmpty()
        {
            m_CurrentNode = null;
            m_Body.Clear();
            // Visibility is manual (toolbar / I); selection only updates content.
            m_Body.Add(new Label("No node selected")
            {
                style =
                {
                    color = new Color(0.55f, 0.55f, 0.55f),
                    unityFontStyleAndWeight = FontStyle.Italic,
                    paddingTop = 8,
                },
            });
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

                var ctx = m_GraphView.SceneEditContext;
                var modeText = ctx.IsObjectMode
                    ? "Scene: Object mode"
                    : $"Scene: {ctx.Domain}";
                m_Body.Add(new Label(modeText)
                {
                    style =
                    {
                        color = new Color(0.6f, 0.6f, 0.6f),
                        fontSize = 10,
                        marginBottom = 4,
                    },
                });

                if (node is PcgManifestNodeView createSplineNode &&
                    (createSplineNode.NodeType == "CreateSpline" || createSplineNode.NodeType == "CreateBezierSpline"))
                {
                    if (ctx.IsComponentMode && ctx.Domain == SceneEditDomain.SplineControlPoint)
                    {
                        var data = createSplineNode.CollectData();
                        var editPlane = data?.GetRaw("editPlane")?.ToString() ?? "none";
                        var hint = editPlane != "none"
                            ? $"Scene View: click/drag points, Shift multi-select, A/I/Del add/insert/delete, Ctrl/Cmd+click segment or empty space (edit plane {editPlane.ToUpperInvariant()})."
                            : "Scene View: click/drag points, Shift multi-select, A/I/Del add/insert/delete, Ctrl/Cmd+click segment to insert or empty space to add.";
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
                    else
                    {
                        m_Body.Add(new Label("Enter PCG Mode in Scene View, then select Spline CP to edit control points")
                        {
                            style =
                            {
                                color = new Color(0.6f, 0.5f, 0.3f),
                                fontSize = 10,
                                marginBottom = 6,
                                whiteSpace = WhiteSpace.Normal,
                            },
                        });
                    }
                }

                if (node is PcgManifestNodeView groupCapable && NodeSupportsGroupViewer(groupCapable))
                {
                    if (ctx.IsComponentMode && ctx.Domain != SceneEditDomain.None &&
                        ctx.Domain != SceneEditDomain.SplineControlPoint)
                    {
                        m_Body.Add(new Label($"Group viewer — right Group List; hover any domain to preview")
                        {
                            style =
                            {
                                color = new Color(0.55f, 0.85f, 1f),
                                fontSize = 10,
                                marginBottom = 6,
                                width = Length.Percent(100),
                                whiteSpace = WhiteSpace.Normal,
                            },
                        });
                    }
                    else
                    {
                        m_Body.Add(new Label("Enter PCG Mode, then open Group List on the right Scene View strip")
                        {
                            style =
                            {
                                color = new Color(0.6f, 0.5f, 0.3f),
                                fontSize = 10,
                                marginBottom = 6,
                                whiteSpace = WhiteSpace.Normal,
                            },
                        });
                    }
                }

                if (node is PcgManifestNodeView manifestNode)
                {
                    ShowManifestProperties(manifestNode);
                    if (manifestNode.NodeType == "ExportFBX")
                        m_Body.Add(CreateFbxExportActions(manifestNode));
                    if (manifestNode.NodeType == PcgMeshyResolver.NodeType)
                        m_Body.Add(CreateMeshyGenerateSection(manifestNode));
                }

                if (node is PcgExternalSubgraphNodeView externalSubgraph)
                {
                    ShowSubgraphInterface(externalSubgraph.Snapshot);
                    ShowSubgraphPromotedParameters(externalSubgraph);
                }
                else if (node is PcgSubgraphNodeView inlineSubgraph &&
                         inlineSubgraph.Kind == PcgSubgraphNodeKind.Instance)
                {
                    ShowSubgraphInterface(inlineSubgraph.InterfaceSnapshot);
                    ShowSubgraphPromotedParameters(inlineSubgraph);
                }

                if (node is PcgSubgraphParentRefNodeView parentRef)
                    ShowParentRefProperties(parentRef);
            }
            finally
            {
                m_IsRebuilding = false;
            }
        }

        // ─── Manifest nodes ──────────────────────────────────────────

        private void ShowSubgraphInterface(PcgSubgraphInterfaceSnapshot snapshot)
        {
            snapshot ??= new PcgSubgraphInterfaceSnapshot();
            var container = new VisualElement
            {
                style =
                {
                    marginTop = 4,
                    paddingTop = 6,
                    borderTopWidth = 1,
                    borderTopColor = new Color(0.3f, 0.3f, 0.3f),
                },
            };
            container.Add(new Label("Interface Pins")
            {
                style =
                {
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.82f, 0.82f, 0.82f),
                    marginBottom = 4,
                },
            });
            AddSubgraphPortSection(container, "Inputs", snapshot.inputs);
            AddSubgraphPortSection(container, "Outputs", snapshot.outputs);
            m_Body.Add(container);
        }

        private void ShowSubgraphPromotedParameters(PcgGraphNodeBase node)
        {
            List<PcgGraphParameter> parameters;
            string liveContentHash = "";
            string schemaVersion = PcgSubgraphAssetMigration.Version10;
            PcgExternalSubgraphNodeView external = null;

            if (node is PcgSubgraphNodeView inline &&
                inline.Kind == PcgSubgraphNodeKind.Instance)
            {
                var definition = m_GraphView.FindSubgraphDefinitionForNode(inline);
                parameters = definition?.parameters;
            }
            else if (node is PcgExternalSubgraphNodeView linked)
            {
                external = linked;
                if (!m_GraphView.TryLoadExternalSubgraphParameters(
                        linked.AssetGuid,
                        out parameters,
                        out liveContentHash,
                        out schemaVersion))
                {
                    parameters = null;
                }
            }
            else
            {
                return;
            }

            if (parameters == null || parameters.Count == 0)
            {
                if (external != null && external.HasAssetVersionMismatch(liveContentHash))
                    m_Body.Add(CreateAssetVersionUpgradeRow(external, liveContentHash, schemaVersion));
                return;
            }

            var container = new VisualElement
            {
                style =
                {
                    marginTop = 8,
                    paddingTop = 6,
                    borderTopWidth = 1,
                    borderTopColor = new Color(0.3f, 0.3f, 0.3f),
                },
            };
            container.Add(new Label("Promoted Parameters")
            {
                style =
                {
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.82f, 0.82f, 0.82f),
                    marginBottom = 4,
                },
            });

            var nodeData = node.CollectData();
            foreach (var parameter in parameters)
            {
                if (parameter == null)
                    continue;
                var resolved = PcgSubgraphInstanceParameterStorage.ResolveOverride(nodeData, parameter);
                container.Add(CreateSubgraphOverrideRow(node, parameter, resolved));
            }

            if (external != null)
                container.Add(CreateAssetVersionUpgradeRow(external, liveContentHash, schemaVersion));

            m_Body.Add(container);
        }

        private VisualElement CreateSubgraphOverrideRow(
            PcgGraphNodeBase node,
            PcgGraphParameter parameter,
            PcgParameterOverride resolved)
        {
            var row = new VisualElement
            {
                style =
                {
                    marginBottom = 6,
                    paddingBottom = 4,
                    borderBottomWidth = 1,
                    borderBottomColor = new Color(0.28f, 0.28f, 0.28f),
                },
            };
            row.Add(new Label(string.IsNullOrEmpty(parameter.name) ? parameter.id : parameter.name)
            {
                style =
                {
                    unityFontStyleAndWeight = FontStyle.Bold,
                    fontSize = 11,
                    color = new Color(0.78f, 0.78f, 0.78f),
                },
            });

            void PersistOverride(object value)
            {
                m_GraphView.WithUndo("Change Subgraph Parameter", () =>
                {
                    var data = node.CollectData();
                    PcgSubgraphInstanceParameterStorage.SetOverrideValue(data, parameter, value);
                    node.ApplyData(data);
                });
                NotifyGraphChanged();
            }

            switch (parameter.type)
            {
                case "integer":
                {
                    var field = new IntegerField { value = resolved.intValue };
                    field.RegisterValueChangedCallback(evt => PersistOverride(evt.newValue));
                    row.Add(field);
                    break;
                }
                case "number":
                {
                    var field = new FloatField { value = resolved.floatValue };
                    field.RegisterValueChangedCallback(evt => PersistOverride(evt.newValue));
                    row.Add(field);
                    break;
                }
                case "boolean":
                {
                    var field = new Toggle { value = resolved.boolValue };
                    field.RegisterValueChangedCallback(evt => PersistOverride(evt.newValue));
                    row.Add(field);
                    break;
                }
                default:
                {
                    var field = new TextField { value = resolved.stringValue ?? "" };
                    field.RegisterValueChangedCallback(evt => PersistOverride(evt.newValue));
                    row.Add(field);
                    break;
                }
            }

            return row;
        }

        private VisualElement CreateAssetVersionUpgradeRow(
            PcgExternalSubgraphNodeView external,
            string liveContentHash,
            string schemaVersion)
        {
            var row = new VisualElement { style = { marginTop = 8 } };
            if (external.HasAssetVersionMismatch(liveContentHash))
            {
                row.Add(new Label($"Asset changed (schema {schemaVersion}). Instance snapshot may be stale.")
                {
                    style =
                    {
                        color = new Color(0.9f, 0.65f, 0.35f),
                        fontSize = 10,
                        whiteSpace = WhiteSpace.Normal,
                        marginBottom = 4,
                    },
                });
            }

            var buttonRow = new VisualElement { style = { flexDirection = FlexDirection.Row } };
            buttonRow.Add(new Button(() =>
            {
                m_GraphView.UpgradeExternalSubgraphInstance(external);
                ShowNode(external);
            })
            {
                text = "Upgrade Instance",
                style = { flexGrow = 1, marginRight = 4 },
            });
            buttonRow.Add(new Button(() =>
            {
                m_GraphView.WithUndo("Pin Subgraph Asset Version", () =>
                {
                    external.PinCurrentAssetVersion(liveContentHash);
                    var data = external.CollectData();
                    external.ApplyData(data);
                });
                ShowNode(external);
            })
            {
                text = "Pin Version",
                style = { width = 90 },
            });
            row.Add(buttonRow);
            return row;
        }

        private static void AddSubgraphPortSection(
            VisualElement container,
            string title,
            IReadOnlyList<PcgSubgraphPort> ports)
        {
            ports ??= Array.Empty<PcgSubgraphPort>();
            container.Add(new Label($"{title} ({ports.Count})")
            {
                style =
                {
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = new Color(0.62f, 0.72f, 0.82f),
                    fontSize = 10,
                    marginTop = 3,
                    marginBottom = 2,
                },
            });

            if (ports.Count == 0)
            {
                container.Add(new Label("(none)")
                {
                    style =
                    {
                        color = new Color(0.48f, 0.48f, 0.48f),
                        fontSize = 10,
                        marginLeft = 8,
                    },
                });
                return;
            }

            foreach (var port in ports)
            {
                if (port == null)
                    continue;
                var row = new VisualElement
                {
                    tooltip = $"Stable id: {port.id}",
                    style =
                    {
                        flexDirection = FlexDirection.Row,
                        alignItems = Align.Center,
                        minHeight = 20,
                        marginLeft = 8,
                        marginBottom = 1,
                    },
                };
                row.Add(new Label(string.IsNullOrEmpty(port.name) ? port.id : port.name)
                {
                    style =
                    {
                        flexGrow = 1,
                        color = new Color(0.8f, 0.8f, 0.8f),
                        overflow = Overflow.Hidden,
                        unityTextAlign = TextAnchor.MiddleLeft,
                    },
                });
                row.Add(new Label(string.IsNullOrEmpty(port.pinType) ? "Any" : port.pinType)
                {
                    style =
                    {
                        minWidth = 105,
                        color = new Color(0.55f, 0.82f, 1f),
                        unityTextAlign = TextAnchor.MiddleRight,
                        unityFontStyleAndWeight = FontStyle.Bold,
                        fontSize = 10,
                    },
                });
                container.Add(row);
            }
        }

        private void ShowParentRefProperties(PcgSubgraphParentRefNodeView node)
        {
            m_Body.Add(new Label("References a node in the immediate parent scope.")
            {
                style =
                {
                    color = new Color(0.55f, 0.75f, 0.9f),
                    fontSize = 10,
                    marginBottom = 6,
                    whiteSpace = WhiteSpace.Normal,
                },
            });

            if (m_GraphView.TryGetImmediateParentScope(out var parentNodes, out _))
            {
                var choices = parentNodes
                    .Where(n => n != null &&
                                n.type != PcgStructuralNodeTypes.SubgraphInput &&
                                n.type != PcgStructuralNodeTypes.SubgraphOutput &&
                                n.type != PcgStructuralNodeTypes.SubgraphParentRef)
                    .Select(n => n.id)
                    .ToList();
                if (choices.Count > 0)
                {
                    var current = node.ParentNodeId;
                    var index = Mathf.Max(0, choices.IndexOf(current));
                    var popup = new PopupField<string>(choices, index);
                    popup.RegisterValueChangedCallback(evt =>
                    {
                        m_GraphView.WithUndo("Set Parent Reference", () =>
                        {
                            node.SetParentReference(evt.newValue, node.ParentHandle);
                            m_GraphView.CommitState();
                        });
                    });
                    m_Body.Add(popup);
                }
            }

            var handleField = new TextField("Parent Handle") { value = node.ParentHandle };
            handleField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Set Parent Handle", () =>
                {
                    node.SetParentReference(node.ParentNodeId, evt.newValue);
                    m_GraphView.CommitState();
                });
            });
            m_Body.Add(handleField);
        }

        private void ShowManifestProperties(PcgManifestNodeView node)
        {
            if (!PcgNodeManifest.TryGet(node.NodeType, out var def))
            {
                m_Body.Add(new Label("(no parameters)") { style = { color = new Color(0.5f, 0.5f, 0.5f) } });
                return;
            }

            if (def.properties.Count == 0 && def.outputGroups.Count == 0)
            {
                m_Body.Add(new Label("(no parameters)") { style = { color = new Color(0.5f, 0.5f, 0.5f) } });
                return;
            }

            if (node.NodeType == "Carve")
            {
                ShowCarveProperties(node, def);
                return;
            }

            if (def.inspectorSections != null && def.inspectorSections.Count > 0)
            {
                ShowSectionedManifestProperties(node, def);
                AddFixedOutputGroupsIfNeeded(node, def);
                return;
            }

            // Legacy path: Input Groups / Output Groups / Parameters.
            var inputGroupProps = new List<(string key, ManifestPropertyDef prop)>();
            var outputGroupProps = new List<(string key, ManifestPropertyDef prop)>();
            var regularProps = new List<(string key, ManifestPropertyDef prop)>();
            var companionTargets = new HashSet<string>(
                def.properties.Values
                    .Where(prop => !string.IsNullOrEmpty(prop.companionField))
                    .Select(prop => prop.companionField));
            foreach (var (key, prop) in def.properties)
            {
                if (companionTargets.Contains(key) || !IsPropertyVisible(node, key, prop))
                    continue;
                if (prop.type is "groupSelect" or "groupMultiSelect")
                    inputGroupProps.Add((key, prop));
                else if (prop.isGroupOutput)
                    outputGroupProps.Add((key, prop));
                else
                    regularProps.Add((key, prop));
            }

            if (inputGroupProps.Count > 0)
            {
                AddSectionHeader("Input Groups");
                foreach (var (key, prop) in inputGroupProps)
                    m_Body.Add(CreatePropertyRow(
                        node, key, prop, def, rebuildOnChange: IsVisibilityDriver(def, key)));

                var available = ResolveUpstreamGroups(node.NodeId);
                if (available.Count > 0)
                {
                    m_Body.Add(new Label($"{available.Count} upstream group{(available.Count != 1 ? "s" : "")} selectable")
                    {
                        style = { color = new Color(0.4f, 0.66f, 0.4f), fontSize = 10, unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 4 },
                    });
                }
                else
                {
                    m_Body.Add(new Label("No upstream groups yet — empty Input selects all faces/edges as documented by the node")
                    {
                        style = { color = new Color(0.6f, 0.5f, 0.3f), fontSize = 10, unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 4, whiteSpace = WhiteSpace.Normal },
                    });
                }
            }

            if (outputGroupProps.Count > 0 || HasFixedOutputGroups(def, outputGroupProps))
            {
                AddSectionHeader("Output Groups");
                foreach (var (key, prop) in outputGroupProps)
                    m_Body.Add(CreatePropertyRow(
                        node, key, prop, def, rebuildOnChange: IsVisibilityDriver(def, key)));
                AddFixedOutputGroupRows(node, def, outputGroupProps);
            }

            if ((inputGroupProps.Count > 0 || outputGroupProps.Count > 0 || def.outputGroups.Count > 0)
                && regularProps.Count > 0)
                AddSectionHeader("Parameters");
            foreach (var (key, prop) in regularProps)
            {
                // AttributeWrangle channels: never show raw JSON textarea.
                if (node.NodeType == "AttributeWrangle" && key == "parameters")
                {
                    AddSectionHeader("Channels (chi / chf)");
                    m_Body.Add(new Label("Add multiple channels. Optional Expr is evaluated once at cook (e.g. @iteration).")
                    {
                        style =
                        {
                            color = new Color(0.55f, 0.7f, 0.55f),
                            fontSize = 10,
                            marginBottom = 4,
                            whiteSpace = WhiteSpace.Normal,
                        },
                    });
                    m_Body.Add(CreateWrangleParametersEditor(node));
                    continue;
                }
                if (node.NodeType == "GroupDelete" && key == "deletions")
                {
                    m_Body.Add(CreateGroupDeleteRulesEditor(node));
                    continue;
                }
                m_Body.Add(CreatePropertyRow(
                    node, key, prop, def, rebuildOnChange: IsVisibilityDriver(def, key)));
            }
        }

        private void ShowCarveProperties(PcgManifestNodeView node, ManifestNodeDef def)
        {
            foreach (var key in new[] { "group", "useFirstU", "useSecondU", "useFirstV", "useSecondV" })
                AddCarvePropertyRow(m_Body, node, def, key);

            m_Body.Add(CreateCarveModePanel(
                node,
                def,
                "location",
                "uDivisions",
                "vDivisions",
                "cutAtAllInternalUBreakpoints",
                "cutAtAllInternalVBreakpoints"));
            m_Body.Add(CreateCarveModePanel(
                node,
                def,
                "operation",
                "keepInside",
                "keepOutside",
                "extractType",
                "keepOriginal",
                "onlyAtBreakpoints"));
        }

        private VisualElement CreateCarveModePanel(
            PcgManifestNodeView node,
            ManifestNodeDef def,
            string modeKey,
            params string[] contentKeys)
        {
            var panel = new VisualElement
            {
                style =
                {
                    marginTop = 6,
                    marginBottom = 6,
                    paddingLeft = 2,
                    paddingRight = 2,
                    paddingTop = 2,
                    paddingBottom = 4,
                    borderTopWidth = 1,
                    borderBottomWidth = 1,
                    borderLeftWidth = 1,
                    borderRightWidth = 1,
                    borderTopColor = new Color(0.12f, 0.12f, 0.12f),
                    borderBottomColor = new Color(0.12f, 0.12f, 0.12f),
                    borderLeftColor = new Color(0.12f, 0.12f, 0.12f),
                    borderRightColor = new Color(0.12f, 0.12f, 0.12f),
                },
            };

            AddCarvePropertyRow(panel, node, def, modeKey, hideLabel: true);

            var content = new VisualElement
            {
                style =
                {
                    paddingLeft = 92,
                    paddingTop = 2,
                },
            };
            foreach (var key in contentKeys)
                AddCarvePropertyRow(content, node, def, key);
            panel.Add(content);
            return panel;
        }

        private void AddCarvePropertyRow(
            VisualElement parent,
            PcgManifestNodeView node,
            ManifestNodeDef def,
            string key,
            bool hideLabel = false)
        {
            if (!def.properties.TryGetValue(key, out var prop) ||
                !IsPropertyVisible(node, key, prop))
                return;

            var row = CreatePropertyRow(
                node,
                key,
                prop,
                def,
                rebuildOnChange: IsVisibilityDriver(def, key),
                showActions: false,
                hideLabel: hideLabel);

            if (key is "uDivisions" or "cutAtAllInternalUBreakpoints")
            {
                row.SetEnabled(
                    MatchesVisibleClause(node, "useFirstU", "true", null) ||
                    MatchesVisibleClause(node, "useSecondU", "true", null));
            }
            else if (key is "vDivisions" or "cutAtAllInternalVBreakpoints")
            {
                row.SetEnabled(
                    MatchesVisibleClause(node, "useFirstV", "true", null) ||
                    MatchesVisibleClause(node, "useSecondV", "true", null));
            }

            parent.Add(row);
        }

        private bool NodeSupportsGroupViewer(PcgManifestNodeView node)
        {
            if (node.NodeType == "GroupDelete")
                return true;

            if (PcgNodeManifest.TryGet(node.NodeType, out var def))
            {
                if (def.outputGroups.Count > 0)
                    return true;
                foreach (var prop in def.properties.Values)
                {
                    if (prop.isGroupOutput || prop.type is "groupSelect" or "groupMultiSelect")
                        return true;
                }
            }

            return ResolveUpstreamGroups(node.NodeId).Count > 0;
        }

        private static bool HasFixedOutputGroups(
            ManifestNodeDef def,
            List<(string key, ManifestPropertyDef prop)> editableOutputs)
        {
            var editableKeys = new HashSet<string>(editableOutputs.Select(p => p.key));
            foreach (var og in def.outputGroups)
            {
                if (og.dynamic && editableKeys.Contains(og.name))
                    continue;
                if (!og.dynamic)
                    return true;
                // Dynamic but no matching editable property — still declare fixed/legacy name.
                if (!editableKeys.Contains(og.name))
                    return true;
            }

            return false;
        }

        private void AddFixedOutputGroupsIfNeeded(PcgManifestNodeView node, ManifestNodeDef def)
        {
            var editable = def.properties
                .Where(kv => kv.Value.isGroupOutput)
                .Select(kv => (kv.Key, kv.Value))
                .ToList();
            if (!HasFixedOutputGroups(def, editable))
                return;

            AddSectionHeader("Output Groups");
            AddFixedOutputGroupRows(node, def, editable);
        }

        private void AddFixedOutputGroupRows(
            PcgManifestNodeView node,
            ManifestNodeDef def,
            List<(string key, ManifestPropertyDef prop)> editableOutputs)
        {
            var editableKeys = new HashSet<string>(editableOutputs.Select(p => p.key));
            var data = node.CollectData();
            foreach (var declared in PcgGroupResolution.ResolveOutputGroups(def, data))
            {
                // Skip groups already covered by editable isGroupOutput property rows.
                if (!string.IsNullOrEmpty(declared.propertyKey) && editableKeys.Contains(declared.propertyKey))
                    continue;

                var row = new VisualElement { style = { marginBottom = 4 } };
                row.Add(new Label($"{declared.label}")
                {
                    style =
                    {
                        color = new Color(0.8f, 0.8f, 0.8f),
                        unityFontStyleAndWeight = FontStyle.Bold,
                    },
                });
                row.Add(new Label($"{declared.name}  ·  {declared.domain}  ·  fixed output")
                {
                    style =
                    {
                        color = new Color(0.55f, 0.75f, 0.55f),
                        fontSize = 11,
                        unityFontStyleAndWeight = FontStyle.Italic,
                    },
                });
                m_Body.Add(row);
            }
        }

        private void ShowSectionedManifestProperties(PcgManifestNodeView node, ManifestNodeDef def)
        {
            var props = def.properties
                .Select(kv => (key: kv.Key, prop: kv.Value))
                .OrderBy(p => p.prop.hasOrder ? p.prop.order : int.MaxValue)
                .ThenBy(p => p.key)
                .ToList();

            var companionTargets = new HashSet<string>(
                props
                    .Where(p => !string.IsNullOrEmpty(p.prop.companionField))
                    .Select(p => p.prop.companionField));

            // Sticky top: properties without a section (e.g. Houdini-style Group).
            var topProps = props.Where(p => string.IsNullOrEmpty(p.prop.section)).ToList();
            foreach (var (key, prop) in topProps)
            {
                if (companionTargets.Contains(key) || !IsPropertyVisible(node, key, prop))
                    continue;
                m_Body.Add(CreatePropertyRow(node, key, prop, def, rebuildOnChange: IsVisibilityDriver(def, key)));
            }

            if (topProps.Any(p => p.prop.type is "groupSelect" or "groupMultiSelect"))
            {
                var available = ResolveUpstreamGroups(node.NodeId);
                if (available.Count > 0)
                {
                    m_Body.Add(new Label($"{available.Count} group{(available.Count != 1 ? "s" : "")} available from upstream")
                    {
                        style = { color = new Color(0.4f, 0.66f, 0.4f), fontSize = 10, unityFontStyleAndWeight = FontStyle.Italic, paddingBottom = 4 },
                    });
                }
            }

            foreach (var section in def.inspectorSections)
            {
                var sectionProps = props
                    .Where(p => p.prop.section == section.id
                                && !companionTargets.Contains(p.key)
                                && IsPropertyVisible(node, p.key, p.prop))
                    .ToList();
                if (sectionProps.Count == 0)
                    continue;

                VisualElement container = m_Body;
                if (def.inspectorSectionLayout == "tabs")
                    continue;

                if (section.foldout)
                {
                    var foldoutKey = $"{node.NodeId}|{section.id}";
                    if (!s_SectionExpanded.TryGetValue(foldoutKey, out var expanded))
                        expanded = section.defaultExpanded;

                    var foldout = new Foldout
                    {
                        text = string.IsNullOrEmpty(section.label) ? section.id : section.label,
                        value = expanded,
                    };
                    foldout.style.marginTop = 4;
                    foldout.RegisterValueChangedCallback(evt => s_SectionExpanded[foldoutKey] = evt.newValue);
                    m_Body.Add(foldout);
                    container = foldout;
                }
                else if (section.header && !string.IsNullOrEmpty(section.label))
                {
                    AddSectionHeader(section.label);
                }

                foreach (var item in BuildSectionPropertyItems(node, def, sectionProps, companionTargets))
                {
                    if (TryAddGroupDeletePropertyRow(container, node, def, item, rebuildOnChange: item.rebuildOnChange))
                        continue;
                    if (item.isGroup)
                        container.Add(CreateGroupedPropertyRow(node, def, item.members, rebuildOnChange: item.rebuildOnChange));
                    else
                        container.Add(CreatePropertyRow(node, item.key, item.prop, def, rebuildOnChange: item.rebuildOnChange));
                }

                if (node.NodeType == "TransformMesh" && section.id == "preTransform")
                    container.Add(CreateMoveCentroidButton(node));
            }

            if (def.inspectorSectionLayout == "tabs" && def.inspectorSections.Count > 0)
                ShowTabbedInspectorSections(node, def, props, companionTargets);
        }

        private void ShowTabbedInspectorSections(
            PcgManifestNodeView node,
            ManifestNodeDef def,
            List<(string key, ManifestPropertyDef prop)> props,
            HashSet<string> companionTargets)
        {
            var tabSections = def.inspectorSections
                .Where(section => props.Any(p =>
                    p.prop.section == section.id &&
                    !companionTargets.Contains(p.key) &&
                    IsPropertyVisible(node, p.key, p.prop)))
                .ToList();
            if (tabSections.Count == 0)
                return;

            var tabKey = node.NodeId;
            if (!s_ActiveTabSection.TryGetValue(tabKey, out var activeId) ||
                tabSections.All(s => s.id != activeId))
                activeId = tabSections[0].id;

            var tabRow = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    flexWrap = Wrap.Wrap,
                    marginTop = 6,
                    marginBottom = 4,
                },
            };
            m_Body.Add(tabRow);

            var tabBody = new VisualElement { style = { marginTop = 2 } };
            m_Body.Add(tabBody);

            void RebuildActiveTab()
            {
                tabBody.Clear();
                var activeSection = tabSections.FirstOrDefault(s => s.id == activeId) ?? tabSections[0];
                activeId = activeSection.id;
                s_ActiveTabSection[tabKey] = activeId;

                var sectionProps = props
                    .Where(p => p.prop.section == activeSection.id
                                && !companionTargets.Contains(p.key)
                                && IsPropertyVisible(node, p.key, p.prop))
                    .ToList();
                foreach (var item in BuildSectionPropertyItems(node, def, sectionProps, companionTargets))
                {
                    if (TryAddGroupDeletePropertyRow(tabBody, node, def, item, rebuildOnChange: item.rebuildOnChange))
                        continue;
                    if (item.isGroup)
                        tabBody.Add(CreateGroupedPropertyRow(node, def, item.members, rebuildOnChange: item.rebuildOnChange));
                    else
                        tabBody.Add(CreatePropertyRow(node, item.key, item.prop, def, rebuildOnChange: item.rebuildOnChange));
                }
            }

            foreach (var section in tabSections)
            {
                var isActive = section.id == activeId;
                var sectionId = section.id;
                var button = new Button
                {
                    text = string.IsNullOrEmpty(section.label) ? section.id : section.label,
                };
                button.clicked += () =>
                {
                    if (activeId == sectionId)
                        return;
                    activeId = sectionId;
                    s_ActiveTabSection[tabKey] = sectionId;
                    foreach (var child in tabRow.Children().OfType<Button>())
                        child.style.backgroundColor = new StyleColor(StyleKeyword.Null);
                    button.style.backgroundColor = new Color(0.28f, 0.38f, 0.48f);
                    RebuildActiveTab();
                };
                button.style.marginRight = 2;
                button.style.marginBottom = 2;
                if (isActive)
                    button.style.backgroundColor = new Color(0.28f, 0.38f, 0.48f);
                tabRow.Add(button);
            }

            RebuildActiveTab();
        }

        private struct SectionPropertyItem
        {
            public bool isGroup;
            public string key;
            public ManifestPropertyDef prop;
            public List<(string key, ManifestPropertyDef prop)> members;
            public bool rebuildOnChange;
        }

        private IEnumerable<SectionPropertyItem> BuildSectionPropertyItems(
            PcgManifestNodeView node,
            ManifestNodeDef def,
            List<(string key, ManifestPropertyDef prop)> sectionProps,
            HashSet<string> companionTargets)
        {
            var emittedRowGroups = new HashSet<string>(StringComparer.Ordinal);
            foreach (var (key, prop) in sectionProps)
            {
                if (companionTargets.Contains(key) || !IsPropertyVisible(node, key, prop))
                    continue;

                if (!string.IsNullOrEmpty(prop.rowGroup))
                {
                    if (emittedRowGroups.Contains(prop.rowGroup))
                        continue;

                    var members = sectionProps
                        .Where(p => p.prop.rowGroup == prop.rowGroup &&
                                    !companionTargets.Contains(p.key) &&
                                    IsPropertyVisible(node, p.key, p.prop))
                        .OrderBy(p => p.prop.hasRowOrder ? p.prop.rowOrder : int.MaxValue)
                        .ThenBy(p => p.key)
                        .ToList();
                    if (members.Count == 0)
                        continue;

                    emittedRowGroups.Add(prop.rowGroup);
                    var rebuild = members.Any(m => IsVisibilityDriver(def, m.key));
                    yield return new SectionPropertyItem
                    {
                        isGroup = true,
                        members = members,
                        rebuildOnChange = rebuild,
                    };
                    continue;
                }

                yield return new SectionPropertyItem
                {
                    isGroup = false,
                    key = key,
                    prop = prop,
                    rebuildOnChange = IsVisibilityDriver(def, key),
                };
            }
        }

        private VisualElement CreateGroupedPropertyRow(
            PcgManifestNodeView node,
            ManifestNodeDef def,
            List<(string key, ManifestPropertyDef prop)> members,
            bool rebuildOnChange)
        {
            var lead = members[0];
            var container = new VisualElement
            {
                style =
                {
                    marginBottom = 4,
                    flexShrink = 0,
                    width = Length.Percent(100),
                    marginLeft = lead.prop.indent ? 16 : 0,
                },
            };

            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    flexWrap = Wrap.Wrap,
                    width = Length.Percent(100),
                },
            };

            var leadLabelAdded = false;
            foreach (var (key, prop) in members)
            {
                if (!leadLabelAdded && string.IsNullOrEmpty(prop.rowPrefix))
                {
                    row.Add(new Label(PcgGroupResolution.PropertyDisplayLabel(key, prop))
                    {
                        style =
                        {
                            minWidth = 56,
                            marginRight = 4,
                            color = new Color(0.8f, 0.8f, 0.8f),
                            fontSize = 10,
                        },
                    });
                    leadLabelAdded = true;
                }
                else if (!string.IsNullOrEmpty(prop.rowPrefix))
                {
                    row.Add(new Label(prop.rowPrefix)
                    {
                        style =
                        {
                            marginLeft = 4,
                            marginRight = 4,
                            color = new Color(0.65f, 0.65f, 0.65f),
                            fontSize = 10,
                        },
                    });
                }

                Action<object> setValueOverride = null;
                if (rebuildOnChange)
                {
                    setValueOverride = v =>
                    {
                        node.SetPropertyValue(key, v);
                        ScheduleInspectorRebuild(node);
                    };
                }

                var binding = m_Blackboard.FindBinding(node.NodeId, key);
                var field = CreateCompactValueField(key, prop, node, binding, setValueOverride);
                ApplyEnabledWhen(field, node, prop);
                row.Add(field);
            }

            container.Add(row);
            return container;
        }

        private VisualElement CreateCompactValueField(
            string key,
            ManifestPropertyDef prop,
            PcgManifestNodeView node,
            PcgGraphParameter binding,
            Action<object> setValueOverride = null)
        {
            var wrapper = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    flexShrink = 0,
                    marginRight = 2,
                },
            };

            if (binding != null)
            {
                wrapper.Add(new Label($"→{binding.name}")
                {
                    style = { color = new Color(0.4f, 0.7f, 1.0f), fontSize = 9, marginRight = 2 },
                });
                return wrapper;
            }

            var currentVal = node.CollectData().GetRaw(key);
            Action<object> apply = v =>
            {
                if (setValueOverride != null)
                    setValueOverride(v);
                else
                    node.SetPropertyValue(key, v);
            };

            VisualElement field = prop.type switch
            {
                "integer" => MakeCompactIntField(currentVal, v => apply(v)),
                "number" => MakeCompactFloatField(key, prop, currentVal, v => apply(v)),
                "boolean" => MakeCompactToggleField(currentVal, v => apply(v)),
                "enum" => MakeCompactEnumField(prop, currentVal, v => apply(v)),
                _ => CreateValueField(key, prop, node, null, setValueOverride),
            };
            wrapper.Add(field);
            return wrapper;
        }

        private IntegerField MakeCompactIntField(object val, Action<int> onSet)
        {
            var field = new IntegerField
            {
                value = Convert.ToInt32(val ?? 0, CultureInfo.InvariantCulture),
            };
            PcgInspectorWidgets.ConfigureCompactNumericField(field);
            field.style.width = 44;
            field.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
            return field;
        }

        private VisualElement MakeCompactFloatField(
            string key, ManifestPropertyDef prop, object val, Action<float> onSet)
        {
            if (prop.hasRange)
            {
                var current = Convert.ToSingle(val ?? prop.minimum, CultureInfo.InvariantCulture);
                var slider = PcgInspectorWidgets.CreateSliderRow(
                    false,
                    prop.minimum,
                    prop.maximum,
                    current,
                    onSet,
                    onDragBegin: () => m_GraphView.BeginDrag("Change Property"),
                    onDragEnd: () =>
                    {
                        m_GraphView.EndDrag();
                        NotifyGraphChanged();
                    },
                    onFieldCommit: v => m_GraphView.WithUndo("Change Property", () => onSet(v)),
                    numericFirst: true);
                slider.style.flexGrow = 1;
                slider.style.minWidth = 120;
                slider.style.maxWidth = 180;
                return slider;
            }

            var field = new FloatField
            {
                value = Convert.ToSingle(val ?? 0f, CultureInfo.InvariantCulture),
            };
            PcgInspectorWidgets.ConfigureCompactNumericField(field);
            field.style.width = 52;
            field.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
            return field;
        }

        private Toggle MakeCompactToggleField(object val, Action<bool> onSet)
        {
            var b = val switch
            {
                bool bv => bv,
                string s => string.Equals(s, "true", StringComparison.OrdinalIgnoreCase),
                _ => false,
            };
            var field = new Toggle { value = b };
            field.label = string.Empty;
            field.AddToClassList(BaseField<bool>.noLabelVariantUssClassName);
            field.style.marginRight = 2;
            field.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
            return field;
        }

        private VisualElement MakeCompactEnumField(
            ManifestPropertyDef prop, object val, Action<string> onSet)
        {
            var field = MakeEnumField(string.Empty, prop, val, onSet);
            if (prop.uiHint == "radio")
                return field;
            field.style.width = 72;
            field.style.minWidth = 72;
            field.style.maxWidth = 96;
            field.style.flexShrink = 0;
            return field;
        }

        private static bool IsVisibilityDriver(ManifestNodeDef def, string key) =>
            def.properties.Values.Any(p =>
                (!string.IsNullOrEmpty(p.visibleWhenProperty) && p.visibleWhenProperty == key) ||
                (!string.IsNullOrEmpty(p.enabledWhenProperty) && p.enabledWhenProperty == key) ||
                (p.visibleWhenAny != null &&
                 p.visibleWhenAny.Any(c => c.property == key)) ||
                (p.enabledWhenAll != null &&
                 p.enabledWhenAll.Any(c => c.property == key)));

        private static bool IsPropertyVisible(PcgManifestNodeView node, string key, ManifestPropertyDef prop)
        {
            if (node.NodeType == "MatchSize" &&
                (key == "targetPosition" || key == "targetSize"))
            {
                var justifyWith = NormalizeVisibleValue(node.CollectData().GetRaw("justifyWith"));
                if (string.IsNullOrEmpty(justifyWith))
                    justifyWith = "inputIfWired";
                if (justifyWith == "locationAndSize")
                    return true;
                if (justifyWith != "inputIfWired")
                    return false;

                var referencePort = node.GetInputPort("reference");
                return referencePort == null || !referencePort.connected;
            }

            if (prop.visibleWhenAny != null && prop.visibleWhenAny.Count > 0)
            {
                foreach (var clause in prop.visibleWhenAny)
                {
                    if (MatchesVisibleClause(node, clause.property, clause.equals, clause.oneOf))
                        return true;
                }
                return false;
            }

            if (string.IsNullOrEmpty(prop.visibleWhenProperty))
                return true;

            return MatchesVisibleClause(node, prop.visibleWhenProperty, prop.visibleWhenEquals,
                prop.visibleWhenOneOf);
        }

        private static bool MatchesVisibleClause(
            PcgManifestNodeView node, string property, string equals, List<string> oneOf)
        {
            if (string.IsNullOrEmpty(property))
                return true;

            var current = NormalizeVisibleValue(node.CollectData().GetRaw(property));
            if (string.IsNullOrEmpty(current) &&
                PcgNodeManifest.TryGet(node.NodeType, out var def) &&
                def.properties.TryGetValue(property, out var driver))
            {
                current = NormalizeVisibleValue(driver.defaultValue);
            }

            if (oneOf != null && oneOf.Count > 0)
            {
                foreach (var candidate in oneOf)
                {
                    if (string.Equals(current, NormalizeVisibleValue(candidate), StringComparison.Ordinal))
                        return true;
                }
                return false;
            }

            return string.Equals(current, NormalizeVisibleValue(equals), StringComparison.Ordinal);
        }

        private static string NormalizeVisibleValue(object value)
        {
            return value switch
            {
                null => "",
                bool b => b ? "true" : "false",
                string s when bool.TryParse(s, out var parsed) => parsed ? "true" : "false",
                _ => value.ToString() ?? "",
            };
        }

        private void ScheduleInspectorRebuild(PcgManifestNodeView node)
        {
            schedule.Execute(() =>
            {
                if (m_CurrentNode == node)
                    ShowNode(node);
            }).ExecuteLater(1);
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

        /// <summary>
        /// Houdini-style single row: [right-aligned label] [value field / slider …] [+] [bind].
        /// Booleans use a compact Houdini-style checkbox row (label on the toggle).
        /// </summary>
        private VisualElement CreatePropertyRow(
            PcgManifestNodeView node, string key, ManifestPropertyDef prop, bool rebuildOnChange = false) =>
            CreatePropertyRow(node, key, prop, null, rebuildOnChange);

        private VisualElement CreatePropertyRow(
            PcgManifestNodeView node,
            string key,
            ManifestPropertyDef prop,
            ManifestNodeDef def,
            bool rebuildOnChange = false,
            bool showActions = true,
            bool hideLabel = false)
        {
            Action<object> setValueOverride = null;
            if (rebuildOnChange)
            {
                setValueOverride = v =>
                {
                    node.SetPropertyValue(key, v);
                    ScheduleInspectorRebuild(node);
                };
            }

            if (prop.type == "boolean")
                return CreateHoudiniToggleRow(
                    node, key, prop, def, setValueOverride, showActions);

            var container = new VisualElement
            {
                style =
                {
                    marginBottom = 2,
                    flexShrink = 0,
                    width = Length.Percent(100),
                    marginLeft = prop.indent ? 16 : 0,
                },
            };

            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    width = Length.Percent(100),
                },
            };

            if (!hideLabel)
            {
                row.Add(new Label(PcgGroupResolution.PropertyDisplayLabel(key, prop))
                {
                    style =
                    {
                        width = 92,
                        minWidth = 56,
                        flexShrink = 0,
                        whiteSpace = WhiteSpace.Normal,
                        unityTextAlign = TextAnchor.MiddleRight,
                        color = new Color(0.8f, 0.8f, 0.8f),
                        fontSize = 10,
                        paddingTop = 1,
                        marginRight = 6,
                    },
                });
            }

            var valueHolder = new VisualElement
            {
                style =
                {
                    flexGrow = 1,
                    flexShrink = 1,
                    minWidth = 48,
                    marginRight = showActions ? 4 : 0,
                },
            };

            void RebuildValue()
            {
                valueHolder.Clear();
                // AttributeWrangle parameters are drawn as a dedicated Channels section in
                // ShowManifestProperties — never fall back to multiline JSON here.
                if (node.NodeType == "AttributeWrangle" && key == "parameters")
                    return;
                if (node.NodeType == "GroupDelete" && key == "deletions")
                    return;
                var binding = m_Blackboard.FindBinding(node.NodeId, key);
                var valueElement = CreateValueField(key, prop, node, binding, setValueOverride);
                valueElement.style.marginTop = 0;
                valueElement.style.width = Length.Percent(100);
                if (hideLabel && prop.uiHint == "radio")
                {
                    valueElement.style.width = StyleKeyword.Auto;
                    valueElement.style.maxWidth = 132;
                    valueElement.style.flexGrow = 0;
                }
                valueHolder.Add(valueElement);
            }

            RebuildValue();
            row.Add(valueHolder);

            if (showActions)
            {
                var actions = new VisualElement
                {
                    style =
                    {
                        flexDirection = FlexDirection.Row,
                        flexShrink = 0,
                        alignItems = Align.Center,
                    },
                };

                // Promote-to-parameter button
                var promoteBtn = new Button(() => PromoteToParameter(node, key, prop))
                {
                    text = "+",
                    tooltip = "Promote to parameter",
                };
                promoteBtn.style.width = 22;
                promoteBtn.style.flexShrink = 0;
                actions.Add(promoteBtn);

                // Bind dropdown
                var (bindOptions, paramIds, currentIdx) = BuildBindOptions(node.NodeId, key, prop.type);
                var bindPopup = new PopupField<string>(bindOptions, currentIdx);
                bindPopup.style.width = 72;
                bindPopup.style.flexShrink = 0;
                bindPopup.style.marginLeft = 2;
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

                    // Refresh value only — full ShowNode() on BevelMesh retriggers PopupFields and can stack-overflow.
                    RebuildValue();
                });
                actions.Add(bindPopup);
                row.Add(actions);
            }

            container.Add(row);
            ApplyEnabledWhen(container, node, prop);
            return container;
        }

        /// <summary>
        /// Houdini-style: ☐ label [optional companion value / slider] [+] [bind].
        /// A toggle and its companion are one parameter row, matching Houdini's enabled parameters.
        /// </summary>
        private VisualElement CreateHoudiniToggleRow(
            PcgManifestNodeView node,
            string key,
            ManifestPropertyDef prop,
            ManifestNodeDef def,
            Action<object> setValueOverride,
            bool showActions)
        {
            var container = new VisualElement
            {
                style =
                {
                    marginBottom = 4,
                    flexShrink = 0,
                    flexDirection = FlexDirection.Column,
                    width = Length.Percent(100),
                    marginLeft = prop.indent ? 16 : 0,
                },
            };

            var currentVal = node.CollectData().GetRaw(key);
            var toggled = currentVal switch
            {
                bool bv => bv,
                string s => string.Equals(s, "true", StringComparison.OrdinalIgnoreCase),
                _ => false,
            };

            Action<object> apply = v =>
            {
                if (setValueOverride != null)
                    setValueOverride(v);
                else
                    node.SetPropertyValue(key, v);
            };

            var topRow = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    flexWrap = Wrap.NoWrap,
                    width = Length.Percent(100),
                },
            };

            ManifestPropertyDef companionProp = null;
            var hasCompanion = def != null &&
                               !string.IsNullOrEmpty(prop.companionField) &&
                               def.properties.TryGetValue(prop.companionField, out companionProp);

            // Split checkbox + label so the label remains a compact, clickable Houdini-style prefix.
            var toggleBlock = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    flexGrow = hasCompanion ? 0 : 1,
                    flexShrink = 1,
                    minWidth = hasCompanion ? 92 : 120,
                },
            };
            if (hasCompanion)
                toggleBlock.style.width = 92;

            var toggle = new Toggle { value = toggled };
            toggle.label = string.Empty;
            toggle.AddToClassList(BaseField<bool>.noLabelVariantUssClassName);
            toggle.style.flexShrink = 0;
            toggle.style.marginTop = 1;
            toggle.style.marginRight = 4;

            var toggleLabel = new Label(PcgGroupResolution.PropertyDisplayLabel(key, prop))
            {
                style =
                {
                    flexGrow = 1,
                    flexShrink = 1,
                    minWidth = 0,
                    whiteSpace = WhiteSpace.NoWrap,
                    color = new Color(0.85f, 0.85f, 0.85f),
                    paddingTop = 2,
                },
            };
            toggleLabel.RegisterCallback<ClickEvent>(_ =>
            {
                if (!toggle.enabledSelf)
                    return;
                toggle.value = !toggle.value;
            });

            toggle.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () => apply(evt.newValue));
                NotifyGraphChanged();
                if (setValueOverride == null &&
                    def != null &&
                    IsVisibilityDriver(def, key))
                    ScheduleInspectorRebuild(node);
            });

            toggleBlock.Add(toggle);
            toggleBlock.Add(toggleLabel);
            topRow.Add(toggleBlock);

            if (hasCompanion)
            {
                var companionBinding = m_Blackboard.FindBinding(node.NodeId, prop.companionField);
                var companionField = CreateValueField(
                    prop.companionField, companionProp, node, companionBinding, null);
                companionField.style.flexGrow = 1;
                companionField.style.flexShrink = 1;
                companionField.style.minWidth = 80;
                companionField.style.width = StyleKeyword.Auto;
                companionField.style.marginLeft = 4;
                companionField.style.marginRight = 4;
                companionField.style.marginTop = 0;
                companionField.SetEnabled(toggled);
                toggle.RegisterValueChangedCallback(evt => companionField.SetEnabled(evt.newValue));
                topRow.Add(companionField);
            }

            if (showActions)
            {
                var actions = new VisualElement
                {
                    style =
                    {
                        flexDirection = FlexDirection.Row,
                        flexShrink = 0,
                        alignItems = Align.Center,
                        marginLeft = 4,
                    },
                };

                var promoteBtn = new Button(() => PromoteToParameter(node, key, prop))
                {
                    text = "+",
                    tooltip = "Promote to parameter",
                };
                promoteBtn.style.width = 22;
                promoteBtn.style.flexShrink = 0;
                actions.Add(promoteBtn);

                var (bindOptions, paramIds, currentIdx) = BuildBindOptions(node.NodeId, key, prop.type);
                var bindPopup = new PopupField<string>(bindOptions, currentIdx);
                bindPopup.style.width = 72;
                bindPopup.style.flexShrink = 0;
                bindPopup.style.marginLeft = 2;
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
                    ScheduleInspectorRebuild(node);
                });
                actions.Add(bindPopup);
                topRow.Add(actions);
            }
            container.Add(topRow);

            ApplyEnabledWhen(container, node, prop);
            return container;
        }

        private static void ApplyEnabledWhen(
            VisualElement row, PcgManifestNodeView node, ManifestPropertyDef prop)
        {
            if (string.IsNullOrEmpty(prop.enabledWhenProperty) &&
                (prop.enabledWhenAll == null || prop.enabledWhenAll.Count == 0))
                return;

            var enabled = true;
            if (!string.IsNullOrEmpty(prop.enabledWhenProperty))
            {
                enabled = MatchesVisibleClause(
                    node, prop.enabledWhenProperty, prop.enabledWhenEquals, prop.enabledWhenOneOf);
            }
            if (enabled && prop.enabledWhenAll != null)
            {
                foreach (var clause in prop.enabledWhenAll)
                {
                    if (!MatchesVisibleClause(node, clause.property, clause.equals, clause.oneOf))
                    {
                        enabled = false;
                        break;
                    }
                }
            }
            row.SetEnabled(enabled);
        }

        private VisualElement CreateValueField(
            string key,
            ManifestPropertyDef prop,
            PcgManifestNodeView node,
            PcgGraphParameter binding,
            Action<object> setValueOverride = null)
        {
            var wrapper = new VisualElement { style = { marginTop = 2 } };
            Action<object> apply = v =>
            {
                if (setValueOverride != null)
                    setValueOverride(v);
                else
                    node.SetPropertyValue(key, v);
            };

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
                        },
                        numericFirst: true));
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
                            apply(Mathf.RoundToInt(newValue));
                        else
                            apply(newValue);
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
                                apply(Mathf.RoundToInt(newValue));
                            else
                                apply(newValue);
                        });
                        NotifyGraphChanged();
                    },
                    numericFirst: true));
                return wrapper;
            }

            VisualElement field = prop.type switch
            {
                "integer" => MakeIntField(key, currentVal, v => apply(v)),
                "number" => MakeFloatField(key, currentVal, v => apply(v)),
                "boolean" => MakeToggleField(key, currentVal, v => apply(v)),
                "enum" => MakeEnumField(key, prop, currentVal, v => apply(v)),
                "vector3" => MakeVector3Field(node, key, v => apply(v)),
                "texture2d" => MakeTextureField(key, currentVal, v => apply(v)),
                "groupSelect" => MakeGroupSelectField(key, prop, currentVal, node, v => apply(v)),
                "groupMultiSelect" => MakeGroupMultiSelectField(key, prop, currentVal, node, v => apply(v)),
                _ => MakeTextField(key, prop, currentVal, v => apply(v)),
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

        private sealed class WrangleParamRow
        {
            public string Name = "";
            public double Value;
            public string Expr = "";
        }

        private sealed class GroupDeleteRuleRow
        {
            public bool Enabled = true;
            public string GroupType = "any";
            public string GroupNames = "";
        }

        private static readonly (string value, string label)[] k_GroupDeleteTypeOptions =
        {
            ("any", "Any"),
            ("points", "Points"),
            ("primitives", "Primitives"),
            ("edges", "Edges"),
            ("vertices", "Vertices"),
        };

        private bool TryAddGroupDeletePropertyRow(
            VisualElement container,
            PcgManifestNodeView node,
            ManifestNodeDef def,
            SectionPropertyItem item,
            bool rebuildOnChange)
        {
            if (item.isGroup || node.NodeType != "GroupDelete")
                return false;

            if (item.key == "deletions")
            {
                container.Add(CreateGroupDeleteRulesEditor(node));
                return true;
            }

            return false;
        }

        private static VisualElement CreateHoudiniLabeledRow(string label, VisualElement field, float labelWidth = 96f)
        {
            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    marginBottom = 4,
                    width = Length.Percent(100),
                },
            };
            row.Add(new Label(label)
            {
                style =
                {
                    width = labelWidth,
                    minWidth = labelWidth,
                    flexShrink = 0,
                    color = new Color(0.85f, 0.85f, 0.85f),
                    unityFontStyleAndWeight = FontStyle.Normal,
                },
            });
            field.style.flexGrow = 1;
            field.style.flexShrink = 1;
            field.style.minWidth = 60;
            row.Add(field);
            return row;
        }

        private VisualElement MakeGroupDeleteNamesField(
            PcgManifestNodeView node,
            string currentValue,
            Action<string> onSet)
        {
            var row = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row, alignItems = Align.Center },
            };

            var textField = new TextField { value = currentValue ?? "" };
            textField.style.flexGrow = 1;
            textField.style.flexShrink = 1;
            textField.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Edit Group Names", () => onSet(evt.newValue ?? "")));
            row.Add(textField);

            var available = ResolveUpstreamGroups(node.NodeId);
            var expandBtn = new Button { text = "\u25be", tooltip = "Browse available groups" };
            expandBtn.style.width = 22;
            expandBtn.style.flexShrink = 0;
            expandBtn.style.marginLeft = 2;
            if (available.Count > 0)
            {
                expandBtn.clicked += () =>
                {
                    var menu = new GenericMenu();
                    foreach (var sourceGroup in available.GroupBy(g => g.sourceNodeType ?? "Unknown").OrderBy(g => g.Key))
                    {
                        foreach (var g in sourceGroup)
                        {
                            var capturedName = g.name;
                            var path = $"{sourceGroup.Key}/{g.name} ({g.domain})";
                            menu.AddItem(new GUIContent(path), textField.value == capturedName, () =>
                            {
                                m_GraphView.WithUndo("Pick Group", () =>
                                {
                                    onSet(capturedName);
                                    textField.value = capturedName;
                                    NotifyGraphChanged();
                                });
                            });
                        }
                    }

                    var r = expandBtn.worldBound;
                    menu.DropDown(new Rect(r.x, r.y + r.height, 0, 0));
                };
            }
            else
            {
                expandBtn.SetEnabled(false);
            }

            row.Add(expandBtn);
            return row;
        }

        private static List<GroupDeleteRuleRow> ParseGroupDeleteRules(object raw)
        {
            var rows = new List<GroupDeleteRuleRow>();
            List<object> list = null;
            if (raw is List<object> asList)
            {
                list = asList;
            }
            else
            {
                var text = raw?.ToString() ?? "";
                if (string.IsNullOrWhiteSpace(text) || text.StartsWith("System.Collections", StringComparison.Ordinal))
                    return rows;
                try
                {
                    if (PcgMiniJson.Deserialize(text) is List<object> parsed)
                        list = parsed;
                }
                catch
                {
                    return rows;
                }
            }

            if (list == null)
                return rows;

            foreach (var item in list)
            {
                if (item is not Dictionary<string, object> obj)
                    continue;
                var row = new GroupDeleteRuleRow();
                if (obj.TryGetValue("enabled", out var enabledObj) && enabledObj != null)
                {
                    if (enabledObj is bool enabledBool)
                        row.Enabled = enabledBool;
                    else
                        row.Enabled = string.Equals(enabledObj.ToString(), "true", StringComparison.OrdinalIgnoreCase);
                }
                if (obj.TryGetValue("groupType", out var typeObj) && typeObj != null)
                    row.GroupType = typeObj.ToString() ?? "any";
                if (obj.TryGetValue("groupNames", out var namesObj) && namesObj != null)
                    row.GroupNames = namesObj.ToString() ?? "";
                rows.Add(row);
            }
            return rows;
        }

        private static string SerializeGroupDeleteRules(List<GroupDeleteRuleRow> rows)
        {
            var list = new List<object>();
            foreach (var row in rows)
            {
                list.Add(new Dictionary<string, object>
                {
                    ["enabled"] = row.Enabled,
                    ["groupType"] = row.GroupType ?? "any",
                    ["groupNames"] = row.GroupNames ?? "",
                });
            }
            return PcgMiniJson.Serialize(list);
        }

        private VisualElement CreateGroupDeleteRulesEditor(PcgManifestNodeView node)
        {
            var root = new VisualElement
            {
                style =
                {
                    marginTop = 2,
                    marginBottom = 8,
                    width = Length.Percent(100),
                },
            };

            var rows = ParseGroupDeleteRules(node.CollectData().GetRaw("deletions"));
            if (rows.Count == 0)
                rows.Add(new GroupDeleteRuleRow { GroupNames = "base" });

            void Commit()
            {
                m_GraphView.WithUndo("Edit Group Delete Rules", () =>
                    node.SetPropertyValue("deletions", SerializeGroupDeleteRules(rows)));
                NotifyGraphChanged();
            }

            void SetRowCount(int count)
            {
                count = Mathf.Clamp(count, 1, 32);
                while (rows.Count < count)
                    rows.Add(new GroupDeleteRuleRow());
                while (rows.Count > count)
                    rows.RemoveAt(rows.Count - 1);
                Commit();
                Rebuild();
            }

            void Rebuild()
            {
                root.Clear();

                var countRow = new VisualElement
                {
                    style =
                    {
                        flexDirection = FlexDirection.Row,
                        alignItems = Align.Center,
                        marginBottom = 8,
                        width = Length.Percent(100),
                    },
                };
                countRow.Add(new Label("Number of Deletions")
                {
                    style =
                    {
                        width = 120,
                        minWidth = 120,
                        flexShrink = 0,
                        color = new Color(0.85f, 0.85f, 0.85f),
                    },
                });

                var countField = new IntegerField { value = rows.Count };
                PcgInspectorWidgets.ConfigureCompactNumericField(countField);
                countField.style.width = 48;
                countField.style.marginRight = 4;
                countField.RegisterValueChangedCallback(evt =>
                {
                    SetRowCount(evt.newValue);
                });
                countRow.Add(countField);

                var addBtn = new Button(() => SetRowCount(rows.Count + 1))
                {
                    text = "+",
                    tooltip = "Add deletion rule",
                };
                addBtn.style.width = 22;
                addBtn.style.marginRight = 2;
                countRow.Add(addBtn);

                var removeBtn = new Button(() => SetRowCount(rows.Count - 1))
                {
                    text = "-",
                    tooltip = "Remove last deletion rule",
                };
                removeBtn.style.width = 22;
                countRow.Add(removeBtn);
                root.Add(countRow);

                for (var i = 0; i < rows.Count; i++)
                {
                    var index = i;
                    var row = rows[index];
                    var card = new VisualElement
                    {
                        style =
                        {
                            marginBottom = 8,
                            paddingLeft = 6,
                            paddingRight = 6,
                            paddingTop = 6,
                            paddingBottom = 6,
                            width = Length.Percent(100),
                            backgroundColor = new Color(0.18f, 0.2f, 0.18f, 0.55f),
                            borderTopLeftRadius = 3,
                            borderTopRightRadius = 3,
                            borderBottomLeftRadius = 3,
                            borderBottomRightRadius = 3,
                        },
                    };

                    var enableRow = new VisualElement
                    {
                        style =
                        {
                            flexDirection = FlexDirection.Row,
                            alignItems = Align.Center,
                            marginBottom = 4,
                            width = Length.Percent(100),
                        },
                    };
                    var enableToggle = new Toggle { value = row.Enabled };
                    enableToggle.label = string.Empty;
                    enableToggle.AddToClassList(BaseField<bool>.noLabelVariantUssClassName);
                    enableToggle.style.flexShrink = 0;
                    enableToggle.RegisterValueChangedCallback(evt =>
                    {
                        rows[index].Enabled = evt.newValue;
                        Commit();
                    });
                    enableRow.Add(enableToggle);
                    enableRow.Add(new Label($"Deletion {index + 1}")
                    {
                        style = { color = new Color(0.7f, 0.7f, 0.7f), fontSize = 10, flexGrow = 1 },
                    });

                    var removeRule = new Button(() =>
                    {
                        rows.RemoveAt(index);
                        if (rows.Count == 0)
                            rows.Add(new GroupDeleteRuleRow());
                        Commit();
                        Rebuild();
                    })
                    {
                        text = "×",
                        tooltip = "Remove this deletion rule",
                    };
                    removeRule.style.width = 22;
                    removeRule.style.flexShrink = 0;
                    enableRow.Add(removeRule);
                    card.Add(enableRow);

                    var typeChoices = k_GroupDeleteTypeOptions.Select(o => o.label).ToList();
                    var typeIndex = Array.FindIndex(k_GroupDeleteTypeOptions, o => o.value == row.GroupType);
                    if (typeIndex < 0)
                        typeIndex = 0;
                    var typeField = new PopupField<string>(typeChoices, typeIndex);
                    typeField.RegisterValueChangedCallback(evt =>
                    {
                        var idx = typeChoices.IndexOf(evt.newValue);
                        rows[index].GroupType = idx >= 0 ? k_GroupDeleteTypeOptions[idx].value : "any";
                        Commit();
                    });
                    card.Add(CreateHoudiniLabeledRow("Group Type", typeField));

                    var namesField = MakeGroupDeleteNamesField(node, row.GroupNames, value =>
                    {
                        rows[index].GroupNames = value;
                        Commit();
                    });
                    card.Add(CreateHoudiniLabeledRow("Group Names", namesField));

                    root.Add(card);
                }
            }

            Rebuild();
            return root;
        }

        private static List<WrangleParamRow> ParseWrangleParameters(object raw)
        {
            var rows = new List<WrangleParamRow>();
            Dictionary<string, object> dict = null;
            if (raw is Dictionary<string, object> asDict)
            {
                dict = asDict;
            }
            else
            {
                var text = raw?.ToString() ?? "";
                if (string.IsNullOrWhiteSpace(text) || text.StartsWith("System.Collections", StringComparison.Ordinal))
                    return rows;
                try
                {
                    if (PcgMiniJson.Deserialize(text) is Dictionary<string, object> parsed)
                        dict = parsed;
                }
                catch
                {
                    return rows;
                }
            }

            if (dict == null)
                return rows;

            foreach (var (key, value) in dict)
            {
                var row = new WrangleParamRow { Name = key };
                if (value is Dictionary<string, object> obj)
                {
                    if (obj.TryGetValue("expr", out var exprObj) && exprObj != null)
                        row.Expr = exprObj.ToString() ?? "";
                    if (obj.TryGetValue("value", out var valObj) && valObj != null)
                        row.Value = Convert.ToDouble(valObj, CultureInfo.InvariantCulture);
                }
                else if (value is string s)
                {
                    row.Expr = s;
                }
                else if (value != null)
                {
                    try
                    {
                        row.Value = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                    }
                    catch
                    {
                        row.Expr = value.ToString() ?? "";
                    }
                }
                rows.Add(row);
            }
            return rows;
        }

        private static string SerializeWrangleParameters(List<WrangleParamRow> rows)
        {
            var dict = new Dictionary<string, object>();
            foreach (var row in rows)
            {
                if (string.IsNullOrWhiteSpace(row.Name))
                    continue;
                if (!string.IsNullOrWhiteSpace(row.Expr))
                {
                    dict[row.Name.Trim()] = new Dictionary<string, object>
                    {
                        ["value"] = row.Value,
                        ["expr"] = row.Expr,
                    };
                }
                else
                {
                    dict[row.Name.Trim()] = row.Value;
                }
            }
            return PcgMiniJson.Serialize(dict);
        }

        private VisualElement CreateWrangleParametersEditor(PcgManifestNodeView node)
        {
            var root = new VisualElement
            {
                style =
                {
                    marginTop = 2,
                    marginBottom = 8,
                    paddingLeft = 4,
                    paddingRight = 4,
                    paddingTop = 4,
                    paddingBottom = 4,
                    width = Length.Percent(100),
                    backgroundColor = new Color(0.18f, 0.2f, 0.18f, 0.55f),
                    borderTopLeftRadius = 3,
                    borderTopRightRadius = 3,
                    borderBottomLeftRadius = 3,
                    borderBottomRightRadius = 3,
                },
            };

            var rows = ParseWrangleParameters(node.CollectData().GetRaw("parameters"));
            if (rows.Count == 0)
                rows.Add(new WrangleParamRow { Name = "sag", Value = 3.0 });

            void Commit()
            {
                m_GraphView.WithUndo("Edit Wrangle Parameters", () =>
                    node.SetPropertyValue("parameters", SerializeWrangleParameters(rows)));
                NotifyGraphChanged();
            }

            void Rebuild()
            {
                root.Clear();

                var header = new VisualElement
                {
                    style =
                    {
                        flexDirection = FlexDirection.Row,
                        marginBottom = 4,
                        width = Length.Percent(100),
                    },
                };
                Label Col(string text, float grow, float width = 0)
                {
                    var label = new Label(text)
                    {
                        style =
                        {
                            color = new Color(0.65f, 0.85f, 0.65f),
                            fontSize = 10,
                            unityFontStyleAndWeight = FontStyle.Bold,
                            flexGrow = grow,
                            marginLeft = width > 0 ? 4 : 0,
                        },
                    };
                    if (width > 0)
                    {
                        label.style.flexGrow = 0;
                        label.style.width = width;
                    }
                    return label;
                }
                header.Add(Col("Name", 1));
                header.Add(Col("Value", 0, 64));
                header.Add(Col("Expr (optional)", 1.4f));
                header.Add(Col("", 0, 22));
                root.Add(header);

                for (var i = 0; i < rows.Count; i++)
                {
                    var index = i;
                    var row = rows[index];
                    var line = new VisualElement
                    {
                        style =
                        {
                            flexDirection = FlexDirection.Row,
                            alignItems = Align.Center,
                            marginBottom = 4,
                            width = Length.Percent(100),
                        },
                    };

                    var nameField = new TextField { value = row.Name };
                    nameField.style.flexGrow = 1;
                    nameField.style.minWidth = 60;
                    nameField.RegisterValueChangedCallback(evt =>
                    {
                        rows[index].Name = evt.newValue ?? "";
                        Commit();
                    });
                    line.Add(nameField);

                    var valueField = new FloatField { value = (float)row.Value };
                    valueField.style.width = 64;
                    valueField.style.marginLeft = 4;
                    valueField.RegisterValueChangedCallback(evt =>
                    {
                        rows[index].Value = evt.newValue;
                        Commit();
                    });
                    line.Add(valueField);

                    var exprField = new TextField { value = row.Expr };
                    exprField.style.flexGrow = 1.4f;
                    exprField.style.marginLeft = 4;
                    exprField.style.minWidth = 80;
                    exprField.tooltip = "Optional expression, e.g. @iteration or detail(\"iteration\",0)";
                    exprField.RegisterValueChangedCallback(evt =>
                    {
                        rows[index].Expr = evt.newValue ?? "";
                        Commit();
                    });
                    line.Add(exprField);

                    var remove = new Button(() =>
                    {
                        rows.RemoveAt(index);
                        if (rows.Count == 0)
                            rows.Add(new WrangleParamRow());
                        Commit();
                        Rebuild();
                    })
                    {
                        text = "×",
                        tooltip = "Remove channel",
                    };
                    remove.style.width = 22;
                    remove.style.marginLeft = 4;
                    line.Add(remove);
                    root.Add(line);
                }

                var add = new Button(() =>
                {
                    rows.Add(new WrangleParamRow { Name = "param" + rows.Count, Value = 0 });
                    Commit();
                    Rebuild();
                })
                {
                    text = "+ Add Channel",
                };
                add.style.marginTop = 2;
                add.style.alignSelf = Align.FlexStart;
                root.Add(add);
            }

            Rebuild();
            return root;
        }

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

        private VisualElement CreateMoveCentroidButton(PcgManifestNodeView node)
        {
            var button = new Button(() =>
            {
                if (!TryGetTransformMeshCentroid(node, out var centroid))
                {
                    EditorUtility.DisplayDialog(
                        "Move Centroid to Origin",
                        "Cook or preview this Transform Mesh node first so its geometry bounds are available.",
                        "OK");
                    return;
                }

                m_GraphView.WithUndo("Move Centroid to Origin", () =>
                {
                    var current = PcgVector3Property.ResolveFromNodeData(
                        node.CollectData(), "translate", Vector3.zero);
                    var next = current - centroid;
                    node.SetPropertyValue("translate", PcgVector3Property.Format(next));
                });
                NotifyGraphChanged();
                ShowNode(node);
            })
            {
                text = "Move Centroid to Origin",
                tooltip = "Adjust Translate so the cooked geometry centroid moves to the origin.",
            };
            button.style.height = 24;
            button.style.marginTop = 6;
            button.style.marginBottom = 4;
            return button;
        }

        private bool TryGetTransformMeshCentroid(PcgManifestNodeView node, out Vector3 centroid)
        {
            centroid = Vector3.zero;
            if (m_GraphView?.HostWindow is not PcgGraphEditorWindow window)
                return false;

            var component = Selection.activeGameObject != null
                ? Selection.activeGameObject.GetComponent<PcgGraphComponent>()
                : null;
            if (component == null)
                return false;

            if (!string.Equals(window.PreviewNodeId, node.NodeId, StringComparison.Ordinal))
                return false;

            var preview = component.PolygonPreview;
            if (preview?.Points == null || preview.Points.Length == 0)
                return false;

            var sum = Vector3.zero;
            foreach (var point in preview.Points)
                sum += point;
            centroid = sum / preview.Points.Length;
            return true;
        }

        private VisualElement CreateFbxExportActions(PcgManifestNodeView node)
        {
            var container = new VisualElement
            {
                style =
                {
                    marginTop = 8,
                    paddingTop = 8,
                    borderTopWidth = 1,
                    borderTopColor = new Color(0.3f, 0.3f, 0.3f),
                },
            };

            var browse = new Button(() =>
            {
                var current = node.CollectData().GetRaw("path")?.ToString();
                var selected = PcgFbxExportController.BrowseForPath(current);
                if (string.IsNullOrEmpty(selected))
                    return;
                m_GraphView.WithUndo("Choose FBX Export Path", () =>
                    node.SetPropertyValue("path", selected));
                NotifyGraphChanged();
                ShowNode(node);
            })
            {
                text = "Browse…",
                tooltip = "Choose an FBX file. Project-relative paths remain portable.",
            };
            container.Add(browse);

            var export = new Button(() =>
            {
                if (m_GraphView.HostWindow is PcgGraphEditorWindow window)
                    PcgFbxExportController.Export(window, node);
            })
            {
                text = "Export Now",
                tooltip = "Cook this node's upstream geometry and write the FBX once.",
            };
            export.style.height = 28;
            export.style.marginTop = 5;
            export.style.unityFontStyleAndWeight = FontStyle.Bold;
            container.Add(export);

            container.Add(new Label("Editor-only ROP: preview and automatic cooks never write this file.")
            {
                style =
                {
                    color = new Color(0.55f, 0.7f, 0.55f),
                    fontSize = 9,
                    marginTop = 4,
                    whiteSpace = WhiteSpace.Normal,
                },
            });
            return container;
        }

        
        private VisualElement CreateMeshyGenerateSection(PcgManifestNodeView node)
        {
            var container = new VisualElement
            {
                style =
                {
                    marginTop = 8,
                    paddingTop = 8,
                    borderTopWidth = 1,
                    borderTopColor = new Color(0.3f, 0.3f, 0.3f),
                },
            };

            var statusLabel = new Label
            {
                style =
                {
                    fontSize = 9,
                    whiteSpace = WhiteSpace.Normal,
                    marginBottom = 4,
                    display = DisplayStyle.None,
                },
            };
            container.Add(statusLabel);

            var progressBar = new ProgressBar
            {
                lowValue = 0f,
                highValue = 1f,
                style = { marginBottom = 4, display = DisplayStyle.None },
            };
            container.Add(progressBar);

            var button = new Button
            {
                style =
                {
                    height = 28,
                    unityFontStyleAndWeight = FontStyle.Bold,
                },
            };
            container.Add(button);

            container.Add(new Label("Calls the Meshy API and caches the GLB into Library/PCG/MeshyCache. Consumes API credits.")
            {
                style =
                {
                    color = new Color(0.55f, 0.7f, 0.55f),
                    fontSize = 9,
                    marginTop = 4,
                    whiteSpace = WhiteSpace.Normal,
                },
            });

            void RefreshIdleUi()
            {
                var state = PcgMeshyGenerateController.GetState(node.NodeId);
                var hasCache = PcgMeshyResolver.TryGetCachedModelPath(
                    node.NodeId, node.CollectData(), out var cachePath);

                button.text = hasCache ? "Regenerate" : "Generate";
                button.tooltip = hasCache
                    ? "Call the Meshy API again and overwrite the cached GLB."
                    : "Create a Meshy Image-to-3D task and download the GLB.";
                progressBar.style.display = DisplayStyle.None;

                if (!string.IsNullOrEmpty(state.Error))
                {
                    statusLabel.text = state.Error;
                    statusLabel.style.color = new Color(0.9f, 0.45f, 0.4f);
                    statusLabel.style.display = DisplayStyle.Flex;
                }
                else if (state.Succeeded && !string.IsNullOrEmpty(state.ModelPath))
                {
                    statusLabel.text = $"Cached → {state.ModelPath}";
                    statusLabel.style.color = new Color(0.55f, 0.7f, 0.55f);
                    statusLabel.style.display = DisplayStyle.Flex;
                }
                else if (hasCache)
                {
                    statusLabel.text = $"Cached → {cachePath}";
                    statusLabel.style.color = new Color(0.6f, 0.6f, 0.6f);
                    statusLabel.style.display = DisplayStyle.Flex;
                }
                else
                {
                    statusLabel.style.display = DisplayStyle.None;
                }
            }

            var wasRunning = false;
            button.clicked += () =>
            {
                if (PcgMeshyGenerateController.IsRunning(node.NodeId))
                {
                    PcgMeshyGenerateController.Cancel(node.NodeId);
                    return;
                }

                PcgMeshyGenerateController.Begin(node.NodeId, node.CollectData(), path =>
                {
                    m_GraphView.WithUndo("Meshy 3D Generate", () =>
                    {
                        node.SetPropertyValue("path", path);
                        node.SetPropertyValue("projectRoot", "");
                    });
                    NotifyGraphChanged();
                    if (m_CurrentNode == node)
                        ShowNode(node);
                });

                if (PcgMeshyGenerateController.IsRunning(node.NodeId))
                {
                    wasRunning = true;
                    button.text = "Cancel";
                    progressBar.style.display = DisplayStyle.Flex;
                    statusLabel.style.display = DisplayStyle.None;
                }
                else
                {
                    RefreshIdleUi();
                }
            };

            container.schedule.Execute(() =>
            {
                var state = PcgMeshyGenerateController.GetState(node.NodeId);
                if (state.Running)
                {
                    wasRunning = true;
                    button.text = "Cancel";
                    progressBar.style.display = DisplayStyle.Flex;
                    progressBar.value = Mathf.Clamp01(state.Progress);
                    progressBar.title = state.Message ?? "";
                    return;
                }

                if (wasRunning)
                {
                    wasRunning = false;
                    if (m_CurrentNode == node)
                        ShowNode(node);
                    else
                        RefreshIdleUi();
                }
            }).Every(100);

            RefreshIdleUi();
            var initial = PcgMeshyGenerateController.GetState(node.NodeId);
            if (initial.Running)
            {
                wasRunning = true;
                button.text = "Cancel";
                progressBar.style.display = DisplayStyle.Flex;
                progressBar.value = Mathf.Clamp01(initial.Progress);
                progressBar.title = initial.Message ?? "";
                statusLabel.style.display = DisplayStyle.None;
            }
            return container;
        }

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
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
            return field;
        }

        private FloatField MakeFloatField(string key, object val, Action<float> onSet)
        {
            var field = new FloatField { value = Convert.ToSingle(val ?? 0f, CultureInfo.InvariantCulture) };
            field.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
            return field;
        }

        private VisualElement MakeVector3Field(PcgManifestNodeView node, string key, Action<object> onSet)
        {
            var current = PcgVector3Property.ResolveFromNodeData(
                node.CollectData(),
                key,
                propDefaultVector(key));
            return PcgInspectorWidgets.CreateVector3Row(
                current,
                next =>
                {
                    onSet(PcgVector3Property.Format(next));
                    NotifyGraphChanged();
                },
                next =>
                {
                    m_GraphView.WithUndo("Change Property", () =>
                        onSet(PcgVector3Property.Format(next)));
                    NotifyGraphChanged();
                });
        }

        private Vector3 propDefaultVector(string key)
        {
            if (m_CurrentNode is not PcgManifestNodeView manifestNode ||
                !PcgNodeManifest.TryGet(manifestNode.NodeType, out var def) ||
                !def.properties.TryGetValue(key, out var prop))
                return Vector3.zero;
            return PcgVector3Property.ParseOrDefault(prop.defaultValue, Vector3.zero);
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
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
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

            if (prop.uiHint == "radio")
                return MakeRadioEnumField(labels, values, selectedIdx, onSet);

            var popup = new PopupField<string>(labels, selectedIdx);
            popup.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () =>
                {
                    var idx = labels.IndexOf(evt.newValue);
                    if (idx >= 0 && idx < values.Count)
                        onSet(values[idx]);
                });
                NotifyGraphChanged();
            });
            return popup;
        }

        private VisualElement MakeRadioEnumField(
            List<string> labels, List<string> values, int selectedIdx, Action<string> onSet)
        {
            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    flexGrow = 1,
                    marginBottom = 2,
                },
            };

            var buttons = new List<Button>();
            void Refresh(int active)
            {
                for (var i = 0; i < buttons.Count; i++)
                {
                    var selected = i == active;
                    buttons[i].style.backgroundColor = selected
                        ? new Color(0.22f, 0.35f, 0.55f)
                        : new Color(0.16f, 0.16f, 0.16f);
                    buttons[i].style.color = selected
                        ? Color.white
                        : new Color(0.75f, 0.75f, 0.75f);
                }
            }

            for (var i = 0; i < labels.Count; i++)
            {
                var index = i;
                var button = new Button(() =>
                {
                    m_GraphView.WithUndo("Change Property", () => onSet(values[index]));
                    NotifyGraphChanged();
                    Refresh(index);
                })
                {
                    text = labels[i],
                    style =
                    {
                        flexGrow = 1,
                        marginLeft = 0,
                        marginRight = 0,
                        borderTopLeftRadius = i == 0 ? 3 : 0,
                        borderBottomLeftRadius = i == 0 ? 3 : 0,
                        borderTopRightRadius = i == labels.Count - 1 ? 3 : 0,
                        borderBottomRightRadius = i == labels.Count - 1 ? 3 : 0,
                        borderTopWidth = 1,
                        borderBottomWidth = 1,
                        borderLeftWidth = 1,
                        borderRightWidth = 1,
                        borderTopColor = new Color(0.09f, 0.09f, 0.09f),
                        borderBottomColor = new Color(0.09f, 0.09f, 0.09f),
                        borderLeftColor = new Color(0.09f, 0.09f, 0.09f),
                        borderRightColor = new Color(0.09f, 0.09f, 0.09f),
                        unityTextAlign = TextAnchor.MiddleCenter,
                    },
                };
                buttons.Add(button);
                row.Add(button);
            }
            Refresh(selectedIdx);
            return row;
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

        private TextField MakeTextField(
            string key, ManifestPropertyDef prop, object val, Action<string> onSet)
        {
            var field = new TextField
            {
                value = val?.ToString() ?? "",
                multiline = prop.multiline,
            };
            if (prop.multiline)
            {
                field.style.minHeight = Math.Max(2, prop.lines) * 18;
                field.style.whiteSpace = WhiteSpace.Normal;
            }
            field.RegisterValueChangedCallback(evt =>
            {
                m_GraphView.WithUndo("Change Property", () => onSet(evt.newValue));
                NotifyGraphChanged();
            });
            return field;
        }

        // ─── Group Resolution ──────────────────────────────────────

        public struct AvailableGroup
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
        public List<AvailableGroup> ResolveUpstreamGroups(string startNodeId)
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

                // Walk geometry edges (SpatialMesh / SpatialGeometry / SpatialSpline).
                var sourceHandle = edge.output.userData as string ?? edge.output.portName;
                var outputPinType = PcgNodeManifest.GetOutputPinType(sourceNode.NodeType, sourceHandle);
                if (!PcgNodeManifest.IsSpatialGeometryFamilyPin(outputPinType))
                    continue;

                CollectNodeGroups(sourceNode, result, seen);
                FindUpstreamMeshGroups(sourceNode.NodeId, result, seen, visited);
            }
        }

        public static void CollectNodeGroups(
            PcgGraphNodeBase node,
            List<AvailableGroup> result,
            HashSet<string> seen)
        {
            if (!PcgNodeManifest.TryGet(node.NodeType, out var def))
                return;

            var data = node.CollectData();
            foreach (var declared in PcgGroupResolution.ResolveOutputGroups(def, data))
            {
                var dedupKey = $"{declared.name}:{declared.domain}";
                if (!seen.Add(dedupKey))
                    continue;

                result.Add(new AvailableGroup
                {
                    name = declared.name,
                    domain = declared.domain,
                    sourceNodeId = node.NodeId,
                    sourceNodeType = node.NodeType,
                    label = declared.label,
                });
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

            // Houdini-style: text field + expand button
            var row = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row, alignItems = Align.Center },
            };

            var textField = new TextField { value = currentVal };
            textField.style.flexGrow = 1;
            textField.style.flexShrink = 1;
            textField.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Group", () => onSet(evt.newValue)));
            row.Add(textField);

            var expandBtn = new Button { text = "\u25be", tooltip = "Browse available groups" };
            expandBtn.style.width = 22;
            expandBtn.style.flexShrink = 0;
            expandBtn.style.marginLeft = 2;

            if (available.Count > 0)
            {
                expandBtn.clicked += () =>
                {
                    var menu = new GenericMenu();
                    var bySource = available
                        .GroupBy(g => g.sourceNodeType ?? "Unknown")
                        .OrderBy(g => g.Key);

                    foreach (var sourceGroup in bySource)
                    {
                        foreach (var g in sourceGroup)
                        {
                            var capturedName = g.name;
                            var path = $"{sourceGroup.Key}/{g.name} ({g.domain})";
                            var isCurrent = textField.value == capturedName;
                            menu.AddItem(new GUIContent(path), isCurrent, () =>
                            {
                                m_GraphView.WithUndo("Pick Group", () =>
                                {
                                    onSet(capturedName);
                                    textField.value = capturedName;
                                    NotifyGraphChanged();
                                });
                            });
                        }
                    }

                    var r = expandBtn.worldBound;
                    menu.DropDown(new Rect(r.x, r.y + r.height, 0, 0));
                };
            }
            else
            {
                expandBtn.SetEnabled(false);
            }

            row.Add(expandBtn);
            container.Add(row);

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

            // Houdini-style: text field + expand button
            var row = new VisualElement
            {
                style = { flexDirection = FlexDirection.Row, alignItems = Align.Center },
            };

            var textField = new TextField { value = currentStr };
            textField.style.flexGrow = 1;
            textField.style.flexShrink = 1;
            textField.RegisterValueChangedCallback(evt =>
                m_GraphView.WithUndo("Change Groups", () => onSet(evt.newValue)));
            row.Add(textField);

            var expandBtn = new Button { text = "\u25be", tooltip = "Browse available groups" };
            expandBtn.style.width = 22;
            expandBtn.style.flexShrink = 0;
            expandBtn.style.marginLeft = 2;

            if (available.Count > 0)
            {
                expandBtn.clicked += () =>
                {
                    var menu = new GenericMenu();
                    var currentSelected = new HashSet<string>(
                        textField.value
                            .Split(',', StringSplitOptions.RemoveEmptyEntries)
                            .Select(s => s.Trim())
                            .Where(s => !string.IsNullOrEmpty(s)));

                    var bySource = available
                        .GroupBy(g => g.sourceNodeType ?? "Unknown")
                        .OrderBy(g => g.Key);

                    foreach (var sourceGroup in bySource)
                    {
                        foreach (var g in sourceGroup)
                        {
                            var capturedName = g.name;
                            var path = $"{sourceGroup.Key}/{g.name} ({g.domain})";
                            var isSelected = currentSelected.Contains(capturedName);
                            menu.AddItem(new GUIContent(path), isSelected, () =>
                            {
                                m_GraphView.WithUndo("Toggle Group", () =>
                                {
                                    if (currentSelected.Contains(capturedName))
                                        currentSelected.Remove(capturedName);
                                    else
                                        currentSelected.Add(capturedName);

                                    var newVal = string.Join(",", currentSelected);
                                    onSet(newVal);
                                    textField.value = newVal;
                                    NotifyGraphChanged();
                                });
                            });
                        }
                    }

                    var r = expandBtn.worldBound;
                    menu.DropDown(new Rect(r.x, r.y + r.height, 0, 0));
                };
            }
            else
            {
                expandBtn.SetEnabled(false);
            }

            row.Add(expandBtn);
            container.Add(row);

            return container;
        }
    }
}
