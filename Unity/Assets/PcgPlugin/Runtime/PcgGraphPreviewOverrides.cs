using System;
using System.Globalization;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Applies preview-quality caps before cook (Subdivide levels, scatter count, bevel segments).
    /// </summary>
    public static class PcgGraphPreviewOverrides
    {
        public const int MaxPreviewSubdivideLevels = 1;
        public const int MaxPreviewScatterCount = 128;
        public const int MaxPreviewBevelSegments = 1;

        public static void Apply(PcgGraphDocument doc, PcgPreviewQuality quality)
        {
            if (doc?.nodes == null || quality == PcgPreviewQuality.Full)
                return;

            foreach (var node in doc.nodes)
            {
                if (node?.data == null || string.IsNullOrEmpty(node.type))
                    continue;

                switch (node.type)
                {
                    case "SubdivideMesh":
                        CapInt(node, "levels", MaxPreviewSubdivideLevels);
                        break;
                    case "SampleMeshSurface":
                        CapInt(node, "count", MaxPreviewScatterCount);
                        break;
                    case "BevelMesh":
                        CapInt(node, "segments", MaxPreviewBevelSegments);
                        break;
                }
            }
        }

        private static void CapInt(PcgGraphNodeRecord node, string key, int max)
        {
            var raw = node.data.GetRaw(key);
            if (!TryParseInt(raw, out var value))
                return;

            if (value > max)
                node.data.SetRaw(key, max);
        }

        private static bool TryParseInt(object raw, out int value)
        {
            value = 0;
            switch (raw)
            {
                case int i:
                    value = i;
                    return true;
                case long l:
                    value = (int)l;
                    return true;
                case float f:
                    value = (int)f;
                    return true;
                case double d:
                    value = (int)d;
                    return true;
                case string s when int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed):
                    value = parsed;
                    return true;
                default:
                    return false;
            }
        }
    }
}
