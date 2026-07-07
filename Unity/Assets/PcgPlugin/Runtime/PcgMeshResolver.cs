using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Resolves GetMeshData nodes to vertex/index buffers for pcg_execute_graph_v4.
    /// </summary>
    public static class PcgMeshResolver
    {
#if UNITY_EDITOR
        private static readonly Dictionary<string, PcgMeshUpload> s_UploadCache = new();
#endif

        public static List<PcgMeshUpload> CollectFromGraphJson(
            string json,
            GameObject componentHost,
            IReadOnlyList<PcgMeshBinding> componentBindings,
            IReadOnlyList<PcgPreviewMeshBinding> previewBindings)
        {
            var uploads = new List<PcgMeshUpload>();
            if (string.IsNullOrWhiteSpace(json))
                return uploads;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                return uploads;

            foreach (var node in doc.nodes)
            {
                if (node.type != "GetMeshData" || string.IsNullOrEmpty(node.id))
                    continue;

                var mesh = TryResolveGetMeshData(
                    node.data, componentHost, componentBindings, previewBindings);
                if (mesh == null)
                    continue;

                var upload = BuildUpload(node.id, mesh);
                if (upload != null)
                    uploads.Add(upload);
            }

            return uploads;
        }

        public static Mesh TryResolveGetMeshData(
            PcgNodeData nodeData,
            GameObject componentHost,
            IReadOnlyList<PcgMeshBinding> componentBindings,
            IReadOnlyList<PcgPreviewMeshBinding> previewBindings)
        {
            var sourceText = nodeData?.GetRaw("source")?.ToString() ?? "Binding";
            var bindingKey = nodeData?.GetRaw("bindingKey")?.ToString() ?? "targetMesh";
            var meshAsset = nodeData?.GetRaw("meshAsset")?.ToString() ?? "";

            if (!System.Enum.TryParse<PcgMeshBindingSource>(sourceText, true, out var source))
                source = PcgMeshBindingSource.Binding;

            return PcgMeshBindingTable.ResolveMesh(
                bindingKey,
                source,
                meshAsset,
                componentBindings,
                previewBindings,
                componentHost);
        }

        public static void ClearCache()
        {
#if UNITY_EDITOR
            s_UploadCache.Clear();
#endif
        }

        private static PcgMeshUpload BuildUpload(string slotId, Mesh mesh)
        {
            if (mesh == null || string.IsNullOrEmpty(slotId))
                return null;

#if UNITY_EDITOR
            var cacheKey = slotId + "|" + mesh.GetInstanceID() + "|" + mesh.vertexCount + "|" + mesh.triangles.Length;
            if (s_UploadCache.TryGetValue(cacheKey, out var cached))
                return cached;
#endif

            var vertices = mesh.vertices;
            var triangles = mesh.triangles;
            if (vertices == null || vertices.Length == 0 || triangles == null || triangles.Length < 3)
                return null;

            var positions = new float[vertices.Length * 3];
            for (var i = 0; i < vertices.Length; i++)
            {
                positions[i * 3] = vertices[i].x;
                positions[i * 3 + 1] = vertices[i].y;
                positions[i * 3 + 2] = vertices[i].z;
            }

            var upload = new PcgMeshUpload
            {
                SlotId = slotId,
                Positions = positions,
                Indices = triangles,
                VertexCount = vertices.Length,
                IndexCount = triangles.Length,
            };

#if UNITY_EDITOR
            s_UploadCache[cacheKey] = upload;
#endif
            return upload;
        }
    }
}
