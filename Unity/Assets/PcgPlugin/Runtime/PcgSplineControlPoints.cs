using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>Parse / serialize CreateSpline <c>controlPoints</c> JSON arrays.</summary>
    public static class PcgSplineControlPoints
    {
        public static List<Vector3> Parse(string raw)
        {
            var points = new List<Vector3>();
            if (string.IsNullOrWhiteSpace(raw))
                return points;

            try
            {
                if (PcgMiniJson.Deserialize(raw) is not List<object> list)
                    return points;

                foreach (var item in list)
                {
                    if (item is not Dictionary<string, object> dict)
                        continue;

                    points.Add(new Vector3(
                        ReadComponent(dict, "x"),
                        ReadComponent(dict, "y"),
                        ReadComponent(dict, "z")));
                }
            }
            catch
            {
                // ignore malformed JSON
            }

            return points;
        }

        public static string Serialize(IReadOnlyList<Vector3> points)
        {
            if (points == null || points.Count == 0)
                return "[]";

            var sb = new StringBuilder(points.Count * 32);
            sb.Append('[');
            for (var i = 0; i < points.Count; i++)
            {
                if (i > 0)
                    sb.Append(',');

                var p = points[i];
                sb.Append(string.Format(
                    CultureInfo.InvariantCulture,
                    "{{\"x\":{0},\"y\":{1},\"z\":{2}}}",
                    p.x,
                    p.y,
                    p.z));
            }

            sb.Append(']');
            return sb.ToString();
        }

        public static bool HasExplicitControlPoints(PcgNodeData data)
        {
            return Parse(data?.GetRaw("controlPoints")?.ToString()).Count > 0;
        }

        public static List<Vector3> GetEffectivePoints(PcgNodeData data)
        {
            var points = Parse(data?.GetRaw("controlPoints")?.ToString());
            if (points.Count > 0)
                return points;

            return new List<Vector3>
            {
                ReadVector(data, "startX", "startY", "startZ"),
                ReadVector(data, "endX", "endY", "endZ"),
            };
        }

        public static void WriteControlPoints(PcgNodeData data, IReadOnlyList<Vector3> points)
        {
            if (data == null)
                return;

            data.SetRaw("controlPoints", Serialize(points));
        }

        // --- Tangents ---

        public static List<Vector3> ParseTangents(string raw)
        {
            return Parse(raw);
        }

        public static string SerializeTangents(IReadOnlyList<Vector3> tangents)
        {
            return Serialize(tangents);
        }

        public static bool HasExplicitTangents(PcgNodeData data)
        {
            return ParseTangents(data?.GetRaw("tangents")?.ToString()).Count > 0;
        }

        /// <summary>
        /// Returns explicit tangents if stored, otherwise computes Catmull-Rom tangents
        /// from the control points. Each tangent is the direction the curve passes
        /// through that point (not normalised).
        /// </summary>
        public static List<Vector3> GetTangents(
            IReadOnlyList<Vector3> controlPoints,
            PcgNodeData data,
            bool closed)
        {
            var stored = ParseTangents(data?.GetRaw("tangents")?.ToString());
            if (stored.Count == controlPoints.Count)
                return stored;

            return ComputeCatmullRomTangents(controlPoints, closed);
        }

        /// <summary>Standard Catmull-Rom tangent: 0.5 * (next - prev).</summary>
        public static List<Vector3> ComputeCatmullRomTangents(
            IReadOnlyList<Vector3> points,
            bool closed)
        {
            var tangents = new List<Vector3>(points.Count);
            var count = points.Count;
            if (count == 0)
                return tangents;

            for (var i = 0; i < count; i++)
            {
                var prev = closed
                    ? points[(i - 1 + count) % count]
                    : points[Mathf.Max(0, i - 1)];
                var next = closed
                    ? points[(i + 1) % count]
                    : points[Mathf.Min(count - 1, i + 1)];
                tangents.Add((next - prev) * 0.5f);
            }

            return tangents;
        }

        public static void WriteStartEnd(PcgNodeData data, Vector3 start, Vector3 end)
        {
            if (data == null)
                return;

            data.SetRaw("startX", start.x);
            data.SetRaw("startY", start.y);
            data.SetRaw("startZ", start.z);
            data.SetRaw("endX", end.x);
            data.SetRaw("endY", end.y);
            data.SetRaw("endZ", end.z);
        }

        private static Vector3 ReadVector(PcgNodeData data, string xKey, string yKey, string zKey)
        {
            return new Vector3(
                ReadFloat(data, xKey, 0f),
                ReadFloat(data, yKey, 0f),
                ReadFloat(data, zKey, 0f));
        }

        private static float ReadFloat(PcgNodeData data, string key, float defaultValue)
        {
            if (data == null)
                return defaultValue;

            var raw = data.GetRaw(key);
            if (raw == null)
                return defaultValue;

            return raw switch
            {
                float f => f,
                double d => (float)d,
                int i => i,
                long l => l,
                _ => float.TryParse(
                    raw.ToString(),
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsed)
                    ? parsed
                    : defaultValue,
            };
        }

        private static float ReadComponent(Dictionary<string, object> dict, string key)
        {
            if (!dict.TryGetValue(key, out var raw) || raw == null)
                return 0f;

            return raw switch
            {
                float f => f,
                double d => (float)d,
                int i => i,
                long l => l,
                _ => float.TryParse(
                    raw.ToString(),
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsed)
                    ? parsed
                    : 0f,
            };
        }
    }
}
