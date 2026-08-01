using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Shared authoring → resolve → flatten pipeline used by Editor cook, preview, FBX, and importers.
    /// </summary>
    public static class PcgExecutionDocumentBuilder
    {
        private const string PreviewSinkNodeId = "__pcg_preview_sink__";

        public static bool TryBuild(
            PcgGraphDocument authoring,
            PcgExternalSubgraphLoader loader,
            out PcgGraphDocument flat,
            out PcgExternalResolveResult resolveResult,
            out string error)
        {
            return TryBuild(authoring, loader, out flat, out resolveResult, out error, out _);
        }

        public static bool TryBuild(
            PcgGraphDocument authoring,
            PcgExternalSubgraphLoader loader,
            out PcgGraphDocument flat,
            out PcgExternalResolveResult resolveResult,
            out string error,
            out Dictionary<string, string> outputStatsAliases)
        {
            flat = null;
            resolveResult = null;
            error = null;
            outputStatsAliases = new Dictionary<string, string>();

            if (authoring == null)
            {
                error = "Authoring document is null.";
                return false;
            }

            var working = authoring.Clone();
            if (!working.HasExternalSubgraphAssets())
            {
                if (!PcgGraphFlattener.TryFlattenForExecution(working, out flat, out error, out outputStatsAliases))
                    return false;
                PruneToExecutionSink(flat);
                resolveResult = new PcgExternalResolveResult
                {
                    Document = working,
                    DependencyGuids = new List<string>(),
                };
                return true;
            }

            if (loader == null)
            {
                error = "Graph contains SubgraphAsset nodes but no external loader was provided.";
                return false;
            }

            if (!PcgExternalSubgraphResolver.TryResolveToInline(working, loader, out resolveResult))
            {
                error = resolveResult?.Error ?? "External subgraph resolve failed.";
                if (!string.IsNullOrEmpty(resolveResult?.ErrorChain))
                    error += " | chain: " + resolveResult.ErrorChain;
                return false;
            }

            if (!PcgGraphFlattener.TryFlattenForExecution(resolveResult.Document, out flat, out error, out outputStatsAliases))
                return false;

            PruneToExecutionSink(flat);
            return true;
        }

        public static bool TryBuildJson(
            PcgGraphDocument authoring,
            PcgExternalSubgraphLoader loader,
            out string flatJson,
            out PcgExternalResolveResult resolveResult,
            out string error,
            bool pretty = false)
        {
            return TryBuildJson(
                authoring, loader, out flatJson, out resolveResult, out error, out _, pretty);
        }

        public static bool TryBuildJson(
            PcgGraphDocument authoring,
            PcgExternalSubgraphLoader loader,
            out string flatJson,
            out PcgExternalResolveResult resolveResult,
            out string error,
            out Dictionary<string, string> outputStatsAliases,
            bool pretty = false)
        {
            flatJson = null;
            if (!TryBuild(authoring, loader, out var flat, out resolveResult, out error, out outputStatsAliases))
                return false;
            flatJson = PcgGraphSerializer.ToJson(flat, pretty);
            return true;
        }

        public static bool TryBuildFromJson(
            string authoringJson,
            PcgExternalSubgraphLoader loader,
            out string flatJson,
            out PcgExternalResolveResult resolveResult,
            out string error,
            bool pretty = false)
        {
            flatJson = null;
            resolveResult = null;
            if (!PcgGraphSerializer.TryFromJson(authoringJson, out var doc, out error))
                return false;
            return TryBuildJson(doc, loader, out flatJson, out resolveResult, out error, pretty);
        }

#if UNITY_EDITOR
        public static PcgExternalSubgraphLoader CreateEditorAssetDatabaseLoader()
        {
            return (string assetGuid, out string sourceJson, out string loadError) =>
            {
                sourceJson = null;
                loadError = null;
                var canonical = PcgAssetGuidUtility.Canonicalize(assetGuid);
                var path = UnityEditor.AssetDatabase.GUIDToAssetPath(canonical);
                if (string.IsNullOrEmpty(path))
                {
                    // Unity GUIDToAssetPath expects lowercase 32-hex without dashes.
                    path = UnityEditor.AssetDatabase.GUIDToAssetPath(assetGuid);
                }

                if (string.IsNullOrEmpty(path) || !File.Exists(path))
                {
                    loadError = "Asset path not found for GUID " + canonical;
                    return false;
                }

                if (!path.EndsWith(".pcgsubgraph", StringComparison.OrdinalIgnoreCase))
                {
                    loadError = "Asset is not a .pcgsubgraph: " + path;
                    return false;
                }

                try
                {
                    sourceJson = File.ReadAllText(path);
                    return true;
                }
                catch (Exception ex)
                {
                    loadError = ex.Message;
                    return false;
                }
            };
        }
#endif

        public static bool ContainsExternalOrAuthoringV3(string json, out string reason)
        {
            reason = null;
            if (string.IsNullOrWhiteSpace(json))
                return false;
            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
            {
                // Unknown / parse failure: let callers decide.
                return false;
            }

            if (doc.version == "3.0")
            {
                reason = "Graph version 3.0 requires Editor bake before Player/StreamingAssets execution.";
                return true;
            }

            if (doc.HasExternalSubgraphAssets())
            {
                reason = "Graph contains SubgraphAsset references that must be baked in the Editor.";
                return true;
            }

            return false;
        }

        public static IEnumerable<string> CollectExternalGuids(PcgGraphDocument doc)
        {
            if (doc == null)
                yield break;

            foreach (var guid in CollectExternalGuids(doc.nodes))
                yield return guid;

            if (doc.subgraphs == null)
                yield break;

            foreach (var definition in doc.subgraphs)
            {
                if (definition?.nodes == null)
                    continue;
                foreach (var guid in CollectExternalGuids(definition.nodes))
                    yield return guid;
            }
        }

        private static IEnumerable<string> CollectExternalGuids(List<PcgGraphNodeRecord> nodes)
        {
            if (nodes == null)
                yield break;
            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.SubgraphAsset)
                    continue;
                var guid = PcgAssetGuidUtility.Canonicalize(node.data?.GetRaw("assetGuid")?.ToString());
                if (PcgAssetGuidUtility.IsValid(guid))
                    yield return guid;
            }
        }

        /// <summary>
        /// Keeps only the nodes that contribute to the sink selected by pcg-core.
        /// Disconnected authoring branches are valid work-in-progress and must not
        /// make the active output fail validation or execution.
        /// </summary>
        private static void PruneToExecutionSink(PcgGraphDocument document)
        {
            if (document?.nodes == null || document.nodes.Count == 0)
                return;

            var edges = document.edges ?? new List<PcgGraphEdgeRecord>();
            var nodesWithOutgoing = new HashSet<string>(
                edges
                    .Where(edge => edge != null && !string.IsNullOrEmpty(edge.source))
                    .Select(edge => edge.source),
                StringComparer.Ordinal);
            var sink = document.nodes.FirstOrDefault(node =>
                           node != null &&
                           node.id == PreviewSinkNodeId &&
                           node.type == "Output")
                       ?? document.nodes.FirstOrDefault(node =>
                           node != null &&
                           node.type == "Output" &&
                           !nodesWithOutgoing.Contains(node.id))
                       ?? document.nodes.FirstOrDefault(node =>
                           node != null &&
                           !nodesWithOutgoing.Contains(node.id));
            if (sink == null || string.IsNullOrEmpty(sink.id))
                return;

            var incomingByTarget = edges
                .Where(edge =>
                    edge != null &&
                    !string.IsNullOrEmpty(edge.source) &&
                    !string.IsNullOrEmpty(edge.target))
                .GroupBy(edge => edge.target, StringComparer.Ordinal)
                .ToDictionary(group => group.Key, group => group.ToList(), StringComparer.Ordinal);
            var required = new HashSet<string>(StringComparer.Ordinal) { sink.id };
            var pending = new Stack<string>();
            pending.Push(sink.id);
            while (pending.Count > 0)
            {
                var target = pending.Pop();
                if (!incomingByTarget.TryGetValue(target, out var incoming))
                    continue;
                foreach (var edge in incoming)
                {
                    if (required.Add(edge.source))
                        pending.Push(edge.source);
                }
            }

            document.nodes = document.nodes
                .Where(node => node != null && required.Contains(node.id))
                .ToList();
            document.edges = edges
                .Where(edge =>
                    edge != null &&
                    required.Contains(edge.source) &&
                    required.Contains(edge.target))
                .ToList();
        }
    }
}
