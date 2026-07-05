using System;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public static class PcgGraphNodeFactory
    {
        private static int _nodeCounter = 100;

        public static void ResetCounterFromDocument(PcgGraphDocument doc)
        {
            var max = 0;
            foreach (var node in doc.nodes)
            {
                if (node.id != null && node.id.StartsWith("n", StringComparison.Ordinal) &&
                    int.TryParse(node.id.Substring(1), out var num) && num > max)
                {
                    max = num;
                }
            }

            _nodeCounter = Math.Max(_nodeCounter, max);
        }

        public static string NextNodeId() => $"n{++_nodeCounter}";

        public static PcgGraphNodeBase Create(string type, string id, Vector2 position, PcgNodeData data = null)
        {
            if (!PcgNodeManifest.TryGet(type, out var def))
                throw new ArgumentException($"Unknown node type: {type}");

            var node = new PcgManifestNodeView(def);
            node.Initialize(id, position);
            node.ApplyData(data ?? PcgNodeManifest.DefaultDataFor(type));
            return node;
        }
    }
}
