#if UNITY_EDITOR
using DJTechRuntime.PCG;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Resolves MatchSize target bounds in graph-local space (mirrors pcg-core
    /// <c>match_size_geometry</c> target-bounds selection).
    /// </summary>
    internal static class PcgMatchSizeBoundsUtility
    {
        internal struct MinMaxBounds
        {
            public bool Valid;
            public Vector3 Min;
            public Vector3 Max;

            public Vector3 Center => (Min + Max) * 0.5f;
            public Vector3 Size => Max - Min;

            public bool IsDegenerate =>
                Size.sqrMagnitude <= 1e-8f;
        }

        public static bool TryGetTargetBounds(
            PcgGraphDocument doc,
            PcgGraphNodeRecord node,
            PcgGraphComponent component,
            string previewNodeId,
            out MinMaxBounds bounds)
        {
            bounds = default;
            if (doc == null || node == null || node.type != "MatchSize")
                return false;

            var data = node.data;
            var justifyWith = ReadString(data, "justifyWith", "inputIfWired");
            var referenceSourceId = FindReferenceSourceNodeId(doc, node.id);
            var referenceWired = !string.IsNullOrEmpty(referenceSourceId);

            var useReference = justifyWith switch
            {
                "secondInput" => true,
                "inputIfWired" => referenceWired,
                "originAndUnitSize" => false,
                _ => false, // locationAndSize
            };

            if (justifyWith == "originAndUnitSize")
            {
                bounds = BoundsFromPositionSize(
                    Vector3.zero,
                    Vector3.one,
                    new[] { "center", "center", "center" });
                return bounds.Valid;
            }

            if (useReference)
            {
                if (TryBoundsFromPreview(component, previewNodeId, referenceSourceId, out bounds))
                    return true;

                if (!referenceWired)
                {
                    bounds = new MinMaxBounds
                    {
                        Valid = true,
                        Min = Vector3.zero,
                        Max = Vector3.zero,
                    };
                    return true;
                }

                return false;
            }

            var targetJustify = new[]
            {
                ResolveTargetJustify(ReadString(data, "justifyX", "center"), ReadString(data, "targetJustifyX", "same")),
                ResolveTargetJustify(ReadString(data, "justifyY", "center"), ReadString(data, "targetJustifyY", "same")),
                ResolveTargetJustify(ReadString(data, "justifyZ", "center"), ReadString(data, "targetJustifyZ", "same")),
            };

            var targetPosition = ReadVector(data, "targetPosition", Vector3.zero);
            var targetSize = ReadVector(data, "targetSize", Vector3.one);
            if (targetSize.x < 0f || targetSize.y < 0f || targetSize.z < 0f)
                return false;

            bounds = BoundsFromPositionSize(targetPosition, targetSize, targetJustify);
            return bounds.Valid;
        }

        private static bool TryBoundsFromPreview(
            PcgGraphComponent component,
            string previewNodeId,
            string referenceSourceId,
            out MinMaxBounds bounds)
        {
            bounds = default;
            if (component == null || string.IsNullOrEmpty(referenceSourceId))
                return false;

            // Target bounds come from the reference source geometry, never from the
            // MatchSize cooked output (that reflects the result, not the target volume).
            if (previewNodeId != referenceSourceId)
                return false;

            var preview = component.PolygonPreview;
            if (preview?.Points == null || preview.Points.Length == 0)
                return false;

            return TryBoundsFromPoints(preview.Points, out bounds);
        }

        public static bool TryBoundsFromPoints(Vector3[] points, out MinMaxBounds bounds)
        {
            bounds = default;
            if (points == null || points.Length == 0)
                return false;

            var min = points[0];
            var max = points[0];
            for (var i = 1; i < points.Length; i++)
            {
                var p = points[i];
                min = Vector3.Min(min, p);
                max = Vector3.Max(max, p);
            }

            bounds = new MinMaxBounds
            {
                Valid = true,
                Min = min,
                Max = max,
            };
            return true;
        }

        public static MinMaxBounds BoundsFromPositionSize(
            Vector3 position,
            Vector3 size,
            string[] targetJustify)
        {
            var bounds = new MinMaxBounds { Valid = true };
            for (var axis = 0; axis < 3; axis++)
            {
                var mode = targetJustify[axis] == "none" ? "center" : targetJustify[axis];
                var pos = axis == 0 ? position.x : axis == 1 ? position.y : position.z;
                var extent = axis == 0 ? size.x : axis == 1 ? size.y : size.z;

                float minimum;
                float maximum;
                if (mode == "min")
                {
                    minimum = pos;
                    maximum = pos + extent;
                }
                else if (mode == "max")
                {
                    maximum = pos;
                    minimum = pos - extent;
                }
                else
                {
                    minimum = pos - extent * 0.5f;
                    maximum = pos + extent * 0.5f;
                }

                if (axis == 0)
                {
                    bounds.Min.x = minimum;
                    bounds.Max.x = maximum;
                }
                else if (axis == 1)
                {
                    bounds.Min.y = minimum;
                    bounds.Max.y = maximum;
                }
                else
                {
                    bounds.Min.z = minimum;
                    bounds.Max.z = maximum;
                }
            }

            return bounds;
        }

        private static string ResolveTargetJustify(string sourceJustify, string targetJustify)
        {
            if (targetJustify != "same")
                return targetJustify;
            return sourceJustify == "none" ? "center" : sourceJustify;
        }

        private static string FindReferenceSourceNodeId(PcgGraphDocument doc, string matchSizeNodeId)
        {
            if (doc?.edges == null)
                return null;

            foreach (var edge in doc.edges)
            {
                if (edge == null || edge.target != matchSizeNodeId)
                    continue;
                if (edge.targetHandle == "reference")
                    return edge.source;
            }

            return null;
        }

        private static string ReadString(PcgNodeData data, string key, string fallback)
        {
            if (data == null)
                return fallback;

            var raw = data.GetRaw(key);
            return raw == null ? fallback : raw.ToString();
        }

        private static Vector3 ReadVector(PcgNodeData data, string key, Vector3 fallback)
        {
            if (data == null)
                return fallback;

            var raw = data.GetRaw(key);
            return PcgVector3Property.TryParse(raw, out var value) ? value : fallback;
        }
    }
}
#endif
