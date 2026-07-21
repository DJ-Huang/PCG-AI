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
    /// Minimal interface editor for linked Subgraph Asset mode.
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
            "SpatialMesh", "Texture", "HeightField",
        };

        public PcgSubgraphInterfacePanel(PcgGraphView graphView)
        {
            m_GraphView = graphView;
            style.flexDirection = FlexDirection.Column;
            style.minWidth = 220;
            style.backgroundColor = new Color(0.18f, 0.18f, 0.18f, 0.95f);
            style.paddingLeft = 8;
            style.paddingRight = 8;
            style.paddingTop = 8;
            style.paddingBottom = 8;

            Add(new Label("Subgraph Interface") { style = { unityFontStyleAndWeight = FontStyle.Bold } });
            Add(new Label("Inputs"));
            Add(m_InputList);
            var addIn = new Button(() => AddPort(inputs: true)) { text = "+ Input" };
            Add(addIn);
            Add(new Label("Outputs") { style = { marginTop = 8 } });
            Add(m_OutputList);
            var addOut = new Button(() => AddPort(inputs: false)) { text = "+ Output" };
            Add(addOut);
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
                m_InputList.Add(MakeRow(port, inputs: true));
            foreach (var port in m_Definition.outputs.ToList())
                m_OutputList.Add(MakeRow(port, inputs: false));
        }

        private VisualElement MakeRow(PcgSubgraphPort port, bool inputs)
        {
            var row = new VisualElement { style = { flexDirection = FlexDirection.Row, marginBottom = 4 } };
            var nameField = new TextField { value = port.name, style = { flexGrow = 1, minWidth = 60 } };
            nameField.RegisterValueChangedCallback(evt =>
            {
                port.name = evt.newValue;
                m_GraphView?.NotifyInterfaceChanged();
            });
            var typeField = new PopupField<string>(PinTypes.ToList(),
                Mathf.Max(0, Array.IndexOf(PinTypes, port.pinType)));
            typeField.RegisterValueChangedCallback(evt =>
            {
                port.pinType = evt.newValue;
                m_GraphView?.NotifyInterfaceChanged();
            });
            var idLabel = new Label(port.id) { style = { width = 70, overflow = Overflow.Hidden } };
            idLabel.tooltip = "Port id (stable, read-only)";
            var remove = new Button(() => RemovePort(port, inputs)) { text = "x", style = { width = 22 } };
            row.Add(nameField);
            row.Add(typeField);
            row.Add(idLabel);
            row.Add(remove);
            return row;
        }

        private void AddPort(bool inputs)
        {
            if (m_Definition == null)
                return;
            var port = new PcgSubgraphPort
            {
                id = Guid.NewGuid().ToString("N"),
                name = inputs ? $"in_{m_Definition.inputs.Count + 1}" : $"out_{m_Definition.outputs.Count + 1}",
                pinType = "Any",
            };
            if (inputs)
                m_Definition.inputs.Add(port);
            else
                m_Definition.outputs.Add(port);
            Rebuild();
            m_GraphView?.NotifyInterfaceChanged();
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
            if (inputs)
                m_Definition.inputs.Remove(port);
            else
                m_Definition.outputs.Remove(port);
            Rebuild();
            m_GraphView?.NotifyInterfaceChanged();
        }
    }
}
