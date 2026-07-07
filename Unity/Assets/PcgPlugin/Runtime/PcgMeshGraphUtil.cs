using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    public static class PcgMeshGraphUtil
    {
        public static bool TryValidateMeshRequirements(
            string json,
            IReadOnlyList<PcgMeshUpload> uploads,
            out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(json))
                return true;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                return true;

            var getMeshNodes = doc.nodes.Where(n => n.type == "GetMeshData").ToList();
            if (getMeshNodes.Count == 0)
                return true;

            var uploaded = new HashSet<string>();
            if (uploads != null)
            {
                foreach (var upload in uploads)
                {
                    if (!string.IsNullOrEmpty(upload?.SlotId))
                        uploaded.Add(upload.SlotId);
                }
            }

            var sb = new System.Text.StringBuilder();
            foreach (var node in getMeshNodes)
            {
                if (uploaded.Contains(node.id))
                    continue;

                var source = node.data?.GetRaw("source")?.ToString() ?? "Binding";
                var bindingKey = node.data?.GetRaw("bindingKey")?.ToString() ?? "";
                var meshAsset = node.data?.GetRaw("meshAsset")?.ToString() ?? "";
                sb.AppendLine(
                    $"- GetMeshData '{node.id}' (source={source}, bindingKey='{bindingKey}', meshAsset='{meshAsset}'): mesh slot not uploaded.");
            }

            if (sb.Length == 0)
                return true;

            error = "Graph contains GetMeshData nodes without runtime mesh slots:\n" + sb;
            return false;
        }
    }
}
