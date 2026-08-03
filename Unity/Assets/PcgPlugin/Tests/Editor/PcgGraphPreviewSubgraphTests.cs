using System.Collections.Generic;
using System.Linq;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using NUnit.Framework;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgGraphPreviewSubgraphTests
    {
        [Test]
        public void ResolveDefaultOutputNode_PrefersConnectedRootOutput()
        {
            var disconnected = new PcgGraphNodeRecord { id = "out_empty", type = "Output" };
            var connected = new PcgGraphNodeRecord { id = "out_final", type = "Output" };
            var document = new PcgGraphDocument
            {
                nodes =
                {
                    disconnected,
                    new PcgGraphNodeRecord { id = "mesh", type = "CreateBoxMesh" },
                    connected,
                },
                edges =
                {
                    new PcgGraphEdgeRecord { id = "e1", source = "mesh", target = "out_final" },
                },
            };

            Assert.That(
                PcgGraphEditorWindow.ResolveDefaultOutputNode(document),
                Is.SameAs(connected));
        }

        [Test]
        public void ResolveDefaultOutputNode_WithoutOutput_ReturnsNull()
        {
            var document = new PcgGraphDocument
            {
                nodes = { new PcgGraphNodeRecord { id = "mesh", type = "CreateBoxMesh" } },
            };

            Assert.That(PcgGraphEditorWindow.ResolveDefaultOutputNode(document), Is.Null);
        }

        [Test]
        public void PreviewCook_SubgraphOutputInsideForEach_KeepsPreviewSinkForOpenForEach()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "foreach_begin",
                        type = "ForEachBegin",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("inner"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "foreach_end",
                        type = "ForEachEnd",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_begin_inst",
                        source = "foreach_begin",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_end",
                        source = "inst",
                        target = "foreach_end",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_end_out",
                        source = "foreach_end",
                        target = "root_out",
                        sourceHandle = "out",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "inner",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "box",
                                type = "CreateBoxMesh",
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_box",
                                source = "input",
                                target = "box",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "e_box_out",
                                source = "box",
                                target = "inner_out",
                                sourceHandle = "out",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDoc,
                    "inner_out",
                    "inner",
                    new[] { "inst" },
                    out var cookDoc,
                    out var previewError),
                Is.True,
                previewError);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == PcgGraphPreviewSubgraph.PreviewSinkNodeId),
                Is.True);
            Assert.That(
                cookDoc.nodes.Any(node => node.type == "Output" && node.id != PcgGraphPreviewSubgraph.PreviewSinkNodeId),
                Is.False,
                "authoring Output must not remain as the execution sink");

            Assert.That(
                PcgExecutionDocumentBuilder.TryBuild(cookDoc, null, out var flat, out _, out var buildError),
                Is.True,
                buildError);
            Assert.That(
                flat.nodes.Any(node => node.id == PcgGraphPreviewSubgraph.PreviewSinkNodeId),
                Is.True);
            Assert.That(flat.nodes.Any(node => node.type == "ForEachBegin"), Is.True);
            Assert.That(flat.nodes.Any(node => node.type == "ForEachEnd"), Is.False);
        }

        [Test]
        public void PreviewCook_PassthroughSubgraphOutput_GraftsParentUpstream()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "parent_box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("windows_floor"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_parent_inst",
                        source = "parent_box",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_out",
                        source = "inst",
                        target = "root_out",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "windows_floor",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "subgraph_input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_out",
                                source = "subgraph_input",
                                target = "inner_out",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDoc,
                    "inner_out",
                    "windows_floor",
                    new[] { "inst" },
                    out var cookDoc,
                    out var previewError),
                Is.True,
                previewError);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == PcgGraphPreviewSubgraph.PreviewSinkNodeId),
                Is.True);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == "subgraph_input"),
                Is.False);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == "inner_out"),
                Is.False);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == $"{PcgGraphPreviewSubgraph.PreviewSinkNodeId}" &&
                                          node.type == "Output"),
                Is.True);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == "__pcg_ext__/0/parent_box"),
                Is.True);

            Assert.That(
                PcgExecutionDocumentBuilder.TryBuild(cookDoc, null, out var flat, out _, out var buildError),
                Is.True,
                buildError);
            Assert.That(flat.nodes.Any(node => node.id == PcgGraphPreviewSubgraph.PreviewSinkNodeId), Is.True);
            Assert.That(flat.nodes.Any(node => node.id.Contains("parent_box")), Is.True);
        }

        [Test]
        public void Flatten_PassthroughSubgraphInstance_EmitsOutputStatsAlias()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "parent_box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("windows_floor"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_parent_inst",
                        source = "parent_box",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_out",
                        source = "inst",
                        target = "root_out",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "windows_floor",
                        inputs =
                        {
                            new PcgSubgraphPort { id = "in_1", pinType = "Any" },
                            new PcgSubgraphPort { id = "in_2", pinType = "Any" },
                        },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "subgraph_input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_out",
                                source = "subgraph_input",
                                target = "inner_out",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphFlattener.TryFlattenForExecution(
                    liveDoc,
                    out var flat,
                    out var error,
                    out var aliases),
                Is.True,
                error);
            Assert.That(flat.nodes.Any(node => node.id == "parent_box"), Is.True);
            Assert.That(flat.nodes.Any(node => node.id == "inst"), Is.False);
            Assert.That(
                aliases.TryGetValue("inst", out var aliasTarget) && aliasTarget == "parent_box",
                Is.True,
                "passthrough Subgraph instance must alias its info-panel stats to the upstream source");
        }

        [Test]
        public void Flatten_SubgraphInstanceWithContent_EmitsOutputStatsAliasToInternalSource()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "parent_box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("inner"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_parent_inst",
                        source = "parent_box",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_out",
                        source = "inst",
                        target = "root_out",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "inner",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "subgraph_input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "bevel",
                                type = "BevelMesh",
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_bevel",
                                source = "subgraph_input",
                                target = "bevel",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "e_bevel_out",
                                source = "bevel",
                                target = "inner_out",
                                sourceHandle = "out",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphFlattener.TryFlattenForExecution(
                    liveDoc,
                    out var flat,
                    out var error,
                    out var aliases),
                Is.True,
                error);
            Assert.That(
                aliases.TryGetValue("inst", out var aliasTarget) && aliasTarget == "inst/bevel",
                Is.True,
                "Subgraph instance with content must alias its stats to the flat internal output source");
        }

        [Test]
        public void NodeInfoStats_OutputStatsAliases_BackfillInstanceStats()
        {
            var stats = new Dictionary<string, PcgNodeMeshStats>
            {
                ["parent_box"] = new PcgNodeMeshStats { pointCount = 8, faceCount = 6 },
            };
            var groups = new Dictionary<string, List<NodeGroupEntry>>
            {
                ["parent_box"] = new List<NodeGroupEntry>
                {
                    new NodeGroupEntry { node_id = "parent_box", name = "base", domain = "face", count = 2 },
                },
            };
            var attrs = new Dictionary<string, List<NodeAttrEntry>>
            {
                ["parent_box"] = new List<NodeAttrEntry>
                {
                    new NodeAttrEntry { node_id = "parent_box", name = "numFloors", owner = "detail" },
                },
            };
            var aliases = new Dictionary<string, string> { ["inst"] = "parent_box" };

            PcgGraphView.ApplyOutputStatsAliases(aliases, stats, groups, attrs);

            Assert.That(stats.TryGetValue("inst", out var instStats) && instStats.pointCount == 8, Is.True);
            Assert.That(groups.ContainsKey("inst"), Is.True);
            Assert.That(attrs.ContainsKey("inst"), Is.True);

            // Existing entries must win over aliases.
            var existing = new PcgNodeMeshStats { pointCount = 1 };
            stats["inst"] = existing;
            PcgGraphView.ApplyOutputStatsAliases(aliases, stats, groups, attrs);
            Assert.That(stats["inst"].pointCount, Is.EqualTo(1));
        }

        [Test]
        public void PreviewCook_ScopeOutputPreview_EmitsOutputStatsAliasesToCookedSource()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "parent_box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("inner"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_parent_inst",
                        source = "parent_box",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_out",
                        source = "inst",
                        target = "root_out",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "inner",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "subgraph_input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "bevel",
                                type = "BevelMesh",
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_bevel",
                                source = "subgraph_input",
                                target = "bevel",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "e_bevel_out",
                                source = "bevel",
                                target = "inner_out",
                                sourceHandle = "out",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDoc,
                    "inner_out",
                    "inner",
                    new[] { "inst" },
                    out var cookDoc,
                    out var aliases,
                    out var previewError),
                Is.True,
                previewError);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == PcgGraphPreviewSubgraph.PreviewSinkNodeId),
                Is.True);
            Assert.That(
                aliases.TryGetValue("inner_out", out var outputTarget) && outputTarget == "bevel",
                Is.True,
                "scope Output node must alias its info-panel stats to the cooked upstream source");
            Assert.That(
                aliases.TryGetValue("iface_Output_out_1", out var anchorTarget) && anchorTarget == "bevel",
                Is.True,
                "interface output anchor must alias its info-panel stats to the cooked upstream source");
        }

        [Test]
        public void PreviewCook_PassthroughScopeOutput_AliasesToGraftedParentSource()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "parent_box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("windows_floor"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_parent_inst",
                        source = "parent_box",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_out",
                        source = "inst",
                        target = "root_out",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "windows_floor",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "subgraph_input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_out",
                                source = "subgraph_input",
                                target = "inner_out",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDoc,
                    "inner_out",
                    "windows_floor",
                    new[] { "inst" },
                    out _,
                    out var aliases,
                    out var previewError),
                Is.True,
                previewError);
            Assert.That(
                aliases.TryGetValue("inner_out", out var outputTarget) &&
                outputTarget == "__pcg_ext__/0/parent_box",
                Is.True,
                "passthrough scope Output must alias to the grafted parent upstream source");
            Assert.That(
                aliases.TryGetValue("iface_Output_out_1", out var anchorTarget) &&
                anchorTarget == "__pcg_ext__/0/parent_box",
                Is.True);
        }

        [Test]
        public void PreviewCook_OutputAnchorPreviewId_ResolvesToHiddenOutputNode()
        {
            var liveDoc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "parent_box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("inner"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "root_out",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_parent_inst",
                        source = "parent_box",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e_inst_out",
                        source = "inst",
                        target = "root_out",
                        sourceHandle = "out_1",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "inner",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "subgraph_input",
                                type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "bevel",
                                type = "BevelMesh",
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "inner_out",
                                type = "Output",
                                data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e_in_bevel",
                                source = "subgraph_input",
                                target = "bevel",
                                sourceHandle = "in_1",
                                targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "e_bevel_out",
                                source = "bevel",
                                target = "inner_out",
                                sourceHandle = "out",
                                targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgGraphPreviewSubgraph.TryBuildPreviewCook(
                    liveDoc,
                    "iface_Output_out_1",
                    "inner",
                    new[] { "inst" },
                    out var cookDoc,
                    out var aliases,
                    out var previewError),
                Is.True,
                previewError);
            Assert.That(
                cookDoc.nodes.Any(node => node.id == PcgGraphPreviewSubgraph.PreviewSinkNodeId),
                Is.True);
            Assert.That(
                aliases.TryGetValue("iface_Output_out_1", out var anchorTarget) && anchorTarget == "bevel",
                Is.True);
        }

        [Test]
        public void NodeInfoStats_OutputStatsAliases_ResolveTransitiveChains()
        {
            var stats = new Dictionary<string, PcgNodeMeshStats>
            {
                ["inst/bevel"] = new PcgNodeMeshStats { pointCount = 8, faceCount = 6 },
            };
            var groups = new Dictionary<string, List<NodeGroupEntry>>
            {
                ["inst/bevel"] = new List<NodeGroupEntry>
                {
                    new NodeGroupEntry { node_id = "inst/bevel", name = "base", domain = "face", count = 2 },
                },
            };
            var attrs = new Dictionary<string, List<NodeAttrEntry>>
            {
                ["inst/bevel"] = new List<NodeAttrEntry>
                {
                    new NodeAttrEntry { node_id = "inst/bevel", name = "numFloors", owner = "detail" },
                },
            };
            var aliases = new Dictionary<string, string>
            {
                ["iface_Output_out_1"] = "inst",
                ["inst"] = "inst/bevel",
            };

            PcgGraphView.ApplyOutputStatsAliases(aliases, stats, groups, attrs);

            Assert.That(
                stats.TryGetValue("iface_Output_out_1", out var anchorStats) && anchorStats.pointCount == 8,
                Is.True,
                "anchor must resolve through the instance alias to the flat internal node stats");
            Assert.That(groups.ContainsKey("iface_Output_out_1"), Is.True);
            Assert.That(attrs.ContainsKey("iface_Output_out_1"), Is.True);
        }

        private static PcgNodeData MakeSubgraphId(string id)
        {
            var data = new PcgNodeData();
            data.SetRaw("subgraphId", id);
            return data;
        }
    }
}
