using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Validates ImageTexture / MeshNoiseDeform texture wiring before native execution.
    /// </summary>
    public static class PcgTextureGraphUtil
    {
        public static bool TryValidateTextureRequirements(
            string json,
            IReadOnlyList<PcgTextureUpload> uploads,
            out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(json))
                return true;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                return true;

            var connectedTextureNodeIds = new HashSet<string>();
            foreach (var edge in doc.edges)
            {
                if (edge.targetHandle != "texture")
                    continue;

                var target = doc.nodes.FirstOrDefault(n => n.id == edge.target);
                if (target?.type == "MeshNoiseDeform" && !string.IsNullOrEmpty(edge.source))
                    connectedTextureNodeIds.Add(edge.source);
            }

            if (connectedTextureNodeIds.Count == 0)
                return true;

            var uploadedIds = new HashSet<string>();
            if (uploads != null)
            {
                foreach (var upload in uploads)
                {
                    if (!string.IsNullOrEmpty(upload?.SlotId))
                        uploadedIds.Add(upload.SlotId);
                }
            }

            var sb = new StringBuilder();
            foreach (var nodeId in connectedTextureNodeIds)
            {
                var node = doc.nodes.FirstOrDefault(n => n.id == nodeId);
                if (node == null ||
                    (node.type != "ImageTexture" && node.type != PcgMeshyImageGenResolver.NodeType))
                {
                    sb.AppendLine(
                        $"- Texture input is wired from '{nodeId}', but that node is missing or not an ImageTexture/MeshyImageGen.");
                    continue;
                }

                if (node.type == PcgMeshyImageGenResolver.NodeType)
                {
                    if (!uploadedIds.Contains(nodeId))
                    {
                        sb.AppendLine(
                            $"- MeshyImageGen node '{nodeId}': no saved/cached image pixels. " +
                            "Click Generate on the node, save the image, then cook again.");
                    }
                    continue;
                }

                var stored = node.data?.GetRaw("texture")?.ToString();
                if (string.IsNullOrWhiteSpace(stored))
                {
                    sb.AppendLine(
                        $"- ImageTexture node '{nodeId}': texture field is empty — pick a Texture2D in Graph Inspector.");
                    continue;
                }

                if (!uploadedIds.Contains(nodeId))
                {
                    sb.AppendLine(
                        $"- ImageTexture node '{nodeId}': could not read pixels from '{stored}'. " +
                        "Re-assign the texture in Graph Inspector (built-in textures need container/name, e.g. Resources/unity_builtin_extra/Default-Particle).");
                }
            }

            if (sb.Length == 0)
                return true;

            error =
                "MeshNoiseDeform is connected to a texture, but runtime pixels were not uploaded:\n" +
                sb +
                "After editing in Graph Editor, click Save on the .pcg file before Run on PcgGraphComponent.";
            return false;
        }
    }
}
