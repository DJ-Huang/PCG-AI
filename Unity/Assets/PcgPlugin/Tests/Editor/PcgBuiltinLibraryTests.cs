using System.Collections.Generic;
using System.IO;
using System.Linq;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEditor;

namespace DJTechEditor.PCG.Tests
{
    /// <summary>
    /// Verifies the synced builtin subgraph library: every index entry resolves to an
    /// imported asset, parses, and its contentHash matches the canonical recomputation
    /// (which scripts/build_library_index.py mirrors from Python).
    /// </summary>
    public sealed class PcgBuiltinLibraryTests
    {
        [Test]
        public void Index_Loads_And_HasEntries()
        {
            Assert.That(PcgBuiltinLibrary.All.Count, Is.GreaterThan(0), "Builtin library index is empty or missing");
        }

        [Test]
        public void EveryEntry_AssetExists_And_Parses()
        {
            foreach (var item in PcgBuiltinLibrary.All)
            {
                var path = PcgBuiltinLibrary.AssetPathFor(item);
                Assert.That(File.Exists(path), Is.True, $"Missing library asset: {path}");
                Assert.That(PcgBuiltinLibrary.TryLoadAsset(item, out var guid, out var asset), Is.True,
                    $"Cannot load asset for {item.id}");
                Assert.That(guid, Is.Not.Empty);
                Assert.That(asset.ImportSucceeded, Is.True, $"Import failed for {item.id}");
                Assert.That(
                    PcgSubgraphAssetSerializer.TryFromJson(asset.SourceJson, out _, out var error),
                    Is.True, $"Parse failed for {item.id}: {error}");
            }
        }

        [Test]
        public void EveryEntry_ContentHash_MatchesCanonical()
        {
            foreach (var item in PcgBuiltinLibrary.All)
            {
                var path = PcgBuiltinLibrary.AssetPathFor(item);
                var json = File.ReadAllText(path);
                Assert.That(
                    PcgSubgraphAssetSerializer.TryFromJson(json, out var doc, out var error),
                    Is.True, $"Parse failed for {item.id}: {error}");
                var recomputed = PcgSubgraphAssetContentHash.Compute(doc);
                Assert.That(doc.contentHash, Is.EqualTo(recomputed),
                    $"Stale contentHash for {item.id} (run scripts/build_library_index.py)");
                Assert.That(item.contentHash, Is.EqualTo(recomputed),
                    $"Index contentHash drifted for {item.id} (run scripts/build_library_index.py)");
            }
        }

        [Test]
        public void Index_Covers_AllSyncedAssets()
        {
            var folder = "Assets/PcgPlugin/Editor/BuiltinLibrary";
            var indexed = new HashSet<string>(
                PcgBuiltinLibrary.All.Select(item => item.file.Replace('\\', '/')));
            foreach (var guid in AssetDatabase.FindAssets("t:PcgSubgraphAsset", new[] { folder }))
            {
                var path = AssetDatabase.GUIDToAssetPath(guid).Replace('\\', '/');
                var relative = path.Substring(folder.Length + 1);
                Assert.That(indexed.Contains(relative), Is.True, $"Synced asset missing from index: {relative}");
            }
        }
    }
}
