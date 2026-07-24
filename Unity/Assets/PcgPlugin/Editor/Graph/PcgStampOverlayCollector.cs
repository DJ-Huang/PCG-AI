#if UNITY_EDITOR
using System.Collections.Generic;
using System.Globalization;
using DJTechRuntime.PCG;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>One MaskByObject geometry stamp reconstructed from graph node data.</summary>
    internal sealed class PcgStampOverlaySpec
    {
        public string MaskNodeId;
        public string TransformNodeId;
        public string BoxNodeId;
        public string Title;
        public Vector3 LocalPosition;
        public Vector3 LocalEuler;
        public Vector3 LocalScale = Vector3.one;
        public Vector3 BoxSize = new(2f, 2f, 2f);
        public bool HasTransform;
        public bool HasBox;
    }

    /// <summary>
    /// Collects stamp volumes from HeightFieldMaskByObject ← TransformMesh ← CreateBoxMesh chains.
    /// </summary>
    internal static class PcgStampOverlayCollector
    {
        public static List<PcgStampOverlaySpec> Collect(PcgGraphDocument doc)
        {
            var specs = new List<PcgStampOverlaySpec>();
            if (doc?.nodes == null || doc.edges == null)
                return specs;

            var nodesById = new Dictionary<string, PcgGraphNodeRecord>();
            foreach (var node in doc.nodes)
            {
                if (node != null && !string.IsNullOrEmpty(node.id))
                    nodesById[node.id] = node;
            }

            foreach (var node in doc.nodes)
            {
                if (node == null || node.type != "HeightFieldMaskByObject")
                    continue;

                var geometrySourceId = FindUpstream(doc, node.id, "geometry");
                if (string.IsNullOrEmpty(geometrySourceId) ||
                    !nodesById.TryGetValue(geometrySourceId, out var geometryNode))
                    continue;

                var spec = new PcgStampOverlaySpec
                {
                    MaskNodeId = node.id,
                    Title = ReadTitle(node, "Stamp Mask"),
                };

                var cursor = geometryNode;
                if (cursor.type == "TransformMesh")
                {
                    spec.HasTransform = true;
                    spec.TransformNodeId = cursor.id;
                    spec.LocalPosition = PcgVector3Property.ResolveFromNodeData(
                        cursor.data, "translate", Vector3.zero);
                    spec.LocalEuler = PcgVector3Property.ResolveFromNodeData(
                        cursor.data, "rotation", Vector3.zero);
                    spec.LocalScale = PcgVector3Property.ResolveFromNodeData(
                        cursor.data, "scale", Vector3.one);
                    spec.Title = ReadTitle(cursor, spec.Title);

                    var boxId = FindUpstream(doc, cursor.id, "in");
                    if (!string.IsNullOrEmpty(boxId) &&
                        nodesById.TryGetValue(boxId, out var boxNode) &&
                        boxNode.type == "CreateBoxMesh")
                    {
                        FillBox(spec, boxNode);
                    }
                }
                else if (cursor.type == "CreateBoxMesh")
                {
                    FillBox(spec, cursor);
                }
                else
                {
                    // Non-box geometry: draw a unit proxy at origin so the chain is visible.
                    spec.BoxSize = new Vector3(4f, 4f, 4f);
                    spec.Title = ReadTitle(cursor, "Stamp Geometry");
                }

                specs.Add(spec);
            }

            return specs;
        }

        public static Matrix4x4 LocalMatrix(PcgStampOverlaySpec spec)
        {
            if (spec == null)
                return Matrix4x4.identity;

            var rotation = Quaternion.Euler(spec.LocalEuler);
            var scale = spec.LocalScale;
            if (scale.x < 0.0001f) scale.x = 0.0001f;
            if (scale.y < 0.0001f) scale.y = 0.0001f;
            if (scale.z < 0.0001f) scale.z = 0.0001f;
            return Matrix4x4.TRS(spec.LocalPosition, rotation, scale);
        }

        private static void FillBox(PcgStampOverlaySpec spec, PcgGraphNodeRecord boxNode)
        {
            spec.HasBox = true;
            spec.BoxNodeId = boxNode.id;
            spec.BoxSize = new Vector3(
                Mathf.Max(0.001f, ReadFloat(boxNode.data, "width", 2f)),
                Mathf.Max(0.001f, ReadFloat(boxNode.data, "height", 2f)),
                Mathf.Max(0.001f, ReadFloat(boxNode.data, "depth", 2f)));
            if (!spec.HasTransform)
                spec.Title = ReadTitle(boxNode, "Stamp Box");
        }

        private static string FindUpstream(PcgGraphDocument doc, string targetId, string targetHandle)
        {
            foreach (var edge in doc.edges)
            {
                if (edge == null)
                    continue;
                if (edge.target == targetId &&
                    string.Equals(edge.targetHandle ?? "in", targetHandle, System.StringComparison.Ordinal))
                    return edge.source;
            }

            return null;
        }

        private static string ReadTitle(PcgGraphNodeRecord node, string fallback)
        {
            var title = node?.data?.GetRaw("__nodeTitle")?.ToString();
            return string.IsNullOrWhiteSpace(title) ? fallback : title;
        }

        public static float ReadFloat(PcgNodeData data, string key, float fallback)
        {
            if (data == null)
                return fallback;

            var raw = data.GetRaw(key);
            if (raw == null)
                return fallback;
            if (raw is float f)
                return f;
            if (raw is double d)
                return (float)d;
            if (raw is int i)
                return i;
            if (float.TryParse(
                    raw.ToString(),
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsed))
                return parsed;
            return fallback;
        }
    }
}
#endif
