using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace DJTechRuntime.PCG
{
    public delegate bool PcgExternalSubgraphLoader(string assetGuid, out string sourceJson, out string error);

    public sealed class PcgExternalResolveResult
    {
        public PcgGraphDocument Document;
        public List<string> DependencyGuids = new();
        public List<string> DependencyPaths = new();
        public string Error;
        public string ErrorChain;
    }

    /// <summary>
    /// Recursively resolves linked <c>SubgraphAsset</c> nodes into inline <c>Subgraph</c>
    /// definitions so Core and Host flatteners only see a self-contained document.
    /// </summary>
    public static class PcgExternalSubgraphResolver
    {
        public const int MaxDepth = 32;
        public const int MaxAssets = 256;

        public static bool TryResolveToInline(
            PcgGraphDocument authoring,
            PcgExternalSubgraphLoader loader,
            out PcgExternalResolveResult result)
        {
            result = new PcgExternalResolveResult();
            if (authoring == null)
            {
                result.Error = "Authoring document is null.";
                return false;
            }

            if (loader == null)
            {
                result.Error = "External subgraph loader is null.";
                return false;
            }

            var working = authoring.Clone();
            var memo = new Dictionary<string, ResolvedAsset>(StringComparer.Ordinal);
            var hashRegistry = new Dictionary<string, string>(StringComparer.Ordinal);
            var stack = new List<string>();
            var dependencyOrder = new List<string>();

            // Always inject resolved definitions into the root document subgraphs list.
            working.subgraphs ??= new List<PcgSubgraphDefinition>();
            if (!ResolveScope(
                    working.nodes,
                    working.edges,
                    working.subgraphs,
                    working.subgraphs,
                    loader,
                    memo,
                    hashRegistry,
                    stack,
                    dependencyOrder,
                    depth: 0,
                    instancePath: "<root>",
                    out var error,
                    out var chain))
            {
                result.Error = error;
                result.ErrorChain = chain;
                return false;
            }

            if (working.HasExternalSubgraphAssets())
            {
                result.Error = "Resolve left residual SubgraphAsset nodes.";
                result.ErrorChain = "<root>";
                return false;
            }

            if (working.version == "3.0")
                working.version = "2.0";

            result.Document = working;
            result.DependencyGuids = dependencyOrder
                .Distinct(StringComparer.Ordinal)
                .OrderBy(guid => guid, StringComparer.Ordinal)
                .ToList();
            return true;
        }

        private sealed class ResolvedAsset
        {
            public string Guid;
            public string DefinitionId;
            public PcgSubgraphAssetDocument Document;
            public PcgSubgraphInterfaceSnapshot Interface;
            public List<PcgSubgraphDefinition> NestedDefinitions = new();
        }

        private static bool ResolveScope(
            List<PcgGraphNodeRecord> nodes,
            List<PcgGraphEdgeRecord> edges,
            List<PcgSubgraphDefinition> localDefinitions,
            List<PcgSubgraphDefinition> rootDefinitions,
            PcgExternalSubgraphLoader loader,
            Dictionary<string, ResolvedAsset> memo,
            Dictionary<string, string> hashRegistry,
            List<string> stack,
            List<string> dependencyOrder,
            int depth,
            string instancePath,
            out string error,
            out string chain)
        {
            error = null;
            chain = instancePath;

            if (localDefinitions != null)
            {
                // Snapshot: EnsureDefinition may append to the same root list while resolving.
                var existing = localDefinitions.ToList();
                foreach (var definition in existing)
                {
                    if (definition == null)
                        continue;
                    if (!ResolveScope(
                            definition.nodes,
                            definition.edges,
                            null,
                            rootDefinitions,
                            loader,
                            memo,
                            hashRegistry,
                            stack,
                            dependencyOrder,
                            depth,
                            instancePath + "/" + definition.id,
                            out error,
                            out chain))
                        return false;
                }
            }

            if (nodes == null)
                return true;

            for (var i = 0; i < nodes.Count; i++)
            {
                var node = nodes[i];
                if (node == null || node.type != PcgStructuralNodeTypes.SubgraphAsset)
                    continue;

                if (depth >= MaxDepth)
                {
                    error = $"External subgraph depth exceeded {MaxDepth}.";
                    chain = BuildChain(stack, instancePath, node.id);
                    return false;
                }

                var rawGuid = node.data?.GetRaw("assetGuid")?.ToString() ?? "";
                var guid = PcgAssetGuidUtility.Canonicalize(rawGuid);
                if (!PcgAssetGuidUtility.IsValid(guid))
                {
                    error = $"Invalid SubgraphAsset.assetGuid on node '{node.id}'.";
                    chain = BuildChain(stack, instancePath, node.id);
                    return false;
                }

                if (stack.Contains(guid))
                {
                    error = $"Recursive external subgraph reference detected: {guid}";
                    chain = BuildChain(stack, instancePath, node.id) + " -> " + guid;
                    return false;
                }

                if (!memo.TryGetValue(guid, out var resolved))
                {
                    if (memo.Count >= MaxAssets)
                    {
                        error = $"External subgraph asset count exceeded {MaxAssets}.";
                        chain = BuildChain(stack, instancePath, node.id);
                        return false;
                    }

                    if (!loader(guid, out var sourceJson, out var loadError))
                    {
                        error = $"Missing or unreadable SubgraphAsset '{guid}': {loadError}";
                        chain = BuildChain(stack, instancePath, node.id);
                        return false;
                    }

                    if (!PcgSubgraphAssetSerializer.TryFromJson(sourceJson, out var assetDoc, out var parseError))
                    {
                        error = $"Failed to parse SubgraphAsset '{guid}': {parseError}";
                        chain = BuildChain(stack, instancePath, node.id);
                        return false;
                    }

                    string definitionId;
                    try
                    {
                        definitionId = PcgAssetGuidUtility.DefinitionIdForGuid(guid);
                    }
                    catch (Exception ex)
                    {
                        error = ex.Message;
                        chain = BuildChain(stack, instancePath, node.id);
                        return false;
                    }

                    if (hashRegistry.TryGetValue(definitionId, out var existingGuid) && existingGuid != guid)
                    {
                        error = $"External subgraph definition id collision between '{existingGuid}' and '{guid}'.";
                        chain = BuildChain(stack, instancePath, node.id);
                        return false;
                    }

                    hashRegistry[definitionId] = guid;
                    resolved = new ResolvedAsset
                    {
                        Guid = guid,
                        DefinitionId = definitionId,
                        Document = assetDoc,
                        Interface = new PcgSubgraphInterfaceSnapshot
                        {
                            name = assetDoc.name ?? "",
                            inputs = assetDoc.inputs.Select(ClonePort).ToList(),
                            outputs = assetDoc.outputs.Select(ClonePort).ToList(),
                        },
                    };
                    memo[guid] = resolved;
                    dependencyOrder.Add(guid);

                    stack.Add(guid);
                    if (!ResolveScope(
                            assetDoc.nodes,
                            assetDoc.edges,
                            assetDoc.subgraphs,
                            rootDefinitions,
                            loader,
                            memo,
                            hashRegistry,
                            stack,
                            dependencyOrder,
                            depth + 1,
                            instancePath + "/" + node.id,
                            out error,
                            out chain))
                        return false;
                    stack.RemoveAt(stack.Count - 1);

                    foreach (var nested in assetDoc.subgraphs)
                    {
                        if (nested == null)
                            continue;
                        var remapped = nested.Clone();
                        remapped.id = definitionId + "__" + nested.id;
                        RemapInlineSubgraphIds(remapped.nodes, definitionId);
                        resolved.NestedDefinitions.Add(remapped);
                    }
                }

                if (!InterfacesCompatible(
                        node.subgraphInterface,
                        resolved.Interface,
                        edges,
                        node.id,
                        out var mismatch))
                {
                    error = $"SubgraphAsset interface mismatch on '{node.id}': {mismatch}";
                    chain = BuildChain(stack, instancePath, node.id);
                    return false;
                }

                var data = node.data?.Clone() ?? new PcgNodeData();
                data.SetRaw("subgraphId", resolved.DefinitionId);
                nodes[i] = new PcgGraphNodeRecord
                {
                    id = node.id,
                    type = PcgStructuralNodeTypes.Subgraph,
                    position = node.position,
                    data = data,
                    subgraphInterface = null,
                };

                EnsureDefinition(rootDefinitions, resolved);
            }

            return true;
        }

        private static void EnsureDefinition(List<PcgSubgraphDefinition> definitions, ResolvedAsset resolved)
        {
            if (definitions == null)
                return;

            if (definitions.All(definition => definition == null || definition.id != resolved.DefinitionId))
            {
                var root = resolved.Document.ToRootDefinition(resolved.DefinitionId);
                RemapInlineSubgraphIds(root.nodes, resolved.DefinitionId);
                definitions.Add(root);
            }

            foreach (var nested in resolved.NestedDefinitions)
            {
                if (definitions.All(definition => definition == null || definition.id != nested.id))
                    definitions.Add(nested.Clone());
            }
        }

        private static void RemapInlineSubgraphIds(List<PcgGraphNodeRecord> nodes, string assetDefinitionId)
        {
            if (nodes == null)
                return;
            foreach (var node in nodes)
            {
                if (node == null || node.type != PcgStructuralNodeTypes.Subgraph)
                    continue;
                var localId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (string.IsNullOrEmpty(localId) || localId.StartsWith("__ext_", StringComparison.Ordinal))
                    continue;
                node.data.SetRaw("subgraphId", assetDefinitionId + "__" + localId);
            }
        }

        private static bool InterfacesCompatible(
            PcgSubgraphInterfaceSnapshot snapshot,
            PcgSubgraphInterfaceSnapshot source,
            List<PcgGraphEdgeRecord> scopeEdges,
            string nodeId,
            out string mismatch)
        {
            mismatch = null;
            if (snapshot == null)
            {
                mismatch = "missing persisted snapshot";
                return false;
            }

            if (!PortsCompatible(
                    snapshot.inputs,
                    source.inputs,
                    scopeEdges,
                    nodeId,
                    input: true,
                    mismatch: out mismatch))
                return false;
            if (!PortsCompatible(
                    snapshot.outputs,
                    source.outputs,
                    scopeEdges,
                    nodeId,
                    input: false,
                    mismatch: out mismatch))
                return false;
            return true;
        }

        private static bool PortsCompatible(
            List<PcgSubgraphPort> snapshotPorts,
            List<PcgSubgraphPort> sourcePorts,
            List<PcgGraphEdgeRecord> scopeEdges,
            string nodeId,
            bool input,
            out string mismatch)
        {
            mismatch = null;
            var direction = input ? "input" : "output";
            snapshotPorts ??= new List<PcgSubgraphPort>();
            sourcePorts ??= new List<PcgSubgraphPort>();
            scopeEdges ??= new List<PcgGraphEdgeRecord>();

            var sourceById = new Dictionary<string, PcgSubgraphPort>(StringComparer.Ordinal);
            foreach (var port in sourcePorts)
            {
                if (port == null || string.IsNullOrEmpty(port.id))
                {
                    mismatch = $"{direction} port id is empty in source";
                    return false;
                }
                if (!sourceById.TryAdd(port.id, port))
                {
                    mismatch = $"duplicate source {direction} port id '{port.id}'";
                    return false;
                }
            }

            foreach (var port in snapshotPorts)
            {
                if (port == null || string.IsNullOrEmpty(port.id))
                {
                    mismatch = $"{direction} snapshot port id is empty";
                    return false;
                }

                if (!sourceById.TryGetValue(port.id, out var sourcePort))
                {
                    // Unconnected stale snapshot ports are harmless and may be dropped by the
                    // Editor's model-level reconcile. Connected ghost ports still fail closed
                    // so a removed interface can never silently rewire authoring data.
                    var connected = scopeEdges.Any(edge =>
                        edge != null &&
                        (input
                            ? edge.target == nodeId && edge.targetHandle == port.id
                            : edge.source == nodeId && edge.sourceHandle == port.id));
                    if (!connected)
                        continue;

                    mismatch = $"source removed {direction} port '{port.id}'";
                    return false;
                }

                if (!string.Equals(port.pinType ?? "Any", sourcePort.pinType ?? "Any", StringComparison.Ordinal))
                {
                    var connected = scopeEdges.Any(edge =>
                        edge != null &&
                        (input
                            ? edge.target == nodeId && edge.targetHandle == port.id
                            : edge.source == nodeId && edge.sourceHandle == port.id));
                    if (!connected)
                        continue;

                    mismatch = $"{direction} port '{port.id}' pinType changed ({port.pinType} -> {sourcePort.pinType})";
                    return false;
                }
            }

            return true;
        }

        private static PcgSubgraphPort ClonePort(PcgSubgraphPort port)
        {
            if (port == null)
                return null;
            return new PcgSubgraphPort
            {
                id = port.id,
                name = port.name,
                pinType = port.pinType,
            };
        }

        private static string BuildChain(List<string> stack, string instancePath, string nodeId)
        {
            var sb = new StringBuilder();
            if (stack != null && stack.Count > 0)
            {
                sb.Append(string.Join(" -> ", stack));
                sb.Append(" | ");
            }
            sb.Append(instancePath).Append('/').Append(nodeId);
            return sb.ToString();
        }
    }
}
