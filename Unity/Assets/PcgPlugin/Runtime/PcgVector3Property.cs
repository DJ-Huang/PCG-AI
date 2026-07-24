using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>Canonical [x,y,z] storage for manifest <c>vector3</c> node properties.</summary>
    public static class PcgVector3Property
    {
        public static string Format(float x, float y, float z) =>
            $"[{x.ToString(CultureInfo.InvariantCulture)},{y.ToString(CultureInfo.InvariantCulture)},{z.ToString(CultureInfo.InvariantCulture)}]";

        public static string Format(Vector3 value) => Format(value.x, value.y, value.z);

        public static string Format(IList<object> values)
        {
            var x = values.Count > 0 ? ToFloat(values[0]) : 0f;
            var y = values.Count > 1 ? ToFloat(values[1]) : 0f;
            var z = values.Count > 2 ? ToFloat(values[2]) : 0f;
            return Format(x, y, z);
        }

        public static string NormalizeStored(object value)
        {
            if (value == null)
                return Format(0f, 0f, 0f);
            if (value is string text && TryParse(text, out var parsed))
                return Format(parsed);
            if (value is IList<object> list)
                return Format(list);
            if (value is Vector3 vector)
                return Format(vector);
            if (value is float[] floats && floats.Length >= 3)
                return Format(floats[0], floats[1], floats[2]);
            return Format(0f, 0f, 0f);
        }

        public static bool TryParse(object raw, out Vector3 value)
        {
            value = Vector3.zero;
            if (raw == null)
                return false;
            if (raw is Vector3 vector)
            {
                value = vector;
                return true;
            }
            if (raw is IList<object> list && list.Count >= 3)
            {
                value = new Vector3(ToFloat(list[0]), ToFloat(list[1]), ToFloat(list[2]));
                return true;
            }
            return TryParse(raw.ToString(), out value);
        }

        public static bool TryParse(string text, out Vector3 value)
        {
            value = Vector3.zero;
            if (string.IsNullOrWhiteSpace(text))
                return false;

            text = text.Trim();
            if (!text.StartsWith("[", StringComparison.Ordinal) || !text.EndsWith("]", StringComparison.Ordinal))
                return false;

            var parts = text.Substring(1, text.Length - 2).Split(',');
            if (parts.Length < 3)
                return false;

            if (!float.TryParse(parts[0].Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var x) ||
                !float.TryParse(parts[1].Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var y) ||
                !float.TryParse(parts[2].Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var z))
                return false;

            value = new Vector3(x, y, z);
            return true;
        }

        public static Vector3 ParseOrDefault(object raw, Vector3 fallback)
        {
            return TryParse(raw, out var value) ? value : fallback;
        }

        public static void AppendJson(StringBuilder sb, object raw)
        {
            var value = ParseOrDefault(raw, Vector3.zero);
            sb.Append('[')
                .Append(value.x.ToString(CultureInfo.InvariantCulture))
                .Append(',')
                .Append(value.y.ToString(CultureInfo.InvariantCulture))
                .Append(',')
                .Append(value.z.ToString(CultureInfo.InvariantCulture))
                .Append(']');
        }

        private static float ToFloat(object value) =>
            float.TryParse(value?.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)
                ? parsed
                : 0f;
    }
}
