using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    public static class PcgMeshBindingTable
    {
        public static Mesh ResolveMesh(
            string bindingKey,
            PcgMeshBindingSource source,
            string meshAssetPath,
            IReadOnlyList<PcgMeshBinding> componentBindings,
            IReadOnlyList<PcgPreviewMeshBinding> previewBindings,
            GameObject componentHost)
        {
            if (!string.IsNullOrEmpty(meshAssetPath))
            {
                var fromPath = LoadMeshAsset(meshAssetPath);
                if (fromPath != null)
                    return fromPath;
            }

            if (componentBindings != null)
            {
                foreach (var binding in componentBindings)
                {
                    if (binding == null || binding.bindingKey != bindingKey)
                        continue;

                    var mesh = ResolveComponentBinding(binding, componentHost);
                    if (mesh != null)
                        return mesh;
                }
            }

            if (previewBindings != null)
            {
                foreach (var preview in previewBindings)
                {
                    if (preview == null || preview.bindingKey != bindingKey)
                        continue;

                    var filter = preview.previewMeshFilter;
                    if (filter != null && filter.sharedMesh != null)
                        return filter.sharedMesh;
                }
            }

            return ResolveBySource(source, componentHost);
        }

        private static Mesh ResolveComponentBinding(PcgMeshBinding binding, GameObject host)
        {
            switch (binding.source)
            {
                case PcgMeshBindingSource.Self:
                    return host != null ? host.GetComponent<MeshFilter>()?.sharedMesh : null;
                case PcgMeshBindingSource.SceneObject:
                    return binding.sceneObject != null
                        ? binding.sceneObject.GetComponent<MeshFilter>()?.sharedMesh
                        : null;
                case PcgMeshBindingSource.Asset:
                    return binding.meshAsset;
                default:
                    return null;
            }
        }

        private static Mesh ResolveBySource(PcgMeshBindingSource source, GameObject host)
        {
            if (source == PcgMeshBindingSource.Self && host != null)
                return host.GetComponent<MeshFilter>()?.sharedMesh;
            return null;
        }

#if UNITY_EDITOR
        private static Mesh LoadMeshAsset(string stored)
        {
            if (string.IsNullOrWhiteSpace(stored))
                return null;

            var normalized = stored.Replace('\\', '/');
            var mesh = UnityEditor.AssetDatabase.LoadAssetAtPath<Mesh>(normalized);
            if (mesh != null)
                return mesh;

            var go = UnityEditor.AssetDatabase.LoadAssetAtPath<GameObject>(normalized);
            return go != null ? go.GetComponentInChildren<MeshFilter>()?.sharedMesh : null;
        }
#else
        private static Mesh LoadMeshAsset(string stored) => null;
#endif
    }
}
