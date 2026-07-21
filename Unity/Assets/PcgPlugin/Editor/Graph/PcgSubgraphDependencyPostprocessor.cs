using System;
using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Reimports consumer graphs when linked Subgraph assets change, and notifies open editor windows.
    /// </summary>
    public sealed class PcgSubgraphDependencyPostprocessor : AssetPostprocessor
    {
        private static void OnPostprocessAllAssets(
            string[] importedAssets,
            string[] deletedAssets,
            string[] movedAssets,
            string[] movedFromAssetPaths)
        {
            var changedGuids = new HashSet<string>(StringComparer.Ordinal);
            CollectGuids(importedAssets, changedGuids);
            CollectGuids(deletedAssets, changedGuids);
            CollectGuids(movedAssets, changedGuids);

            if (changedGuids.Count == 0)
                return;

            RefreshConsumers(changedGuids);
            NotifyOpenWindows(changedGuids);
        }

        private static void CollectGuids(string[] paths, HashSet<string> output)
        {
            if (paths == null)
                return;
            foreach (var path in paths)
            {
                if (string.IsNullOrEmpty(path))
                    continue;
                if (!path.EndsWith(".pcgsubgraph", StringComparison.OrdinalIgnoreCase) &&
                    !path.EndsWith(".pcg", StringComparison.OrdinalIgnoreCase))
                    continue;
                var guid = AssetDatabase.AssetPathToGUID(path);
                if (PcgAssetGuidUtility.IsValid(guid))
                    output.Add(PcgAssetGuidUtility.Canonicalize(guid));
            }
        }

        internal static void RefreshConsumers(HashSet<string> changedGuids)
        {
            var consumers = AssetDatabase.FindAssets("t:PcgGraphAsset")
                .Select(AssetDatabase.GUIDToAssetPath)
                .Where(path => path.EndsWith(".pcg", StringComparison.OrdinalIgnoreCase))
                .ToList();

            foreach (var path in consumers)
            {
                var asset = AssetDatabase.LoadAssetAtPath<PcgGraphAsset>(path);
                if (asset == null || asset.DependencyGuids == null || asset.DependencyGuids.Count == 0)
                    continue;

                var depends = false;
                foreach (var dep in asset.DependencyGuids)
                {
                    if (changedGuids.Contains(PcgAssetGuidUtility.Canonicalize(dep)))
                    {
                        depends = true;
                        break;
                    }
                }

                if (depends)
                    AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceUpdate);
            }
        }

        private static void NotifyOpenWindows(HashSet<string> changedGuids)
        {
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                window?.ReconcileExternalSubgraphAssets(changedGuids);
            }
        }
    }
}
