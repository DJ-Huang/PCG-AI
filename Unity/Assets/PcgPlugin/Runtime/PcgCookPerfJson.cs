using System;
using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    /// <summary>Minimal JSON array parser for native per-node perf payloads.</summary>
    internal static class PcgCookPerfJson
    {
        public static PcgNodePerfEntry[] TryParse(string json)
        {
            if (string.IsNullOrWhiteSpace(json) || json.Length < 2 || json[0] != '[')
                return Array.Empty<PcgNodePerfEntry>();

            var entries = new List<PcgNodePerfEntry>();
            var i = 1;
            while (i < json.Length)
            {
                while (i < json.Length && (json[i] == ' ' || json[i] == ','))
                    i++;
                if (i >= json.Length || json[i] == ']')
                    break;
                if (json[i] != '{')
                    break;

                var end = FindObjectEnd(json, i);
                if (end < 0)
                    break;

                if (TryParseObject(json, i, end, out var entry))
                    entries.Add(entry);
                i = end + 1;
            }

            return entries.ToArray();
        }

        private static int FindObjectEnd(string json, int start)
        {
            var depth = 0;
            for (var i = start; i < json.Length; i++)
            {
                if (json[i] == '{')
                    depth++;
                else if (json[i] == '}')
                {
                    depth--;
                    if (depth == 0)
                        return i;
                }
            }

            return -1;
        }

        private static bool TryParseObject(string json, int start, int end, out PcgNodePerfEntry entry)
        {
            entry = default;
            var slice = json.Substring(start, end - start + 1);
            entry.Id = ReadString(slice, "\"id\"");
            entry.Type = ReadString(slice, "\"type\"");
            entry.Ms = ReadDouble(slice, "\"ms\"");
            entry.Cached = ReadBool(slice, "\"cached\"");
            return !string.IsNullOrEmpty(entry.Id);
        }

        private static string ReadString(string obj, string key)
        {
            var keyIndex = obj.IndexOf(key, StringComparison.Ordinal);
            if (keyIndex < 0)
                return string.Empty;

            var colon = obj.IndexOf(':', keyIndex + key.Length);
            if (colon < 0)
                return string.Empty;

            var quoteStart = obj.IndexOf('"', colon + 1);
            if (quoteStart < 0)
                return string.Empty;

            var quoteEnd = obj.IndexOf('"', quoteStart + 1);
            if (quoteEnd < 0)
                return string.Empty;

            return obj.Substring(quoteStart + 1, quoteEnd - quoteStart - 1);
        }

        private static double ReadDouble(string obj, string key)
        {
            var keyIndex = obj.IndexOf(key, StringComparison.Ordinal);
            if (keyIndex < 0)
                return 0.0;

            var colon = obj.IndexOf(':', keyIndex + key.Length);
            if (colon < 0)
                return 0.0;

            var valueStart = colon + 1;
            while (valueStart < obj.Length && char.IsWhiteSpace(obj[valueStart]))
                valueStart++;

            var valueEnd = valueStart;
            while (valueEnd < obj.Length && "0123456789.-eE+".IndexOf(obj[valueEnd]) >= 0)
                valueEnd++;

            if (valueEnd <= valueStart)
                return 0.0;

            return double.TryParse(
                obj.Substring(valueStart, valueEnd - valueStart),
                System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture,
                out var value)
                ? value
                : 0.0;
        }

        private static bool ReadBool(string obj, string key)
        {
            var keyIndex = obj.IndexOf(key, StringComparison.Ordinal);
            if (keyIndex < 0)
                return false;

            return obj.IndexOf("true", keyIndex, StringComparison.Ordinal) >= 0;
        }
    }
}
