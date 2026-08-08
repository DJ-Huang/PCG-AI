using System;
using System.Linq;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Meshy mesh-op / retexture Generate support: cooks the node's connected `in`
    /// upstream once (synchronous, explicit) and packs the polygon result as a GLB
    /// data URI for upload. Runs on the main thread during action Prepare; the
    /// background work delegate only does HTTP with the captured bytes.
    /// </summary>
    internal static class PcgMeshyUpstreamExport
    {
        public static bool TryCookInputToGlbDataUri(
            PcgGraphEditorWindow window,
            PcgManifestNodeView node,
            out string dataUri,
            out string error)
        {
            dataUri = null;
            error = null;

            if (window == null || node == null)
            {
                error = "The graph editor window is not available.";
                return false;
            }

            var liveDocument = window.ExportLiveDocument();
            if (liveDocument == null)
            {
                error = "The graph editor has no live document.";
                return false;
            }

            var scopeId = window.GraphView.CurrentSubgraphId;
            var scopeDocument = GetScopeDocument(liveDocument, scopeId);
            var incoming = scopeDocument?.edges?.FirstOrDefault(
                edge => edge.target == node.NodeId &&
                        (string.IsNullOrEmpty(edge.targetHandle) || edge.targetHandle == "in"));
            if (incoming == null || string.IsNullOrEmpty(incoming.source))
            {
                error = $"{node.NodeType} '{node.NodeId}' requires a connected mesh input.";
                return false;
            }

            // PcgCore owns a process-global cook cache/cancel flag. Finish any preview
            // worker before starting this explicit synchronous cook.
            PcgGraphComponent.CancelAllEditModeAsyncCooks();

            var component = FindSceneComponent(window.CurrentAssetPath);
            component?.ApplyOverridesToDocument(liveDocument);

            if (!PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDocument,
                    incoming.source,
                    scopeId,
                    window.GraphView.GetSubgraphInstanceChain(),
                    out var cookDocument,
                    out var buildError))
            {
                error = $"Failed to build upstream cook: {buildError}";
                return false;
            }

            if (!PcgExecutionDocumentBuilder.TryBuildJson(
                    cookDocument,
                    window.CreateExternalSubgraphLoader(),
                    out var json,
                    out _,
                    out var flattenError,
                    pretty: false))
            {
                error = $"Failed to flatten upstream cook: {flattenError}";
                return false;
            }

            if (!PcgThirdPartyResolvers.TryPrepareAll(ref json, out var prepareError))
            {
                error = prepareError;
                return false;
            }

            var textures = PcgTextureResolver.CollectFromGraphJson(json);
            var meshes = PcgMeshResolver.CollectFromGraphJson(
                json,
                component != null ? component.gameObject : null,
                component?.MeshBindings,
                window.PreviewMeshBindings);
            var splines = PcgSplineResolver.CollectFromGraphJson(
                json,
                component != null ? component.gameObject : null,
                component?.SplineBindings,
                component != null
                    ? PcgGraphComponent.EditorResolvePreviewSplineBindings?.Invoke(component)
                    : null);

            var (code, result) = PcgNative.ExecuteGraph(json, ReadSeed(component), textures, meshes, splines);
            if (code != PcgResultCode.Ok || result == null)
            {
                error = result?.Error ?? $"PCG upstream cook failed with code {code}.";
                return false;
            }
            if (result.GeometryBinary == null || result.GeometryBinary.Length == 0)
            {
                error = "The connected input did not produce polygon geometry.";
                return false;
            }

            var glb = PcgGlbWriter.WriteFromGeometryBinary(result.GeometryBinary, out var glbError);
            if (glb == null)
            {
                error = $"Failed to encode upstream geometry as GLB: {glbError}";
                return false;
            }

            dataUri = "data:application/octet-stream;base64," + Convert.ToBase64String(glb);
            return true;
        }

        private static PcgGraphDocument GetScopeDocument(PcgGraphDocument document, string scopeId)
        {
            if (string.IsNullOrEmpty(scopeId))
                return document;
            var subgraph = document.subgraphs?.FirstOrDefault(item => item.id == scopeId);
            if (subgraph == null)
                return null;
            return new PcgGraphDocument { nodes = subgraph.nodes, edges = subgraph.edges };
        }

        private static PcgGraphComponent FindSceneComponent(string graphAssetPath)
        {
            if (string.IsNullOrEmpty(graphAssetPath))
                return null;
            var asset = AssetDatabase.LoadAssetAtPath<PcgGraphAsset>(graphAssetPath);
            if (asset == null)
                return null;
            return UnityEngine.Object.FindObjectsByType<PcgGraphComponent>(
                    FindObjectsInactive.Include, FindObjectsSortMode.None)
                .FirstOrDefault(component => component != null && component.GraphAsset == asset);
        }

        private static int ReadSeed(PcgGraphComponent component)
        {
            if (component == null)
                return 42;
            var serialized = new SerializedObject(component);
            return serialized.FindProperty("seed")?.intValue ?? 42;
        }
    }
}
