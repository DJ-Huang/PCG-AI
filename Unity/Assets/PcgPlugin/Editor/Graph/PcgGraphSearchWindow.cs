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
        private string _draggedPinType;

        public void Initialize(PcgGraphView graphView) => _graphView = graphView;

        public void SetSpawnPosition(Vector2 graphPosition) => _spawnPosition = graphPosition;

        public void SetPortDragContext(Port port, HashSet<string> compatibleTypes, string pinType = null)
        {
            _draggedPort = port;
            _compatibleTypes = compatibleTypes;
            _draggedPinType = pinType;
        }

        public void ClearPortDragContext()
        {
            _draggedPort = null;
            _compatibleTypes = null;
            _draggedPinType = null;
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

            AddLibraryGroups(tree);

            return tree;
        }

        private void AddLibraryGroups(List<SearchTreeEntry> tree)
        {
            foreach (var group in PcgBuiltinLibrary.ByCategory())
            {
                IEnumerable<PcgBuiltinLibrary.LibraryItem> items = group;
                if (_draggedPort != null && !string.IsNullOrEmpty(_draggedPinType))
                {
                    var pinType = _draggedPinType;
                    var isOutputDrag = _draggedPort.direction == Direction.Output;
                    items = items.Where(item => isOutputDrag
                        ? PcgBuiltinLibrary.HasCompatibleInput(item, pinType)
                        : PcgBuiltinLibrary.HasCompatibleOutput(item, pinType));
                }

                var list = items.ToList();
                if (list.Count == 0)
                    continue;

                tree.Add(new SearchTreeGroupEntry(new GUIContent($"Library/{group.Key}"), 1));
                foreach (var item in list)
                {
                    var label = $"{item.displayName} ({item.id})";
                    tree.Add(new SearchTreeEntry(new GUIContent(label, item.description))
                    {
                        level = 2,
                        userData = PcgBuiltinLibrary.MakeSearchUserData(item),
                    });
                }
            }
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
            if (PcgBuiltinLibrary.TryParseSearchUserData(searchTreeEntry.userData, out var libraryItem))
            {
                if (_draggedPort != null)
                {
                    var draggedPort = _draggedPort;
                    ClearPortDragContext();
                    _graphView.CreateLibraryNodeAndConnect(libraryItem, _spawnPosition, draggedPort);
                }
                else
                {
                    _graphView.CreateLibrarySubgraphNode(libraryItem.id, _spawnPosition);
                }
                return true;
            }

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
