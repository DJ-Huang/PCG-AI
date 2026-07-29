using System;
using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Interface editor for inline subgraphs and linked Subgraph Asset mode.
    /// Inputs/outputs are wired from here; SubgraphInput/Output nodes stay in data but hidden on canvas.
    /// </summary>
    public sealed class PcgSubgraphInterfacePanel : VisualElement
    {
        private readonly PcgGraphView m_GraphView;
        private PcgSubgraphDefinition m_Definition;
        private readonly VisualElement m_InputList = new();
        private readonly VisualElement m_OutputList = new();

        private static readonly string[] PinTypes =
        {
            "Any", "Param", "SpatialPoint", "SpatialSpline", "SpatialSurface",
            "SpatialMesh", "SpatialGeometry", "Texture", "HeightField",
        };

        public PcgSubgraphInterfacePanel(PcgGraphView graphView)
        {
            m_GraphView = graphView;
            style.flexDirection = FlexDirection.Column;
            style.width = 300;
            style.minWidth = 280;
            style.maxWidth = 340;
            style.flexGrow = 0;
            style.flexShrink = 0;
            style.borderRightWidth = 1;
            style.borderRightColor = new Color(0.15f, 0.15f, 0.15f);
            style.backgroundColor = new Color(0.18f, 0.18f, 0.18f, 0.95f);
            style.paddingLeft = 8;
            style.paddingRight = 8;
            style.paddingTop = 8;
            style.paddingBottom = 8;

            Add(new Label("Subgraph Interface") { style = { unityFontStyleAndWeight = FontStyle.Bold } });
            Add(new Label("Inputs") { style = { fontSize = 11, color = new Color(0.7f, 0.7f, 0.7f) } });
            Add(new Label("Right-click graph → Subgraph Interface → Input/Output to place connector") { style = { fontSize = 10, color = new Color(0.55f, 0.55f, 0.55f), marginBottom = 4, whiteSpace = WhiteSpace.Normal } });
            Add(m_InputList);
            Add(new Button(() => AddPort(inputs: true)) { text = "+ Input" });
            Add(new Label("Outputs") { style = { marginTop = 8 } });
            Add(m_OutputList);
            Add(new Button(() => AddPort(inputs: false)) { text = "+ Output" });
        }

        public void Bind(PcgSubgraphDefinition definition)
        {
            m_Definition = definition;
            Rebuild();
        }

        private void Rebuild()
        {
            m_InputList.Clear();
            m_OutputList.Clear();
            if (m_Definition == null)
                return;

            foreach (var port in m_Definition.inputs.ToList())
                m_InputList.Add(MakeInputRow(port));
            foreach (var port in m_Definition.outputs.ToList())
                m_OutputList.Add(MakeOutputRow(port));
        }

        private VisualElement MakeInputRow(PcgSubgraphPort port) => MakePortCard(port, isInput: true);

        private VisualElement MakeOutputRow(PcgSubgraphPort port) => MakePortCard(port, isInput: false);

        private VisualElement MakePortCard(PcgSubgraphPort port, bool isInput)
        {
            var card = new VisualElement
            {
                style =
                {
                    marginBottom = 8,
                    paddingBottom = 6,
                    borderBottomWidth = 1,
                    borderBottomColor = new Color(0.28f, 0.28f, 0.28f),
                },
            };

            var header = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                },
            };

            var handle = isInput ? MakeInputHandle(port) : MakeOutputHandle(port);
            handle.style.flexShrink = 0;

            var nameField = new TextField
            {
                value = port.name,
                style =
                {
                    flexGrow = 1,
                    flexShrink = 1,
                    minWidth = 0,
                    marginLeft = 6,
                    marginRight = 4,
                },
            };
            nameField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView?.WithUndo("Rename Interface Port", () =>
                {
                    port.name = evt.newValue;
                    m_GraphView.NotifyInterfaceChanged();
                });
            });

            header.Add(handle);
            header.Add(nameField);
            header.Add(MakeConnectionBadge(isInput ? IsInputConnected(port.id) : IsOutputConnected(port.id)));
            header.Add(MakeMoveButton(port, isInput, -1));
            header.Add(MakeMoveButton(port, isInput, 1));
            header.Add(MakeRemoveButton(port, isInput));

            var typeRow = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    marginTop = 4,
                    paddingLeft = 26,
                },
            };
            typeRow.Add(new Label("Type")
            {
                style =
                {
                    width = 34,
                    fontSize = 10,
                    color = new Color(0.65f, 0.65f, 0.65f),
                    marginRight = 4,
                },
            });
            var typeField = new PopupField<string>(
                PinTypes.ToList(),
                Mathf.Max(0, Array.IndexOf(PinTypes, port.pinType)))
            {
                style =
                {
                    flexGrow = 1,
                    flexShrink = 1,
                    minWidth = 0,
                },
            };
            typeField.RegisterValueChangedCallback(evt =>
            {
                m_GraphView?.WithUndo("Retype Interface Port", () =>
                {
                    port.pinType = evt.newValue;
                    m_GraphView.NotifyInterfaceChanged();
                });
            });
            typeRow.Add(typeField);

            card.Add(header);
            card.Add(typeRow);
            return card;
        }

        private static VisualElement MakeInputHandle(PcgSubgraphPort port)
        {
            return MakePinElement(
                new Color(0.35f, 0.85f, 0.45f),
                new Color(0.2f, 0.55f, 0.3f),
                "Subgraph input");
        }

        private static VisualElement MakeOutputHandle(PcgSubgraphPort port)
        {
            return MakePinElement(
                new Color(0.55f, 0.55f, 0.55f),
                new Color(0.35f, 0.35f, 0.35f),
                "Subgraph output");
        }

        private static VisualElement MakePinElement(Color fill, Color border, string tooltip)
        {
            return new VisualElement
            {
                tooltip = tooltip,
                pickingMode = PickingMode.Position,
                style =
                {
                    width = 20,
                    height = 20,
                    borderTopLeftRadius = 10,
                    borderTopRightRadius = 10,
                    borderBottomLeftRadius = 10,
                    borderBottomRightRadius = 10,
                    backgroundColor = fill,
                    borderTopWidth = 1,
                    borderRightWidth = 1,
                    borderBottomWidth = 1,
                    borderLeftWidth = 1,
                    borderTopColor = border,
                    borderRightColor = border,
                    borderBottomColor = border,
                    borderLeftColor = border,
                },
            };
        }

        private static Label MakeConnectionBadge(bool connected)
        {
            return new Label(connected ? "●" : "○")
            {
                tooltip = connected ? "Connected" : "Not connected",
                style =
                {
                    width = 14,
                    flexShrink = 0,
                    marginLeft = 2,
                    marginRight = 2,
                    unityTextAlign = TextAnchor.MiddleCenter,
                    color = connected ? new Color(0.35f, 0.85f, 0.45f) : new Color(0.45f, 0.45f, 0.45f),
                },
            };
        }

        private Button MakeMoveButton(PcgSubgraphPort port, bool inputs, int direction)
        {
            var list = inputs ? m_Definition?.inputs : m_Definition?.outputs;
            var index = list?.IndexOf(port) ?? -1;
            var canMove = list != null && index >= 0 &&
                          index + direction >= 0 && index + direction < list.Count;
            var button = new Button(() => MovePort(port, inputs, direction))
            {
                text = direction < 0 ? "↑" : "↓",
                tooltip = direction < 0 ? "Move up" : "Move down",
                style =
                {
                    width = 20,
                    height = 20,
                    flexShrink = 0,
                    paddingLeft = 0,
                    paddingRight = 0,
                    paddingTop = 0,
                    paddingBottom = 0,
                    marginLeft = 0,
                    fontSize = 11,
                },
            };
            button.SetEnabled(canMove);
            return button;
        }

        private void MovePort(PcgSubgraphPort port, bool inputs, int direction)
        {
            if (m_Definition == null || port == null)
                return;
            var list = inputs ? m_Definition.inputs : m_Definition.outputs;
            var index = list.IndexOf(port);
            if (index < 0)
                return;
            var newIndex = index + direction;
            if (newIndex < 0 || newIndex >= list.Count)
                return;
            m_GraphView?.WithUndo("Reorder Interface Port", () =>
            {
                list.RemoveAt(index);
                list.Insert(newIndex, port);
                Rebuild();
                m_GraphView?.NotifyInterfaceChanged();
            });
        }

        private Button MakeRemoveButton(PcgSubgraphPort port, bool inputs)
        {
            return new Button(() => RemovePort(port, inputs))
            {
                text = "×",
                style =
                {
                    width = 20,
                    height = 20,
                    flexShrink = 0,
                    paddingLeft = 0,
                    paddingRight = 0,
                    paddingTop = 0,
                    paddingBottom = 0,
                    marginLeft = 0,
                    fontSize = 12,
                },
            };
        }

        private bool IsInputConnected(string portId)
        {
            var inputNodeId = PcgSubgraphInterfaceUtility.GetSubgraphInputNodeId(m_Definition);
            if (string.IsNullOrEmpty(inputNodeId))
                return false;
            return m_Definition.edges?.Any(edge =>
                edge != null &&
                edge.source == inputNodeId &&
                edge.sourceHandle == portId) == true;
        }

        private bool IsOutputConnected(string portId)
        {
            var outputNodeId = PcgSubgraphInterfaceUtility.GetSubgraphOutputNodeId(m_Definition);
            if (string.IsNullOrEmpty(outputNodeId))
                return false;
            return m_Definition.edges?.Any(edge =>
                edge != null &&
                edge.target == outputNodeId &&
                edge.targetHandle == portId) == true;
        }

        private void AddPort(bool inputs)
        {
            if (m_Definition == null)
                return;
            m_GraphView?.WithUndo(inputs ? "Add Interface Input" : "Add Interface Output", () =>
            {
                var port = new PcgSubgraphPort
                {
                    id = Guid.NewGuid().ToString("N"),
                    name = inputs ? $"in_{m_Definition.inputs.Count + 1}" : $"out_{m_Definition.outputs.Count + 1}",
                    pinType = "Any",
                };
                if (inputs)
                {
                    m_Definition.inputs.Add(port);
                    PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(m_Definition);
                }
                else
                {
                    m_Definition.outputs.Add(port);
                    PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(m_Definition);
                }
                Rebuild();
                m_GraphView?.NotifyInterfaceChanged();
            });
        }

        private void RemovePort(PcgSubgraphPort port, bool inputs)
        {
            if (m_Definition == null || port == null)
                return;
            if (!EditorUtility.DisplayDialog(
                    "Delete Port",
                    $"Delete port '{port.name}' ({port.id})? Connected edges may become invalid.",
                    "Delete", "Cancel"))
                return;
            m_GraphView?.WithUndo("Remove Interface Port", () =>
            {
                if (inputs)
                {
                    PcgSubgraphInterfaceUtility.RemoveInternalInputEdges(m_Definition, port.id);
                    m_Definition.inputs.Remove(port);
                }
                else
                {
                    PcgSubgraphInterfaceUtility.RemoveInternalOutputEdges(m_Definition, port.id);
                    m_Definition.outputs.Remove(port);
                }
                Rebuild();
                m_GraphView?.NotifyInterfaceChanged();
            });
        }
    }
}
