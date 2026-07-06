using System;
using System.Collections.Generic;
using System.Linq;
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
            var listener = new PcgEdgeConnectorListener();
            var connector = new EdgeConnector<Edge>(listener);
            // EdgeManipulator reconnect reads port.edgeConnector.edgeDragHelper — must set m_EdgeConnector.
            var port = new PcgPort(orientation, direction, capacity, type)
            {
                m_EdgeConnector = connector,
            };
            port.AddManipulator(connector);
            return port;
        }
    }

    /// <summary>
    /// Edge connector listener mirroring GraphView DefaultEdgeConnectorListener,
    /// with OnDropOutsidePort → filtered node search.
    /// </summary>
    public sealed class PcgEdgeConnectorListener : IEdgeConnectorListener
    {
        public void OnDrop(GraphView graphView, Edge edge)
        {
            var edgesToCreate = new List<Edge> { edge };
            var edgesToDelete = new List<Edge>();

            if (edge.input != null && edge.input.capacity == Port.Capacity.Single)
            {
                foreach (var existing in edge.input.connections.ToList())
                {
                    if (existing != edge)
                        edgesToDelete.Add(existing);
                }
            }

            if (edge.output != null && edge.output.capacity == Port.Capacity.Single)
            {
                foreach (var existing in edge.output.connections.ToList())
                {
                    if (existing != edge)
                        edgesToDelete.Add(existing);
                }
            }

            if (edgesToDelete.Count > 0)
                graphView.DeleteElements(edgesToDelete);

            var change = new GraphViewChange { edgesToCreate = edgesToCreate };
            if (graphView.graphViewChanged != null)
                edgesToCreate = graphView.graphViewChanged(change).edgesToCreate ?? edgesToCreate;

            foreach (var e in edgesToCreate)
            {
                if (e?.input == null || e.output == null)
                    continue;

                if (e.parent == null)
                    graphView.AddElement(e);

                if (!ContainsEdge(e.input, e))
                    e.input.Connect(e);
                if (!ContainsEdge(e.output, e))
                    e.output.Connect(e);
            }
        }

        private static bool ContainsEdge(Port port, Edge edge)
        {
            foreach (var connection in port.connections)
            {
                if (connection == edge)
                    return true;
            }

            return false;
        }

        public void OnDropOutsidePort(Edge edge, Vector2 position)
        {
            var draggedPort = edge.output?.edgeConnector?.edgeDragHelper?.draggedPort
                              ?? edge.input?.edgeConnector?.edgeDragHelper?.draggedPort;
            if (draggedPort == null)
                draggedPort = edge.output ?? edge.input;
            if (draggedPort == null)
                return;

            var graphView = draggedPort.GetFirstAncestorOfType<PcgGraphView>();
            if (graphView == null)
                return;

            graphView.ShowPortDragSearchWindow(edge, position);
        }
    }
}
