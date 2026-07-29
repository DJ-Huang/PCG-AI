using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Imports <c>.pcgsubgraph</c> linked Subgraph assets.
    /// </summary>
    [ScriptedImporter(1, "pcgsubgraph")]
    public sealed class PcgSubgraphAssetImporter : ScriptedImporter
    {
        public override void OnImportAsset(AssetImportContext ctx)
        {
            var json = File.ReadAllText(ctx.assetPath);
            var asset = ScriptableObject.CreateInstance<PcgSubgraphAsset>();

            if (!PcgSubgraphAssetSerializer.TryFromJson(json, out var doc, out var error))
            {
                asset.SetImportResult(json, "", false, error, Array.Empty<string>());
                ctx.AddObjectToAsset("main", asset);
                ctx.SetMainObject(asset);
                Debug.LogError($"[PCG] Failed to import {ctx.assetPath}: {error}", asset);
                return;
            }

            var deps = PcgExecutionDocumentBuilder.CollectExternalGuids(
                    new PcgGraphDocument
                    {
                        version = "3.0",
                        nodes = doc.nodes,
                        edges = doc.edges,
                        subgraphs = doc.subgraphs,
                    })
                .Distinct(StringComparer.Ordinal)
                .OrderBy(guid => guid, StringComparer.Ordinal)
                .ToList();

            foreach (var guid in deps)
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                if (!string.IsNullOrEmpty(path))
                    ctx.DependsOnSourceAsset(path);
            }

            asset.SetImportResult(
                json,
                PcgSubgraphAssetNaming.ResolveDisplayName(ctx.assetPath, doc),
                true,
                "",
                deps);
            ctx.AddObjectToAsset("main", asset);
            ctx.SetMainObject(asset);
        }
    }
}
