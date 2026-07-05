using System;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Custom Port that installs a <see cref="PcgEdgeConnectorListener"/> on its
    /// EdgeConnector, mirroring Shader Graph's ShaderPort pattern.
    /// This enables OnDropOutsidePort → filtered node search → auto-connect.
    /// </summary>
    public sealed class PcgPort : Port
    {
        private PcgPort(Orientation orientation, Direction direction, Capacity capacity, Type type)
            : base(orientation, direction, capacity, type) { }

        /// <summary>Create a Port with our custom edge connector listener.</summary>
        public static PcgPort Create(Orientation orientation, Direction direction,
            Capacity capacity, Type type)
        {
            var port = new PcgPort(orientation, direction, capacity, type);
            var listener = new PcgEdgeConnectorListener();
            var connector = new EdgeConnector<Edge>(listener);
            port.AddManipulator(connector);
            return port;
        }
    }

    /// <summary>
    /// Edge connector listener that handles:
    /// - OnDrop: edge already added by EdgeDragHelper, no action needed (undo via OnGraphViewChanged)
    /// - OnDropOutsidePort: opens filtered node search window at drop position
    /// </summary>
    public sealed class PcgEdgeConnectorListener : IEdgeConnectorListener
    {
        public void OnDrop(GraphView graphView, Edge edge)
        {
            // Edge is already connected and added by the EdgeDragHelper.
            // Undo is handled by PcgGraphView.OnGraphViewChanged.
        }

        public void OnDropOutsidePort(Edge edge, Vector2 position)
        {
            // position is in screen coordinates
            var port = edge.output ?? edge.input;
            if (port == null) return;

            var graphView = port.GetFirstAncestorOfType<PcgGraphView>();
            if (graphView == null) return;

            graphView.ShowPortDragSearchWindow(edge, position);
        }
    }
}
