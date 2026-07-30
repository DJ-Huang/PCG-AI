using System;
using System.Collections.Generic;
using System.Linq;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Host-side flattener mirroring <c>pcg-core</c> <c>expand_scope</c>/<c>flatten_subgraphs</c>.
    /// Produces a self-contained version 2.0 document with no structural nodes or definitions.
    /// </summary>
    public static class PcgGraphFlattener
    {
        public const int MaxFlatNodes = 100000;

        private readonly struct Endpoint
        {
            public readonly string Node;
            public readonly string Handle;

            public Endpoint(string node, string handle)
            {
                Node = node;
                Handle = handle;
            }
        }

        private sealed class ParentScopeContext
        {
            public List<PcgGraphNodeRecord> Nodes;
            public List<PcgGraphEdgeRecord> Edges;
            public string Prefix;
            public Dictionary<string, ExpandedScope> Instances;

            public static ParentScopeContext None => new ParentScopeContext
            {
                Nodes = null,
                Edges = null,
                Prefix = "",
                Instances = null,
            };
        }

        private sealed class ExpandedScope
        {
            public List<PcgGraphNodeRecord> Nodes = new();
            public List<PcgGraphEdgeRecord> Edges = new();
            public Dictionary<string, List<Endpoint>> InputTargets = new(StringComparer.Ordinal);
            public Dictionary<string, List<Endpoint>> OutputSources = new(StringComparer.Ordinal);
            public Dictionary<string, List<string>> PassthroughInputsByOutput = new(StringComparer.Ordinal);
            public HashSet<string> DeclaredInputs = new(StringComparer.Ordinal);
        }

        public static bool TryFlattenForExecution(
            PcgGraphDocument resolved,
            out PcgGraphDocument flat,
            out string error)
        {
            return TryFlattenForExecution(resolved, out flat, out error, out _);
        }

        /// <param name="outputStatsAliases">
        /// Root-scope Subgraph instance node id → flat node id that sourced the instance's
        /// first output port. Used by the editor info panel: a Subgraph instance is flattened
        /// away, so the cook result never carries stats under the instance id (Houdini shows
        /// the subnet output's geometry info on the subnet node itself).
        /// </param>
        public static bool TryFlattenForExecution(
            PcgGraphDocument resolved,
            out PcgGraphDocument flat,
            out string error,
            out Dictionary<string, string> outputStatsAliases)
        {
            flat = null;
            error = null;
            outputStatsAliases = new Dictionary<string, string>(StringComparer.Ordinal);
            if (resolved == null)
            {
                error = "Resolved document is null.";
                return false;
            }

            if (resolved.HasExternalSubgraphAssets())
            {
                error = "Cannot flatten document that still contains SubgraphAsset nodes.";
                return false;
            }

            PcgSubgraphInterfaceRepair.RepairDefinitionsForExecution(resolved.subgraphs);

            var definitions = new Dictionary<string, PcgSubgraphDefinition>(StringComparer.Ordinal);
            foreach (var definition in resolved.subgraphs ?? Enumerable.Empty<PcgSubgraphDefinition>())
            {
                if (definition == null || string.IsNullOrEmpty(definition.id))
                {
                    error = "Subgraph definition id is empty.";
                    return false;
                }
                if (!definitions.TryAdd(definition.id, definition))
                {
                    error = "Duplicate subgraph id: " + definition.id;
                    return false;
                }
            }

            if (definitions.Count == 0 &&
                (resolved.nodes == null ||
                 resolved.nodes.All(node =>
                     node == null ||
                     (node.type != PcgStructuralNodeTypes.Subgraph &&
                      node.type != PcgStructuralNodeTypes.SubgraphInput &&
                      node.type != PcgStructuralNodeTypes.SubgraphOutput &&
                      node.type != PcgStructuralNodeTypes.SubgraphParentRef))))
            {
                flat = resolved.Clone();
                flat.version = "2.0";
                flat.subgraphs = new List<PcgSubgraphDefinition>();
                return true;
            }

            var stack = new List<string>();
            var rootInstances = new Dictionary<string, ExpandedScope>(StringComparer.Ordinal);
            if (!ExpandScope(
                    resolved.nodes ?? new List<PcgGraphNodeRecord>(),
                    resolved.edges ?? new List<PcgGraphEdgeRecord>(),
                    prefix: "",
                    definitions,
                    stack,
                    ParentScopeContext.None,
                    null,
                    rootInstances,
                    out var expanded,
                    out error))
                return false;

            CollectOutputStatsAliases(resolved, definitions, rootInstances, outputStatsAliases);

            if (expanded.Nodes.Count > MaxFlatNodes)
            {
                error = $"Flattened node count exceeded {MaxFlatNodes}.";
                return false;
            }

            if (!ValidateUniqueIds(expanded, out error))
                return false;

            flat = new PcgGraphDocument
            {
                version = "2.0",
                nodes = expanded.Nodes,
                edges = expanded.Edges,
                parameters = resolved.parameters?.Select(CloneParameter).ToList() ?? new List<PcgGraphParameter>(),
                subgraphs = new List<PcgSubgraphDefinition>(),
            };
            return true;
        }

        private static bool ExpandScope(
            List<PcgGraphNodeRecord> nodes,
            List<PcgGraphEdgeRecord> edges,
            string prefix,
            Dictionary<string, PcgSubgraphDefinition> definitions,
            List<string> stack,
            ParentScopeContext parentContext,
            PcgSubgraphDefinition scopeDefinition,
            Dictionary<string, ExpandedScope> rootInstances,
            out ExpandedScope output,
            out string error)
        {
            output = new ExpandedScope();
            error = null;

            var byId = new Dictionary<string, PcgGraphNodeRecord>(StringComparer.Ordinal);
            var instances = new Dictionary<string, ExpandedScope>(StringComparer.Ordinal);

            foreach (var node in nodes)
            {
                if (node == null || string.IsNullOrEmpty(node.id))
                {
                    error = "Node id is empty while flattening.";
                    return false;
                }

                byId[node.id] = node;
                if (IsStructuralInterfaceNode(node.type) ||
                    (scopeDefinition != null && node.type == "Output"))
                    continue;

                if (node.type != PcgStructuralNodeTypes.Subgraph)
                {
                    var flatNode = node.Clone();
                    flatNode.id = prefix + node.id;
                    flatNode.subgraphInterface = null;
                    output.Nodes.Add(flatNode);
                    continue;
                }

                var subgraphId = node.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (string.IsNullOrEmpty(subgraphId) || !definitions.TryGetValue(subgraphId, out var definition))
                {
                    error = "Subgraph instance references missing definition: " + subgraphId;
                    return false;
                }

                if (stack.Contains(subgraphId))
                {
                    error = "Recursive subgraph reference detected: " + subgraphId;
                    return false;
                }

                stack.Add(subgraphId);
                var childParent = new ParentScopeContext
                {
                    Nodes = nodes,
                    Edges = edges,
                    Prefix = prefix,
                    Instances = instances,
                };
                if (!ExpandScope(
                        definition.nodes ?? new List<PcgGraphNodeRecord>(),
                        definition.edges ?? new List<PcgGraphEdgeRecord>(),
                        prefix + node.id + "/",
                        definitions,
                        stack,
                        childParent,
                        definition,
                        rootInstances,
                        out var child,
                        out error))
                    return false;
                stack.RemoveAt(stack.Count - 1);

                output.Nodes.AddRange(child.Nodes);
                output.Edges.AddRange(child.Edges);
                foreach (var input in definition.inputs ?? Enumerable.Empty<PcgSubgraphPort>())
                {
                    if (input != null && !string.IsNullOrEmpty(input.id))
                        child.DeclaredInputs.Add(input.id);
                }
                instances[node.id] = child;
                if (prefix.Length == 0)
                    rootInstances[node.id] = child;
            }

            var edgeCounter = 0;
            foreach (var edge in edges ?? Enumerable.Empty<PcgGraphEdgeRecord>())
            {
                if (edge == null)
                    continue;
                if (!byId.TryGetValue(edge.source, out var sourceNode) ||
                    !byId.TryGetValue(edge.target, out var targetNode))
                {
                    error = "Subgraph edge endpoint not found";
                    return false;
                }

                if (sourceNode.type == PcgStructuralNodeTypes.SubgraphInput &&
                    IsScopeOutputNode(targetNode, scopeDefinition))
                {
                    var outputHandle = ScopeOutputHandle(scopeDefinition, edge.targetHandle);
                    if (!output.PassthroughInputsByOutput.TryGetValue(outputHandle, out var inputs))
                    {
                        inputs = new List<string>();
                        output.PassthroughInputsByOutput[outputHandle] = inputs;
                    }
                    if (!inputs.Contains(edge.sourceHandle))
                        inputs.Add(edge.sourceHandle);
                    continue;
                }

                var sources = SourceEndpoints(
                    edge,
                    sourceNode,
                    prefix,
                    byId,
                    edges,
                    instances,
                    parentContext,
                    new HashSet<string>(StringComparer.Ordinal),
                    out error);
                if (error != null)
                    return false;
                var targets = TargetEndpoints(edge, targetNode, prefix, instances);

                if (sourceNode.type == PcgStructuralNodeTypes.SubgraphInput)
                {
                    if (targets.Count == 0)
                    {
                        if (targetNode.type == PcgStructuralNodeTypes.Subgraph &&
                            instances.TryGetValue(edge.target, out var targetInstance) &&
                            targetInstance.DeclaredInputs.Contains(edge.targetHandle))
                            continue;
                        error = "Subgraph input is not connected to an executable node";
                        return false;
                    }
                    if (!output.InputTargets.TryGetValue(edge.sourceHandle, out var list))
                    {
                        list = new List<Endpoint>();
                        output.InputTargets[edge.sourceHandle] = list;
                    }
                    list.AddRange(targets);
                    continue;
                }

                if (IsScopeOutputNode(targetNode, scopeDefinition))
                {
                    if (sources.Count == 0)
                    {
                        error = "Subgraph output is not connected from an executable node";
                        return false;
                    }
                    var outputHandle = ScopeOutputHandle(scopeDefinition, edge.targetHandle);
                    if (!output.OutputSources.TryGetValue(outputHandle, out var list))
                    {
                        list = new List<Endpoint>();
                        output.OutputSources[outputHandle] = list;
                    }
                    list.AddRange(sources);
                    continue;
                }

                if (sources.Count == 0 || targets.Count == 0)
                {
                    if (targets.Count == 0 &&
                        targetNode.type == PcgStructuralNodeTypes.Subgraph &&
                        instances.TryGetValue(edge.target, out var targetInstance) &&
                        targetInstance.DeclaredInputs.Contains(edge.targetHandle))
                        continue;
                    error = "Subgraph edge resolves to an empty interface";
                    return false;
                }

                foreach (var source in sources)
                {
                    foreach (var target in targets)
                    {
                        edgeCounter++;
                        var baseId = string.IsNullOrEmpty(edge.id) ? "edge" : edge.id;
                        output.Edges.Add(new PcgGraphEdgeRecord
                        {
                            id = prefix + baseId + "__" + edgeCounter,
                            source = source.Node,
                            target = target.Node,
                            sourceHandle = source.Handle,
                            targetHandle = target.Handle,
                            sourcePinType = edge.sourcePinType,
                            targetPinType = edge.targetPinType,
                        });
                    }
                }
            }

            return true;
        }

        /// <summary>
        /// Resolves each root-scope Subgraph instance's first output port to the flat node
        /// that feeds it (direct output source or passthrough through the instance inputs).
        /// Best-effort: instances whose output cannot be resolved are skipped.
        /// </summary>
        private static void CollectOutputStatsAliases(
            PcgGraphDocument resolved,
            Dictionary<string, PcgSubgraphDefinition> definitions,
            Dictionary<string, ExpandedScope> rootInstances,
            Dictionary<string, string> outputStatsAliases)
        {
            if (rootInstances == null || rootInstances.Count == 0)
                return;

            var rootNodes = resolved.nodes ?? new List<PcgGraphNodeRecord>();
            var rootEdges = resolved.edges ?? new List<PcgGraphEdgeRecord>();
            var byId = new Dictionary<string, PcgGraphNodeRecord>(StringComparer.Ordinal);
            foreach (var node in rootNodes)
            {
                if (node != null && !string.IsNullOrEmpty(node.id))
                    byId[node.id] = node;
            }

            foreach (var pair in rootInstances)
            {
                if (!byId.TryGetValue(pair.Key, out var instanceNode))
                    continue;

                var subgraphId = instanceNode.data?.GetRaw("subgraphId")?.ToString() ?? "";
                if (!definitions.TryGetValue(subgraphId, out var definition))
                    continue;

                var outputHandle = definition.outputs?
                    .FirstOrDefault(port => port != null && !string.IsNullOrEmpty(port.id))?.id;
                if (string.IsNullOrEmpty(outputHandle))
                    outputHandle = "out";

                var synthetic = new PcgGraphEdgeRecord
                {
                    source = pair.Key,
                    sourceHandle = outputHandle,
                };
                var endpoints = SourceEndpoints(
                    synthetic,
                    instanceNode,
                    prefix: "",
                    byId,
                    rootEdges,
                    rootInstances,
                    ParentScopeContext.None,
                    new HashSet<string>(StringComparer.Ordinal),
                    out _);
                if (endpoints == null || endpoints.Count == 0)
                    continue;

                outputStatsAliases[pair.Key] = endpoints[0].Node;
            }
        }

        private static bool IsStructuralInterfaceNode(string type) =>
            type == PcgStructuralNodeTypes.SubgraphInput ||
            type == PcgStructuralNodeTypes.SubgraphOutput ||
            type == PcgStructuralNodeTypes.SubgraphParentRef;

        private static bool IsScopeOutputNode(
            PcgGraphNodeRecord node,
            PcgSubgraphDefinition scopeDefinition)
        {
            if (node == null || scopeDefinition == null)
                return false;
            return node.type == "Output" ||
                   node.type == PcgStructuralNodeTypes.SubgraphOutput;
        }

        private static string ScopeOutputHandle(
            PcgSubgraphDefinition scopeDefinition,
            string legacyHandle)
        {
            var portId = scopeDefinition?.outputs?
                .FirstOrDefault(port => port != null && !string.IsNullOrEmpty(port.id))?.id;
            return string.IsNullOrEmpty(portId) ? legacyHandle ?? "out_1" : portId;
        }

        private static List<Endpoint> SourceEndpoints(
            PcgGraphEdgeRecord edge,
            PcgGraphNodeRecord sourceNode,
            string prefix,
            Dictionary<string, PcgGraphNodeRecord> byId,
            List<PcgGraphEdgeRecord> scopeEdges,
            Dictionary<string, ExpandedScope> instances,
            ParentScopeContext parentContext,
            HashSet<string> resolvingPassthroughs,
            out string error)
        {
            error = null;
            if (sourceNode.type == PcgStructuralNodeTypes.SubgraphInput ||
                sourceNode.type == PcgStructuralNodeTypes.SubgraphOutput)
                return new List<Endpoint>();

            if (sourceNode.type == PcgStructuralNodeTypes.SubgraphParentRef)
            {
                if (!TryResolveParentRefEndpoints(sourceNode, parentContext, out var endpoints, out error))
                    return new List<Endpoint>();
                return endpoints;
            }

            if (sourceNode.type == PcgStructuralNodeTypes.Subgraph)
            {
                if (!instances.TryGetValue(edge.source, out var instance))
                    return new List<Endpoint>();

                var endpoints = instance.OutputSources.TryGetValue(edge.sourceHandle, out var list)
                    ? new List<Endpoint>(list)
                    : new List<Endpoint>();
                if (!instance.PassthroughInputsByOutput.TryGetValue(edge.sourceHandle, out var inputHandles))
                    return endpoints;

                var resolvingKey = edge.source + "\u001f" + edge.sourceHandle;
                if (!resolvingPassthroughs.Add(resolvingKey))
                {
                    error = "Subgraph passthrough cycle detected: " + edge.source + "/" + edge.sourceHandle;
                    return new List<Endpoint>();
                }

                foreach (var inputHandle in inputHandles)
                {
                    foreach (var incoming in scopeEdges ?? Enumerable.Empty<PcgGraphEdgeRecord>())
                    {
                        if (incoming == null ||
                            incoming.target != edge.source ||
                            incoming.targetHandle != inputHandle ||
                            !byId.TryGetValue(incoming.source, out var incomingSource))
                            continue;

                        var resolved = SourceEndpoints(
                            incoming,
                            incomingSource,
                            prefix,
                            byId,
                            scopeEdges,
                            instances,
                            parentContext,
                            resolvingPassthroughs,
                            out error);
                        if (error != null)
                            return new List<Endpoint>();
                        endpoints.AddRange(resolved);
                    }
                }

                resolvingPassthroughs.Remove(resolvingKey);
                return endpoints;
            }

            return new List<Endpoint> { new(prefix + edge.source, edge.sourceHandle) };
        }

        private static bool TryResolveParentRefEndpoints(
            PcgGraphNodeRecord refNode,
            ParentScopeContext parentContext,
            out List<Endpoint> endpoints,
            out string error)
        {
            endpoints = new List<Endpoint>();
            error = null;

            if (parentContext?.Nodes == null)
            {
                error = "SubgraphParentRef is only valid inside a subgraph definition.";
                return false;
            }

            var parentNodeId = refNode.data?.GetRaw("parentNodeId")?.ToString() ?? "";
            if (string.IsNullOrEmpty(parentNodeId))
            {
                error = "SubgraphParentRef missing parentNodeId.";
                return false;
            }

            var parentHandle = refNode.data?.GetRaw("parentHandle")?.ToString();
            if (string.IsNullOrEmpty(parentHandle))
                parentHandle = "out";

            var parentNode = parentContext.Nodes.FirstOrDefault(n => n.id == parentNodeId);
            if (parentNode == null)
            {
                error = "SubgraphParentRef parent node not found: " + parentNodeId;
                return false;
            }

            if (parentNode.type == PcgStructuralNodeTypes.SubgraphInput ||
                parentNode.type == PcgStructuralNodeTypes.SubgraphOutput ||
                parentNode.type == PcgStructuralNodeTypes.SubgraphParentRef)
            {
                error = "SubgraphParentRef cannot reference structural parent node: " + parentNodeId;
                return false;
            }

            if (parentNode.type == PcgStructuralNodeTypes.Subgraph)
            {
                if (parentContext.Instances == null ||
                    !parentContext.Instances.TryGetValue(parentNodeId, out var instance))
                {
                    error = "SubgraphParentRef parent subgraph instance is not expanded: " + parentNodeId;
                    return false;
                }

                if (!instance.OutputSources.TryGetValue(parentHandle, out var list) || list.Count == 0)
                {
                    error = "SubgraphParentRef parent subgraph output is not connected: " + parentNodeId + "/" + parentHandle;
                    return false;
                }

                endpoints.AddRange(list);
                return true;
            }

            endpoints.Add(new Endpoint(parentContext.Prefix + parentNodeId, parentHandle));
            return true;
        }

        private static List<Endpoint> TargetEndpoints(
            PcgGraphEdgeRecord edge,
            PcgGraphNodeRecord targetNode,
            string prefix,
            Dictionary<string, ExpandedScope> instances)
        {
            if (targetNode.type == PcgStructuralNodeTypes.SubgraphInput ||
                targetNode.type == PcgStructuralNodeTypes.SubgraphOutput ||
                targetNode.type == PcgStructuralNodeTypes.SubgraphParentRef)
                return new List<Endpoint>();

            if (targetNode.type == PcgStructuralNodeTypes.Subgraph)
            {
                if (!instances.TryGetValue(edge.target, out var instance))
                    return new List<Endpoint>();
                return instance.InputTargets.TryGetValue(edge.targetHandle, out var list)
                    ? new List<Endpoint>(list)
                    : new List<Endpoint>();
            }

            return new List<Endpoint> { new(prefix + edge.target, edge.targetHandle) };
        }

        private static bool ValidateUniqueIds(ExpandedScope expanded, out string error)
        {
            error = null;
            var nodeIds = new HashSet<string>(StringComparer.Ordinal);
            foreach (var node in expanded.Nodes)
            {
                if (!nodeIds.Add(node.id))
                {
                    error = "Flattened node id collision: " + node.id;
                    return false;
                }
            }

            var edgeIds = new HashSet<string>(StringComparer.Ordinal);
            foreach (var edge in expanded.Edges)
            {
                if (string.IsNullOrEmpty(edge.id))
                    continue;
                if (!edgeIds.Add(edge.id))
                {
                    error = "Flattened edge id collision: " + edge.id;
                    return false;
                }
            }

            return true;
        }

        private static PcgGraphParameter CloneParameter(PcgGraphParameter parameter)
        {
            if (parameter == null)
                return null;
            return new PcgGraphParameter
            {
                id = parameter.id,
                name = parameter.name,
                type = parameter.type,
                defaultValue = parameter.defaultValue,
                exposed = parameter.exposed,
                targetNode = parameter.targetNode,
                targetProperty = parameter.targetProperty,
                hasRange = parameter.hasRange,
                minValue = parameter.minValue,
                maxValue = parameter.maxValue,
            };
        }
    }
}
