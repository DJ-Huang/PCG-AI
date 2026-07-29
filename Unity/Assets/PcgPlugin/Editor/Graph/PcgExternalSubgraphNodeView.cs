using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// GraphView node for linked <c>SubgraphAsset</c> instances.
    /// Ports are built from the persisted interface snapshot; incompatible ports become ghosts.
    /// </summary>
    public sealed class PcgExternalSubgraphNodeView : PcgGraphNodeBase
    {
        /// <summary>Type subtitle shown under the instance name (same pattern as renamed manifest nodes).</summary>
        private const string TypeSubtitle = "subgraph";

        private readonly Dictionary<string, Port> m_Inputs = new();
        private readonly Dictionary<string, Port> m_Outputs = new();
        private readonly HashSet<string> m_GhostHandles = new(StringComparer.Ordinal);
        private PcgNodeData m_Data = new();
        private PcgSubgraphInterfaceSnapshot m_Snapshot = new();
        private string m_StatusError = "";

        public override string NodeType => PcgStructuralNodeTypes.SubgraphAsset;

        public string AssetGuid =>
            PcgAssetGuidUtility.Canonicalize(m_Data.GetRaw("assetGuid")?.ToString() ?? "");

        public PcgSubgraphInterfaceSnapshot Snapshot => m_Snapshot?.Clone();

        /// <summary>instanceNodeId, assetGuid — enter linked asset in the same GraphView.</summary>
        public event Action<string, string> OpenSourceRequested;

        /// <summary>assetGuid — open the source asset in a dedicated Graph Editor window.</summary>
        public event Action<string> OpenSourceInNewWindowRequested;

        public PcgExternalSubgraphNodeView(PcgSubgraphInterfaceSnapshot snapshot)
        {
            m_Snapshot = snapshot?.Clone() ?? new PcgSubgraphInterfaceSnapshot();
            title = TypeSubtitle;
            RefreshDisplayedTitle();
        }

        protected override void BuildPorts()
        {
            RebuildPorts(m_Snapshot, m_GhostHandles);
        }

        public override Port FindInputPort(string handle = "in") =>
            m_Inputs.TryGetValue(handle ?? "in", out var port) ? port : InputPort;

        public override Port FindOutputPort(string handle = "out") =>
            m_Outputs.TryGetValue(handle ?? "out", out var port) ? port : OutputPort;

        public override void ApplyData(PcgNodeData data)
        {
            m_Data = data?.Clone() ?? new PcgNodeData();
            title = TypeSubtitle;
            RefreshDisplayedTitle();
        }

        public override PcgNodeData CollectData()
        {
            var data = m_Data?.Clone() ?? new PcgNodeData();
            var assetName = string.IsNullOrEmpty(m_Snapshot?.name) ? "Subgraph Asset" : m_Snapshot.name;
            var userTitle = GetUserTitle();
            // Only persist a rename when it differs from the asset display name so
            // source renames still refresh the default title.
            data.SetRaw(
                "__nodeTitle",
                string.IsNullOrEmpty(userTitle) || string.Equals(userTitle, assetName, StringComparison.Ordinal)
                    ? ""
                    : userTitle);
            return data;
        }

        public PcgSubgraphInterfaceSnapshot CollectInterfaceSnapshot() => m_Snapshot?.Clone();

        public void SetSnapshot(PcgSubgraphInterfaceSnapshot snapshot, IEnumerable<string> ghostHandles = null)
        {
            m_Snapshot = snapshot?.Clone() ?? new PcgSubgraphInterfaceSnapshot();
            title = TypeSubtitle;
            m_GhostHandles.Clear();
            if (ghostHandles != null)
            {
                foreach (var handle in ghostHandles)
                {
                    if (!string.IsNullOrEmpty(handle))
                        m_GhostHandles.Add(handle);
                }
            }
            RefreshDisplayedTitle();
            RebuildPorts(m_Snapshot, m_GhostHandles);
        }

        public void SetStatusError(string error)
        {
            m_StatusError = error ?? "";
            style.borderLeftColor = string.IsNullOrEmpty(m_StatusError)
                ? StyleKeyword.Null
                : new StyleColor(new Color(0.85f, 0.2f, 0.2f));
            style.borderLeftWidth = string.IsNullOrEmpty(m_StatusError) ? 0f : 3f;
            tooltip = string.IsNullOrEmpty(m_StatusError) ? "" : m_StatusError;
        }

        public string GetPinnedContentHash() =>
            m_Data?.GetRaw(PcgSubgraphAssetInstanceKeys.PinnedContentHash)?.ToString() ?? "";

        public string GetResolvedContentHash() =>
            m_Data?.GetRaw(PcgSubgraphAssetInstanceKeys.ResolvedContentHash)?.ToString() ?? "";

        public void PinCurrentAssetVersion(string contentHash)
        {
            if (string.IsNullOrEmpty(contentHash))
                return;
            m_Data ??= new PcgNodeData();
            m_Data.SetRaw(PcgSubgraphAssetInstanceKeys.PinnedContentHash, contentHash);
        }

        public void ClearPinnedAssetVersion()
        {
            m_Data?.RemoveRaw(PcgSubgraphAssetInstanceKeys.PinnedContentHash);
        }

        public bool HasAssetVersionMismatch(string liveContentHash)
        {
            if (string.IsNullOrEmpty(liveContentHash))
                return false;
            var pinned = GetPinnedContentHash();
            if (!string.IsNullOrEmpty(pinned))
                return !string.Equals(pinned, liveContentHash, StringComparison.Ordinal);
            var resolved = GetResolvedContentHash();
            return !string.IsNullOrEmpty(resolved) &&
                   !string.Equals(resolved, liveContentHash, StringComparison.Ordinal);
        }

        public string GetInputPinType(string handle) => PortType(m_Snapshot.inputs, handle);

        public string GetOutputPinType(string handle) => PortType(m_Snapshot.outputs, handle);

        public bool IsGhostHandle(string handle) => m_GhostHandles.Contains(handle);

        protected override bool HandleDoubleClick()
        {
            OpenSourceRequested?.Invoke(NodeId, AssetGuid);
            return true;
        }

        public override void BuildContextualMenu(ContextualMenuPopulateEvent evt)
        {
            base.BuildContextualMenu(evt);
            var guid = AssetGuid;
            if (string.IsNullOrEmpty(guid))
                return;
            evt.menu.AppendAction(
                "Show Subgraph Asset in Project",
                _ =>
                {
                    var path = AssetDatabase.GUIDToAssetPath(guid);
                    if (string.IsNullOrEmpty(path))
                        return;
                    var asset = AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(path);
                    if (asset == null)
                        return;
                    EditorGUIUtility.PingObject(asset);
                    Selection.activeObject = asset;
                },
                DropdownMenuAction.AlwaysEnabled);
            evt.menu.AppendAction(
                "Open Subgraph Asset in New Window",
                _ => OpenSourceInNewWindowRequested?.Invoke(guid),
                DropdownMenuAction.AlwaysEnabled);
        }

        private void RebuildPorts(PcgSubgraphInterfaceSnapshot snapshot, IEnumerable<string> ghostHandles)
        {
            var ghosts = new HashSet<string>(StringComparer.Ordinal);
            if (ghostHandles != null)
            {
                foreach (var handle in ghostHandles)
                {
                    if (!string.IsNullOrEmpty(handle))
                        ghosts.Add(handle);
                }
            }

            ReconcilePortMap(
                m_Inputs,
                inputContainer,
                snapshot?.inputs,
                Direction.Input,
                ghosts);
            ReconcilePortMap(
                m_Outputs,
                outputContainer,
                snapshot?.outputs,
                Direction.Output,
                ghosts);

            expanded = true;
            RefreshExpandedState();
        }

        private void ReconcilePortMap(
            Dictionary<string, Port> ports,
            VisualElement container,
            IEnumerable<PcgSubgraphPort> desiredPins,
            Direction direction,
            HashSet<string> ghosts)
        {
            var desired = new Dictionary<string, PcgSubgraphPort>(StringComparer.Ordinal);
            foreach (var pin in desiredPins ?? Enumerable.Empty<PcgSubgraphPort>())
            {
                if (pin == null || string.IsNullOrEmpty(pin.id))
                    continue;
                desired[pin.id] = pin;
            }

            foreach (var existingId in ports.Keys.ToList())
            {
                if (desired.ContainsKey(existingId))
                    continue;
                var port = ports[existingId];
                DisconnectPortEdges(port);
                container.Remove(port);
                ports.Remove(existingId);
            }

            foreach (var pair in desired)
            {
                var pin = pair.Value;
                var pinType = string.IsNullOrEmpty(pin.pinType) ? "Any" : pin.pinType;
                if (ports.TryGetValue(pin.id, out var existing))
                {
                    // Keep the same Port instance so GraphView edges stay attached.
                    existing.tooltip = $"{(string.IsNullOrEmpty(pin.name) ? pin.id : pin.name)} ({pinType})";
                    if (ghosts.Contains(pin.id))
                        MarkGhost(existing);
                    else
                        StyleHoudiniPin(existing, direction, pinType);
                    continue;
                }

                var port = InstantiatePort(direction, pin.id, pin.name, pin.pinType);
                if (ghosts.Contains(pin.id))
                    MarkGhost(port);
                container.Add(port);
                ports[pin.id] = port;
            }
        }

        private static void DisconnectPortEdges(Port port)
        {
            if (port == null)
                return;
            var connections = port.connections?.ToList();
            if (connections == null)
                return;
            foreach (var edge in connections)
            {
                if (edge?.input != null && edge?.output != null)
                    edge.output.Disconnect(edge);
                edge?.input?.Disconnect(edge);
                edge?.RemoveFromHierarchy();
            }
        }

        private void BuildPortList(PcgSubgraphInterfaceSnapshot snapshot, IEnumerable<string> ghostHandles)
        {
            RebuildPorts(snapshot, ghostHandles);
        }

        private Port InstantiatePort(Direction direction, string handle, string displayName, string pinType)
        {
            // Match manifest / inline subgraph nodes: empty portName + Houdini pin styling
            // (GraphView's default portName would render a "Mesh" pill next to the connector).
            var port = PcgPort.Create(Orientation.Vertical, direction, Port.Capacity.Multi, typeof(float));
            port.portName = "";
            port.userData = handle;
            var label = string.IsNullOrEmpty(displayName) ? handle : displayName;
            port.tooltip = $"{label} ({pinType ?? "Any"})";
            StyleHoudiniPin(port, direction, pinType);
            return port;
        }

        private void RefreshDisplayedTitle()
        {
            var custom = m_Data?.GetRaw("__nodeTitle")?.ToString()?.Trim() ?? "";
            var assetName = ResolveLinkedAssetDisplayName();
            if (m_Snapshot != null && !string.IsNullOrEmpty(assetName))
                m_Snapshot.name = assetName;
            SetUserTitle(string.IsNullOrEmpty(custom) ? assetName : custom);
        }

        private string ResolveLinkedAssetDisplayName()
        {
            var guid = AssetGuid;
            if (string.IsNullOrEmpty(guid))
                return string.IsNullOrEmpty(m_Snapshot?.name) ? "Subgraph Asset" : m_Snapshot.name;

            var path = AssetDatabase.GUIDToAssetPath(guid);
            return PcgSubgraphAssetNaming.ResolveDisplayName(path, m_Snapshot?.name);
        }

        private static void MarkGhost(Port port)
        {
            if (port == null)
                return;
            port.portColor = new Color(0.85f, 0.25f, 0.25f);
            port.tooltip = (port.tooltip ?? "") + " [ghost — source interface changed]";
        }

        private static string PortType(IEnumerable<PcgSubgraphPort> ports, string handle)
        {
            if (ports == null || string.IsNullOrEmpty(handle))
                return "Any";
            foreach (var port in ports)
            {
                if (port != null && port.id == handle)
                    return string.IsNullOrEmpty(port.pinType) ? "Any" : port.pinType;
            }
            return "Any";
        }
    }

    /// <summary>
    /// Reconciles persisted SubgraphAsset snapshots against the saved source asset interface.
    /// </summary>
    public static class PcgExternalSubgraphInterfaceSync
    {
        public delegate bool SnapshotLoader(
            string assetGuid,
            out PcgSubgraphInterfaceSnapshot snapshot,
            out string contentHash,
            out string schemaVersion,
            out string error);

        public sealed class ReconcileResult
        {
            public PcgSubgraphInterfaceSnapshot Snapshot;
            public List<string> GhostHandles = new();
            public string Error;
            public bool Compatible = true;
        }

        public sealed class DocumentReconcileReport
        {
            public int VisitedNodes;
            public int UpdatedNodes;
            public List<string> Errors = new();
            public bool Compatible => Errors.Count == 0;
        }

        private sealed class LoadedSnapshot
        {
            public bool Success;
            public PcgSubgraphInterfaceSnapshot Snapshot;
            public string ContentHash = "";
            public string SchemaVersion = PcgSubgraphAssetMigration.Version10;
            public string Error;
        }

        public static ReconcileResult Reconcile(
            PcgSubgraphInterfaceSnapshot persisted,
            PcgSubgraphInterfaceSnapshot source,
            Func<string, bool> isHandleConnected)
        {
            var result = new ReconcileResult
            {
                Snapshot = persisted?.Clone() ?? new PcgSubgraphInterfaceSnapshot(),
            };

            if (source == null)
            {
                result.Compatible = false;
                result.Error = "Source SubgraphAsset is missing.";
                if (result.Snapshot.inputs != null)
                    result.GhostHandles.AddRange(result.Snapshot.inputs.Select(port => port.id));
                if (result.Snapshot.outputs != null)
                    result.GhostHandles.AddRange(result.Snapshot.outputs.Select(port => port.id));
                return result;
            }

            result.Snapshot.name = source.name ?? result.Snapshot.name;
            result.Snapshot.inputs = MergePorts(result.Snapshot.inputs, source.inputs, isHandleConnected, result);
            result.Snapshot.outputs = MergePorts(result.Snapshot.outputs, source.outputs, isHandleConnected, result);
            return result;
        }

        /// <summary>
        /// Reconciles and updates a persisted linked-subgraph node record.
        /// Parent-scope edges are used to retain removed or type-changed ports that are still connected.
        /// </summary>
        public static ReconcileResult ReconcileNodeRecord(
            PcgGraphNodeRecord node,
            IEnumerable<PcgGraphEdgeRecord> parentEdges,
            PcgSubgraphInterfaceSnapshot source)
        {
            if (node == null)
                throw new ArgumentNullException(nameof(node));

            var edges = parentEdges ?? Enumerable.Empty<PcgGraphEdgeRecord>();
            bool IsConnected(string handle) =>
                edges.Any(edge =>
                    edge != null &&
                    ((edge.target == node.id && edge.targetHandle == handle) ||
                     (edge.source == node.id && edge.sourceHandle == handle)));

            var result = Reconcile(node.subgraphInterface, source, IsConnected);
            node.subgraphInterface = result.Snapshot?.Clone();
            return result;
        }

        /// <summary>
        /// Reconciles every linked-subgraph record in the root and inline definitions.
        /// This is model-level by design, so hidden parent scopes cannot retain stale ports.
        /// </summary>
        public static DocumentReconcileReport ReconcileDocument(
            PcgGraphDocument document,
            SnapshotLoader loader,
            HashSet<string> changedGuids = null)
        {
            var report = new DocumentReconcileReport();
            if (document == null || loader == null)
                return report;

            HashSet<string> canonicalFilter = null;
            if (changedGuids is { Count: > 0 })
            {
                canonicalFilter = new HashSet<string>(
                    changedGuids.Select(PcgAssetGuidUtility.Canonicalize),
                    StringComparer.Ordinal);
            }

            var cache = new Dictionary<string, LoadedSnapshot>(StringComparer.Ordinal);
            ReconcileScope(
                document.nodes,
                document.edges,
                "<root>",
                loader,
                canonicalFilter,
                cache,
                report);
            foreach (var definition in document.subgraphs ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null)
                    continue;
                ReconcileScope(
                    definition.nodes,
                    definition.edges,
                    string.IsNullOrEmpty(definition.id) ? "<subgraph>" : definition.id,
                    loader,
                    canonicalFilter,
                    cache,
                    report);
            }

            return report;
        }

        private static void ReconcileScope(
            IEnumerable<PcgGraphNodeRecord> nodes,
            IEnumerable<PcgGraphEdgeRecord> edges,
            string scopePath,
            SnapshotLoader loader,
            HashSet<string> changedGuids,
            Dictionary<string, LoadedSnapshot> cache,
            DocumentReconcileReport report)
        {
            var scopeEdges = edges?.ToList() ?? new List<PcgGraphEdgeRecord>();
            foreach (var node in nodes ?? Enumerable.Empty<PcgGraphNodeRecord>())
            {
                if (node == null || node.type != PcgStructuralNodeTypes.SubgraphAsset)
                    continue;

                var guid = PcgAssetGuidUtility.Canonicalize(
                    node.data?.GetRaw("assetGuid")?.ToString() ?? "");
                if (changedGuids != null && !changedGuids.Contains(guid))
                    continue;

                report.VisitedNodes++;
                if (!PcgAssetGuidUtility.IsValid(guid))
                {
                    report.Errors.Add($"{scopePath}/{node.id}: invalid SubgraphAsset GUID.");
                    continue;
                }

                if (!cache.TryGetValue(guid, out var loaded))
                {
                    loaded = new LoadedSnapshot();
                    loaded.Success = loader(
                        guid,
                        out loaded.Snapshot,
                        out loaded.ContentHash,
                        out loaded.SchemaVersion,
                        out loaded.Error);
                    cache[guid] = loaded;
                }

                if (!loaded.Success || loaded.Snapshot == null)
                {
                    report.Errors.Add(
                        $"{scopePath}/{node.id}: {loaded.Error ?? "source interface is unavailable."}");
                    continue;
                }

                var pinnedHash = node.data?.GetRaw(PcgSubgraphAssetInstanceKeys.PinnedContentHash)?.ToString() ?? "";
                var resolvedHash = node.data?.GetRaw(PcgSubgraphAssetInstanceKeys.ResolvedContentHash)?.ToString() ?? "";
                if (!string.IsNullOrEmpty(pinnedHash) &&
                    !string.IsNullOrEmpty(loaded.ContentHash) &&
                    !string.Equals(pinnedHash, loaded.ContentHash, StringComparison.Ordinal))
                {
                    report.Errors.Add(
                        $"{scopePath}/{node.id}: pinned asset version does not match source ({pinnedHash} != {loaded.ContentHash}).");
                    continue;
                }

                var before = node.subgraphInterface?.Clone();
                var result = ReconcileNodeRecord(node, scopeEdges, loaded.Snapshot);
                node.data ??= new PcgNodeData();
                if (!string.IsNullOrEmpty(loaded.ContentHash))
                    node.data.SetRaw(PcgSubgraphAssetInstanceKeys.ResolvedContentHash, loaded.ContentHash);
                if (!string.IsNullOrEmpty(loaded.SchemaVersion))
                    node.data.SetRaw(PcgSubgraphAssetInstanceKeys.ResolvedSchemaVersion, loaded.SchemaVersion);
                if (!string.IsNullOrEmpty(pinnedHash))
                    node.data.SetRaw(PcgSubgraphAssetInstanceKeys.PinnedContentHash, pinnedHash);
                else if (!string.IsNullOrEmpty(resolvedHash) &&
                         !string.IsNullOrEmpty(loaded.ContentHash) &&
                         !string.Equals(resolvedHash, loaded.ContentHash, StringComparison.Ordinal) &&
                         result.Compatible)
                {
                    // Source changed since last reconcile; keep snapshot updated unless pinned.
                }
                if (!SnapshotsEqual(before, node.subgraphInterface))
                    report.UpdatedNodes++;
                if (!result.Compatible)
                    report.Errors.Add($"{scopePath}/{node.id}: {result.Error}");
            }
        }

        private static List<PcgSubgraphPort> MergePorts(
            List<PcgSubgraphPort> persisted,
            List<PcgSubgraphPort> source,
            Func<string, bool> isHandleConnected,
            ReconcileResult result)
        {
            persisted ??= new List<PcgSubgraphPort>();
            source ??= new List<PcgSubgraphPort>();
            var sourceById = source
                .Where(port => port != null && !string.IsNullOrEmpty(port.id))
                .ToDictionary(port => port.id, StringComparer.Ordinal);
            var merged = new List<PcgSubgraphPort>();
            var seen = new HashSet<string>(StringComparer.Ordinal);

            foreach (var port in persisted)
            {
                if (port == null || string.IsNullOrEmpty(port.id))
                    continue;
                seen.Add(port.id);
                if (!sourceById.TryGetValue(port.id, out var sourcePort))
                {
                    if (isHandleConnected != null && isHandleConnected(port.id))
                    {
                        merged.Add(ClonePort(port));
                        result.GhostHandles.Add(port.id);
                        result.Compatible = false;
                        result.Error = $"Source removed connected port '{port.id}'.";
                    }
                    continue;
                }

                if (!string.Equals(port.pinType ?? "Any", sourcePort.pinType ?? "Any", StringComparison.Ordinal))
                {
                    if (isHandleConnected != null && isHandleConnected(port.id))
                    {
                        merged.Add(ClonePort(port));
                        result.GhostHandles.Add(port.id);
                        result.Compatible = false;
                        result.Error = $"Port '{port.id}' pinType changed.";
                        continue;
                    }
                }

                merged.Add(ClonePort(sourcePort));
            }

            foreach (var port in source)
            {
                if (port == null || string.IsNullOrEmpty(port.id) || seen.Contains(port.id))
                    continue;
                merged.Add(ClonePort(port));
            }

            return merged;
        }

        private static PcgSubgraphPort ClonePort(PcgSubgraphPort port)
        {
            return new PcgSubgraphPort
            {
                id = port.id,
                name = port.name,
                pinType = port.pinType,
                anchorPlaced = port.anchorPlaced,
                anchorX = port.anchorX,
                anchorY = port.anchorY,
            };
        }

        private static bool SnapshotsEqual(
            PcgSubgraphInterfaceSnapshot left,
            PcgSubgraphInterfaceSnapshot right)
        {
            if (ReferenceEquals(left, right))
                return true;
            if (left == null || right == null ||
                !string.Equals(left.name ?? "", right.name ?? "", StringComparison.Ordinal))
            {
                return false;
            }

            return PortsEqual(left.inputs, right.inputs) &&
                   PortsEqual(left.outputs, right.outputs);
        }

        private static bool PortsEqual(
            IReadOnlyList<PcgSubgraphPort> left,
            IReadOnlyList<PcgSubgraphPort> right)
        {
            var leftCount = left?.Count ?? 0;
            var rightCount = right?.Count ?? 0;
            if (leftCount != rightCount)
                return false;
            for (var index = 0; index < leftCount; index++)
            {
                var a = left[index];
                var b = right[index];
                if (a == null || b == null)
                {
                    if (!ReferenceEquals(a, b))
                        return false;
                    continue;
                }

                if (!string.Equals(a.id ?? "", b.id ?? "", StringComparison.Ordinal) ||
                    !string.Equals(a.name ?? "", b.name ?? "", StringComparison.Ordinal) ||
                    !string.Equals(a.pinType ?? "Any", b.pinType ?? "Any", StringComparison.Ordinal) ||
                    a.anchorPlaced != b.anchorPlaced ||
                    !Mathf.Approximately(a.anchorX, b.anchorX) ||
                    !Mathf.Approximately(a.anchorY, b.anchorY))
                {
                    return false;
                }
            }

            return true;
        }
    }
}
