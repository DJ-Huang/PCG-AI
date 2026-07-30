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

        private static PcgNodeData MakeSubgraphId(string id)
        {
            var data = new PcgNodeData();
            data.SetRaw("subgraphId", id);
            return data;
        }
    }
}
