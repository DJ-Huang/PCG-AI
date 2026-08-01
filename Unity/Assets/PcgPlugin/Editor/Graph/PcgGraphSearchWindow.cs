using System.Collections.Generic;
using System.Linq;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgGraphSearchWindow : ScriptableObject, ISearchWindowProvider
    {
        private PcgGraphView _graphView;
        private Vector2 _spawnPosition;
        private Port _draggedPort;
        private HashSet<string> _compatibleTypes;

        public void Initialize(PcgGraphView graphView) => _graphView = graphView;

        public void SetSpawnPosition(Vector2 graphPosition) => _spawnPosition = graphPosition;

        public void SetPortDragContext(Port port, HashSet<string> compatibleTypes)
        {
            _draggedPort = port;
            _compatibleTypes = compatibleTypes;
        }

        public void ClearPortDragContext()
        {
            _draggedPort = null;
            _compatibleTypes = null;
        }

        public List<SearchTreeEntry> CreateSearchTree(SearchWindowContext context)
        {
            var tree = new List<SearchTreeEntry>
            {
                new SearchTreeGroupEntry(new GUIContent("Create Node"), 0),
            };

            if (_draggedPort != null && _compatibleTypes != null)
            {
                var manifestGroups = PcgNodeManifest.All
                    .Where(def => _compatibleTypes.Contains(def.type))
                    .GroupBy(def => def.category ?? "Other")
                    .OrderBy(g => g.Key);

                foreach (var group in manifestGroups)
                    AddGroup(tree, group.Key, group.Select(def => def.type));
            }
            else
            {
                var manifestGroups = PcgNodeManifest.All
                    .GroupBy(def => def.category ?? "Other")
                    .OrderBy(g => g.Key);

                foreach (var group in manifestGroups)
                    AddGroup(tree, group.Key, group.Select(def => def.type));
            }

            return tree;
        }

        private static void AddGroup(List<SearchTreeEntry> tree, string groupName, IEnumerable<string> types)
        {
            tree.Add(new SearchTreeGroupEntry(new GUIContent(groupName), 1));
            foreach (var type in types)
            {
                string label = BuildSearchLabel(type);
                tree.Add(new SearchTreeEntry(new GUIContent(label)) { level = 2, userData = type });
            }
        }

        private static string BuildSearchLabel(string type)
        {
            if (!PcgNodeManifest.TryGet(type, out var def))
                return type;

            var displayName = string.IsNullOrWhiteSpace(def.displayName) ? type : def.displayName;
            var compactDisplay = displayName.Replace(" ", "");
            var compactType = type.Replace(" ", "");

            if (compactDisplay == compactType)
                return $"{displayName} ({type})";
            return $"{displayName} ({type}) [{compactDisplay}]";
        }

        public bool OnSelectEntry(SearchTreeEntry searchTreeEntry, SearchWindowContext context)
        {
            if (searchTreeEntry.userData is not string type)
                return false;

            if (_draggedPort != null)
            {
                var port = _draggedPort;
                ClearPortDragContext();
                _graphView.CreateNodeAndConnect(type, _spawnPosition, port);
            }
            else
            {
                _graphView.CreateNode(type, _spawnPosition);
            }
            return true;
        }
    }
}
