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
    /// Imports .pcg files as PcgGraphAsset.
    /// v1/v2 without external refs cache raw JSON; v3/external refs bake to flat v2 for Player.
    /// Editor windows continue to read raw source via <see cref="PcgGraphAssetUtility.ReadLatestJson"/>.
    /// </summary>
    [ScriptedImporter(2, "pcg")]
    public sealed class PcgAssetImporter : ScriptedImporter
    {
        private const string ThumbnailPath = "Assets/PcgPlugin/Editor/Icons/pcg-icon-32.png";

        public override void OnImportAsset(AssetImportContext ctx)
        {
            var json = File.ReadAllText(ctx.assetPath);
            var asset = ScriptableObject.CreateInstance<PcgGraphAsset>();

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out var parseError))
            {
                asset.SetImportResult("", false, parseError, Array.Empty<string>());
                AddMain(ctx, asset);
                Debug.LogError($"[PCG] Failed to import {ctx.assetPath}: {parseError}", asset);
                return;
            }

            if (!doc.HasExternalSubgraphAssets() && doc.version != "3.0")
            {
                asset.SetImportResult(json, true, "", Array.Empty<string>());
                AddMain(ctx, asset);
                return;
            }

            // Pure file loader first to build closure without importer re-entry.
            var fileLoader = CreateFileGuidLoader(out var guidToPath);
            if (!PcgExecutionDocumentBuilder.TryBuildJson(
                    doc,
                    fileLoader,
                    out var bakedJson,
                    out var resolveResult,
                    out var bakeError,
                    pretty: false))
            {
                asset.SetImportResult("", false, bakeError, Array.Empty<string>());
                AddMain(ctx, asset);
                Debug.LogError($"[PCG] Failed to bake {ctx.assetPath}: {bakeError}", asset);
                return;
            }

            var deps = resolveResult?.DependencyGuids ?? new List<string>();
            foreach (var guid in deps)
            {
                if (guidToPath.TryGetValue(guid, out var path) && !string.IsNullOrEmpty(path))
                    ctx.DependsOnSourceAsset(path);
            }

            asset.SetImportResult(bakedJson, true, "", deps);
            AddMain(ctx, asset);
        }

        private static void AddMain(AssetImportContext ctx, PcgGraphAsset asset)
        {
            var thumbnail = AssetDatabase.LoadAssetAtPath<Texture2D>(ThumbnailPath);
            if (thumbnail != null)
                ctx.AddObjectToAsset("main", asset, thumbnail);
            else
                ctx.AddObjectToAsset("main", asset);
            ctx.SetMainObject(asset);
        }

        private static PcgExternalSubgraphLoader CreateFileGuidLoader(
            out Dictionary<string, string> guidToPath)
        {
            guidToPath = new Dictionary<string, string>(StringComparer.Ordinal);
            var assetsRoot = Application.dataPath;
            if (Directory.Exists(assetsRoot))
            {
                foreach (var path in Directory.EnumerateFiles(assetsRoot, "*.pcgsubgraph", SearchOption.AllDirectories))
                {
                    var projectPath = "Assets" + path.Substring(assetsRoot.Length).Replace('\\', '/');
                    var metaPath = projectPath + ".meta";
                    if (!File.Exists(metaPath))
                        continue;
                    var guid = TryReadMetaGuid(metaPath);
                    if (!PcgAssetGuidUtility.IsValid(guid))
                        continue;
                    guid = PcgAssetGuidUtility.Canonicalize(guid);
                    guidToPath[guid] = projectPath;
                }
            }

            var map = guidToPath;
            return (string assetGuid, out string sourceJson, out string loadError) =>
            {
                sourceJson = null;
                loadError = null;
                var canonical = PcgAssetGuidUtility.Canonicalize(assetGuid);
                if (!map.TryGetValue(canonical, out var projectPath))
                {
                    // Fallback to AssetDatabase for non-file contexts.
                    projectPath = AssetDatabase.GUIDToAssetPath(canonical);
                }

                if (string.IsNullOrEmpty(projectPath))
                {
                    loadError = "No .pcgsubgraph found for GUID " + canonical;
                    return false;
                }

                var absolute = Path.GetFullPath(Path.Combine(
                    Directory.GetParent(Application.dataPath)!.FullName, projectPath));
                if (!File.Exists(absolute))
                {
                    loadError = "Missing file: " + projectPath;
                    return false;
                }

                try
                {
                    sourceJson = File.ReadAllText(absolute);
                    map[canonical] = projectPath;
                    return true;
                }
                catch (Exception ex)
                {
                    loadError = ex.Message;
                    return false;
                }
            };
        }

        private static string TryReadMetaGuid(string metaPath)
        {
            try
            {
                foreach (var line in File.ReadLines(metaPath))
                {
                    if (!line.StartsWith("guid:", StringComparison.Ordinal))
                        continue;
                    return line.Substring("guid:".Length).Trim();
                }
            }
            catch
            {
                // ignored
            }

            return "";
        }
    }
}
