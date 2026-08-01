#if UNITY_EDITOR
using System.Collections.Generic;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// When PcgGraphComponent runs in the Editor, prefer the live Graph Editor document
    /// (includes unsaved ImageTexture assignments) over the on-disk .pcg file —
    /// unless <see cref="SessionPreferAssetJson"/> or the component's PreferAssetJson is set.
    /// </summary>
    [InitializeOnLoad]
    public static class PcgGraphExecutionBridge
    {
        /// <summary>
        /// Session-wide override for Agent/MCP cooks: when true, always use on-disk .pcg
        /// even if a Graph Editor window is open for the same asset.
        /// </summary>
        public static bool SessionPreferAssetJson { get; set; }

        /// <summary>
        /// Root-scope Subgraph instance node id → flat node id that sourced its first output,
        /// captured by the most recent <see cref="BuildExecutionJson"/>. Read by the Graph
        /// Editor info panel so a flattened Subgraph instance still shows its output stats.
        /// </summary>
        internal static IReadOnlyDictionary<string, string> LastOutputStatsAliases { get; private set; }

        /// <summary>Bumped whenever <see cref="LastOutputStatsAliases"/> is replaced.</summary>
        internal static long LastOutputStatsAliasesVersion { get; private set; }

        static PcgGraphExecutionBridge()
        {
            PcgGraphComponent.EditorBuildExecutionJson = BuildExecutionJson;
            PcgGraphComponent.EditorIsNodePreviewActive = IsNodePreviewActive;
        }

        private static bool IsNodePreviewActive(PcgGraphComponent component)
        {
            if (component == null || component.GraphAsset == null)
                return false;

            var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
            if (string.IsNullOrEmpty(assetPath))
                return false;

            var assetGuid = AssetDatabase.AssetPathToGUID(assetPath);
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.HasLoadedGraph)
                    continue;
                if (!window.MatchesGraphAsset(assetPath, assetGuid))
                    continue;
                if (!string.IsNullOrEmpty(window.PreviewNodeId))
                    return true;
            }

            return false;
        }

        private static string BuildExecutionJson(PcgGraphComponent component)
        {
            if (component == null || component.GraphAsset == null)
                return null;

            var assetPath = AssetDatabase.GetAssetPath(component.GraphAsset);
            if (string.IsNullOrEmpty(assetPath))
                return null;

            // Node Preview always needs the live Graph Editor document + truncated cook.
            // PreferAssetJson/SessionPreferAssetJson only apply to full-graph Output cooks.
            var preferAsset = !IsNodePreviewActive(component) &&
                (SessionPreferAssetJson || component.PreferAssetJson);
            if (preferAsset)
            {
                Debug.Log(
                    $"[PCG] preferAssetJson: using on-disk asset for '{assetPath}' " +
                    $"(session={SessionPreferAssetJson}, component={component.PreferAssetJson}).");
                return null;
            }

            var assetGuid = AssetDatabase.AssetPathToGUID(assetPath);
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.HasLoadedGraph)
                    continue;

                if (!window.MatchesGraphAsset(assetPath, assetGuid))
                    continue;

                if (window.IsSubgraphAssetMode)
                {
                    Debug.LogWarning("[PCG] Subgraph Asset mode cannot cook standalone; open a consuming .pcg graph.");
                    return null;
                }

                var liveDoc = window.ExportLiveDocument();
                if (liveDoc == null)
                    continue;

                component.ApplyOverridesToDocument(liveDoc);

                var cookDoc = liveDoc;
                var previewNodeId = window.PreviewNodeId;
                Dictionary<string, string> previewStatsAliases = null;
                if (!string.IsNullOrEmpty(previewNodeId))
                {
                    if (!PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                            liveDoc,
                            previewNodeId,
                            window.PreviewScopeSubgraphId,
                            window.PreviewInstanceChain,
                            out var subgraph,
                            out previewStatsAliases,
                            out var previewError))
                    {
                        Debug.LogError($"[PCG] Node preview subgraph failed: {previewError}");
                        return null;
                    }

                    cookDoc = subgraph;
                    Debug.Log(
                        $"[PCG] Run uses node preview '{window.PreviewNodeLabel}' for '{assetPath}'.");
                }
                else
                {
                    Debug.Log($"[PCG] Run uses live Graph Editor state for '{assetPath}'.");
                }

                if (!PcgExecutionDocumentBuilder.TryBuildJson(
                        cookDoc,
                        window.CreateExternalSubgraphLoader(),
                        out var flatJson,
                        out _,
                        out var buildError,
                        out var outputStatsAliases,
                        pretty: false))
                {
                    Debug.LogError($"[PCG] Failed to build execution document: {buildError}");
                    return null;
                }

                LastOutputStatsAliases = MergeOutputStatsAliases(outputStatsAliases, previewStatsAliases);
                LastOutputStatsAliasesVersion++;

                return flatJson;
            }

            return null;
        }

        /// <summary>
        /// Flattener aliases (Subgraph instances → flat source) win on key collision;
        /// preview aliases (interface Output/anchor ids → cooked source) fill the rest.
        /// </summary>
        private static IReadOnlyDictionary<string, string> MergeOutputStatsAliases(
            IReadOnlyDictionary<string, string> flattenerAliases,
            IReadOnlyDictionary<string, string> previewAliases)
        {
            if (previewAliases == null || previewAliases.Count == 0)
                return flattenerAliases;

            var merged = new Dictionary<string, string>();
            if (flattenerAliases != null)
            {
                foreach (var pair in flattenerAliases)
                    merged[pair.Key] = pair.Value;
            }

            foreach (var pair in previewAliases)
            {
                if (!merged.ContainsKey(pair.Key))
                    merged[pair.Key] = pair.Value;
            }

            return merged;
        }
    }
}
#endif
