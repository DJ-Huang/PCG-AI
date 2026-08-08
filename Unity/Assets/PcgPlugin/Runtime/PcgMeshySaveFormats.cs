using System;
using System.Collections.Generic;
using DJTechRuntime.PCG;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Meshy Generate save-format selection (persisted on the node as saveGlb / saveFbx).
    /// Meshy API can emit these via target_formats / model_urls.
    /// </summary>
    public static class PcgMeshySaveFormats
    {
        public const string Glb = "glb";
        public const string Fbx = "fbx";

        public static readonly string[] Supported = { Glb, Fbx };

        public static List<string> ReadSelected(PcgNodeData data)
        {
            var selected = new List<string>(2);
            if (ReadBool(data, "saveGlb", true))
                selected.Add(Glb);
            if (ReadBool(data, "saveFbx", false))
                selected.Add(Fbx);
            if (selected.Count == 0)
                selected.Add(Glb);
            return selected;
        }

        /// <summary>Preferred cook/save primary: GLB when present, else first selected.</summary>
        public static string Primary(IReadOnlyList<string> formats)
        {
            if (formats == null || formats.Count == 0)
                return Glb;
            for (var i = 0; i < formats.Count; i++)
            {
                if (string.Equals(formats[i], Glb, StringComparison.OrdinalIgnoreCase))
                    return Glb;
            }
            return formats[0];
        }

        public static bool IsSelected(PcgNodeData data, string format) =>
            ReadSelected(data).Exists(f =>
                string.Equals(f, format, StringComparison.OrdinalIgnoreCase));

        public static void SetSelected(PcgNodeData data, string format, bool enabled)
        {
            data ??= new PcgNodeData();
            if (string.Equals(format, Glb, StringComparison.OrdinalIgnoreCase))
                data.SetRaw("saveGlb", enabled);
            else if (string.Equals(format, Fbx, StringComparison.OrdinalIgnoreCase))
                data.SetRaw("saveFbx", enabled);
        }

        private static bool ReadBool(PcgNodeData data, string key, bool fallback)
        {
            var raw = data?.GetRaw(key);
            if (raw == null)
                return fallback;
            if (raw is bool b)
                return b;
            return bool.TryParse(raw.ToString(), out var parsed) ? parsed : fallback;
        }
    }
}
