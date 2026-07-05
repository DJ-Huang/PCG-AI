using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgGraphSearchWindow : ScriptableObject, ISearchWindowProvider
    {
        private PcgGraphView _graphView;
        private Vector2 _spawnPosition;

        public void Initialize(PcgGraphView graphView) => _graphView = graphView;

        public void SetSpawnPosition(Vector2 graphPosition) => _spawnPosition = graphPosition;

        public List<SearchTreeEntry> CreateSearchTree(SearchWindowContext context)
        {
            var tree = new List<SearchTreeEntry>
            {
                new SearchTreeGroupEntry(new GUIContent("Create Node"), 0),
            };

            var legacyTypes = new[] { PcgNodeTypes.ParseConfig, PcgNodeTypes.SpawnPoints, PcgNodeTypes.PlaceInScene };
            AddGroup(tree, "Legacy MVP", legacyTypes);

            var manifestGroups = PcgNodeManifest.All
                .Where(def => PcgNodeManifest.IsManifestOnlyType(def.type))
                .GroupBy(def => def.category ?? "Other")
                .OrderBy(g => g.Key);

            foreach (var group in manifestGroups)
                AddGroup(tree, group.Key, group.Select(def => def.type));

            return tree;
        }

        private static void AddGroup(List<SearchTreeEntry> tree, string groupName, IEnumerable<string> types)
        {
            tree.Add(new SearchTreeGroupEntry(new GUIContent(groupName), 1));
            foreach (var type in types)
            {
                var label = PcgNodeManifest.TryGet(type, out var def) ? def.displayName ?? type : type;
                tree.Add(new SearchTreeEntry(new GUIContent(label)) { level = 2, userData = type });
            }
        }

        public bool OnSelectEntry(SearchTreeEntry searchTreeEntry, SearchWindowContext context)
        {
            if (searchTreeEntry.userData is not string type)
                return false;

            _graphView.CreateNode(type, _spawnPosition);
            return true;
        }
    }
}
