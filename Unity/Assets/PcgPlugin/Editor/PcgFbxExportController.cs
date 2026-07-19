using System;
using System.IO;
using System.Linq;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Explicit Editor-only ROP execution for ExportFBX nodes. Normal graph cooks never
    /// call this class and therefore never write files.
    /// </summary>
    internal static class PcgFbxExportController
    {
        public static bool Export(PcgGraphEditorWindow window, PcgManifestNodeView exportNode)
        {
            if (window == null || exportNode == null || exportNode.NodeType != "ExportFBX")
                return false;

            var liveDocument = window.ExportLiveDocument();
            if (liveDocument == null)
                return Fail("The graph editor has no live document.");

            var exportRecord = FindNode(
                liveDocument, exportNode.NodeId, window.GraphView.CurrentSubgraphId);
            if (exportRecord == null)
                return Fail($"Export node '{exportNode.NodeId}' was not found.");

            var scopeDocument = GetScopeDocument(liveDocument, window.GraphView.CurrentSubgraphId);
            var incoming = scopeDocument?.edges?.FirstOrDefault(edge => edge.target == exportNode.NodeId);
            if (incoming == null)
                return Fail("ExportFBX requires a connected geometry input.");

            // PcgCore owns a process-global cook cache/cancel flag. Finish any preview
            // worker before starting this explicit synchronous ROP cook.
            PcgGraphComponent.CancelAllEditModeAsyncCooks();

            var component = FindSceneComponent(window.CurrentAssetPath);
            component?.ApplyOverridesToDocument(liveDocument);

            if (!PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDocument,
                    incoming.source,
                    window.GraphView.CurrentSubgraphId,
                    window.GraphView.GetSubgraphInstanceChain(),
                    out var cookDocument,
                    out var buildError))
            {
                return Fail($"Failed to build export cook: {buildError}");
            }

            var json = PcgGraphSerializer.ToJson(cookDocument, pretty: false);
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

            var seed = ReadSeed(component);
            var (code, result) = PcgNative.ExecuteGraph(json, seed, textures, meshes, splines);
            if (code != PcgResultCode.Ok || result == null)
                return Fail(result?.Error ?? $"PCG cook failed with code {code}.");
            if (result.GeometryBinary == null || result.GeometryBinary.Length == 0)
                return Fail("The connected node did not produce polygon geometry.");

            var data = exportRecord.data;
            var rawPath = data?.GetRaw("path")?.ToString() ?? "Exports/pcg_export.fbx";
            var outputPath = ResolveOutputPath(rawPath, window.CurrentAssetPath, exportNode.NodeId);
            var scale = ReadFloat(data?.GetRaw("scale"), 1.0f);
            var generateNormals = ReadBool(data?.GetRaw("generateNormals"), true);

            if (!PcgFbxNative.Export(
                    result.GeometryBinary, outputPath, scale, generateNormals, out var exportError))
            {
                return Fail(exportError);
            }

            AssetDatabase.Refresh();
            Debug.Log(
                $"[PCG] Exported FBX '{outputPath}' " +
                $"({result.GeometryBinary.Length} geometry bytes, {PcgFbxNative.Version}).");
            EditorUtility.RevealInFinder(outputPath);
            return true;
        }

        public static string BrowseForPath(string currentValue)
        {
            var projectRoot = Directory.GetParent(Application.dataPath)?.FullName ?? Application.dataPath;
            var currentAbsolute = ResolveProjectPath(currentValue, projectRoot);
            var directory = Path.GetDirectoryName(currentAbsolute);
            if (string.IsNullOrEmpty(directory) || !Directory.Exists(directory))
                directory = projectRoot;
            var filename = Path.GetFileNameWithoutExtension(currentAbsolute);
            if (string.IsNullOrWhiteSpace(filename))
                filename = "pcg_export";

            var selected = EditorUtility.SaveFilePanel("Export PCG Geometry as FBX", directory, filename, "fbx");
            if (string.IsNullOrEmpty(selected))
                return null;

            selected = Path.GetFullPath(selected);
            var relative = Path.GetRelativePath(projectRoot, selected);
            return relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
                ? selected
                : relative.Replace('\\', '/');
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

        private static PcgGraphNodeRecord FindNode(
            PcgGraphDocument document, string nodeId, string scopeId)
        {
            if (string.IsNullOrEmpty(scopeId))
                return document.nodes.FirstOrDefault(node => node.id == nodeId);
            return document.subgraphs?.FirstOrDefault(item => item.id == scopeId)
                ?.nodes?.FirstOrDefault(node => node.id == nodeId);
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

        private static string ResolveOutputPath(
            string rawPath, string graphAssetPath, string nodeId)
        {
            var projectRoot = Directory.GetParent(Application.dataPath)?.FullName ?? Application.dataPath;
            var graphName = string.IsNullOrEmpty(graphAssetPath)
                ? "pcg_graph"
                : Path.GetFileNameWithoutExtension(graphAssetPath);
            var expanded = (rawPath ?? string.Empty)
                .Replace("$GRAPH", SanitizeFilename(graphName))
                .Replace("$NODE", SanitizeFilename(nodeId));
            if (string.IsNullOrWhiteSpace(expanded))
                expanded = $"Exports/{SanitizeFilename(graphName)}.fbx";
            if (!expanded.EndsWith(".fbx", StringComparison.OrdinalIgnoreCase))
                expanded += ".fbx";
            return ResolveProjectPath(expanded, projectRoot);
        }

        private static string ResolveProjectPath(string path, string projectRoot) =>
            Path.GetFullPath(Path.IsPathRooted(path ?? string.Empty)
                ? path
                : Path.Combine(projectRoot, path ?? string.Empty));

        private static string SanitizeFilename(string value)
        {
            foreach (var invalid in Path.GetInvalidFileNameChars())
                value = value.Replace(invalid, '_');
            return value;
        }

        private static float ReadFloat(object value, float fallback)
        {
            try { return Convert.ToSingle(value); }
            catch { return fallback; }
        }

        private static bool ReadBool(object value, bool fallback)
        {
            if (value is bool boolean)
                return boolean;
            return bool.TryParse(value?.ToString(), out var parsed) ? parsed : fallback;
        }

        private static bool Fail(string message)
        {
            Debug.LogError($"[PCG] FBX export failed: {message}");
            EditorUtility.DisplayDialog("PCG FBX Export", message, "OK");
            return false;
        }
    }
}
