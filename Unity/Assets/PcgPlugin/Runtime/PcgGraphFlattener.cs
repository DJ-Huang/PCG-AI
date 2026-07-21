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

        private sealed class ExpandedScope
        {
            public List<PcgGraphNodeRecord> Nodes = new();
            public List<PcgGraphEdgeRecord> Edges = new();
            public Dictionary<string, List<Endpoint>> InputTargets = new(StringComparer.Ordinal);
            public Dictionary<string, List<Endpoint>> OutputSources = new(StringComparer.Ordinal);
        }

        public static bool TryFlattenForExecution(
            PcgGraphDocument resolved,
            out PcgGraphDocument flat,
            out string error)
        {
            flat = null;
            error = null;
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
                      node.type != PcgStructuralNodeTypes.SubgraphOutput))))
            {
                flat = resolved.Clone();
                flat.version = "2.0";
                flat.subgraphs = new List<PcgSubgraphDefinition>();
                return true;
            }

            var stack = new List<string>();
            if (!ExpandScope(
                    resolved.nodes ?? new List<PcgGraphNodeRecord>(),
                    resolved.edges ?? new List<PcgGraphEdgeRecord>(),
                    prefix: "",
                    definitions,
                    stack,
                    out var expanded,
                    out error))
                return false;

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
                if (node.type == PcgStructuralNodeTypes.SubgraphInput ||
                    node.type == PcgStructuralNodeTypes.SubgraphOutput)
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
                if (!ExpandScope(
                        definition.nodes ?? new List<PcgGraphNodeRecord>(),
                        definition.edges ?? new List<PcgGraphEdgeRecord>(),
                        prefix + node.id + "/",
                        definitions,
                        stack,
                        out var child,
                        out error))
                    return false;
                stack.RemoveAt(stack.Count - 1);

                output.Nodes.AddRange(child.Nodes);
                output.Edges.AddRange(child.Edges);
                instances[node.id] = child;
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

                var sources = SourceEndpoints(edge, sourceNode, prefix, instances);
                var targets = TargetEndpoints(edge, targetNode, prefix, instances);

                if (sourceNode.type == PcgStructuralNodeTypes.SubgraphInput)
                {
                    if (targets.Count == 0)
                    {
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

                if (targetNode.type == PcgStructuralNodeTypes.SubgraphOutput)
                {
                    if (sources.Count == 0)
                    {
                        error = "Subgraph output is not connected from an executable node";
                        return false;
                    }
                    if (!output.OutputSources.TryGetValue(edge.targetHandle, out var list))
                    {
                        list = new List<Endpoint>();
                        output.OutputSources[edge.targetHandle] = list;
                    }
                    list.AddRange(sources);
                    continue;
                }

                if (sources.Count == 0 || targets.Count == 0)
                {
                    error = "Subgraph edge resolves to an empty interface";
                    return false;
                }

                foreach (var source in sources)
                {
                    foreach (var target in targets)
                    {
                        edgeCounter++;
                        output.Edges.Add(new PcgGraphEdgeRecord
                        {
                            id = prefix + (string.IsNullOrEmpty(edge.id) ? "edge" + edgeCounter : edge.id),
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

        private static List<Endpoint> SourceEndpoints(
            PcgGraphEdgeRecord edge,
            PcgGraphNodeRecord sourceNode,
            string prefix,
            Dictionary<string, ExpandedScope> instances)
        {
            if (sourceNode.type == PcgStructuralNodeTypes.SubgraphInput ||
                sourceNode.type == PcgStructuralNodeTypes.SubgraphOutput)
                return new List<Endpoint>();

            if (sourceNode.type == PcgStructuralNodeTypes.Subgraph)
            {
                if (!instances.TryGetValue(edge.source, out var instance))
                    return new List<Endpoint>();
                return instance.OutputSources.TryGetValue(edge.sourceHandle, out var list)
                    ? new List<Endpoint>(list)
                    : new List<Endpoint>();
            }

            return new List<Endpoint> { new(prefix + edge.source, edge.sourceHandle) };
        }

        private static List<Endpoint> TargetEndpoints(
            PcgGraphEdgeRecord edge,
            PcgGraphNodeRecord targetNode,
            string prefix,
            Dictionary<string, ExpandedScope> instances)
        {
            if (targetNode.type == PcgStructuralNodeTypes.SubgraphInput ||
                targetNode.type == PcgStructuralNodeTypes.SubgraphOutput)
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
