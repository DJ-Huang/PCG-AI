using System.Collections.Generic;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using NUnit.Framework;

namespace DJTechEditor.PCG.Tests
{
  public sealed class PcgSubgraphParentRefTests
    {
        [Test]
        public void Flatten_ResolvesSubgraphParentRef_FromParentScope()
        {
            var doc = new PcgGraphDocument
            {
                version = "1.0",
                nodes = new List<PcgGraphNodeRecord>
                {
                    new PcgGraphNodeRecord
                    {
                        id = "source",
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
                        id = "sink",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges = new List<PcgGraphEdgeRecord>
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e1",
                        source = "inst",
                        target = "sink",
                        sourceHandle = "out",
                        targetHandle = "in",
                    },
                },
                subgraphs = new List<PcgSubgraphDefinition>
                {
                    new PcgSubgraphDefinition
                    {
                        id = "inner",
                        name = "Inner",
                        outputs = new List<PcgSubgraphPort>
                        {
                            new PcgSubgraphPort { id = "out", name = "Out", pinType = "SpatialMesh" },
                        },
                        nodes = new List<PcgGraphNodeRecord>
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "pref",
                                type = PcgStructuralNodeTypes.SubgraphParentRef,
                                data = MakeParentRef("source", "out"),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "out",
                                type = PcgStructuralNodeTypes.SubgraphOutput,
                                data = new PcgNodeData(),
                            },
                        },
                        edges = new List<PcgGraphEdgeRecord>
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "ie1",
                                source = "pref",
                                target = "out",
                                sourceHandle = "out",
                                targetHandle = "out",
                            },
                        },
                    },
                },
            };

            Assert.That(PcgGraphFlattener.TryFlattenForExecution(doc, out var flat, out var error), Is.True, error);
            Assert.That(flat.nodes.Exists(n => n.id == "source"), Is.True);
            Assert.That(flat.nodes.Exists(n => n.id == "inst/pref"), Is.False);
            Assert.That(flat.nodes.Exists(n => n.type == PcgStructuralNodeTypes.SubgraphParentRef), Is.False);
            Assert.That(
                flat.edges.Exists(e => e.source == "source" && e.target == "sink"),
                Is.True);
        }

        [Test]
        public void InterfaceUtility_AddInputPort_CreatesSubgraphInputNode()
        {
            var definition = new PcgSubgraphDefinition
            {
                id = "sg",
                name = "SG",
                nodes = new List<PcgGraphNodeRecord>(),
                edges = new List<PcgGraphEdgeRecord>(),
            };

            PcgSubgraphInterfaceUtility.AddInputPort(definition, "lots", "SpatialMesh", "Lots");
            PcgSubgraphInterfaceUtility.EnsureInterfaceNodes(definition);

            Assert.That(definition.inputs.Count, Is.EqualTo(1));
            Assert.That(
                definition.nodes.Exists(n => n.type == PcgStructuralNodeTypes.SubgraphInput),
                Is.True);
        }

        private static PcgNodeData MakeSubgraphId(string id)
        {
            var data = new PcgNodeData();
            data.SetRaw("subgraphId", id);
            return data;
        }

        private static PcgNodeData MakeParentRef(string parentNodeId, string parentHandle)
        {
            var data = new PcgNodeData();
            data.SetRaw("parentNodeId", parentNodeId);
            data.SetRaw("parentHandle", parentHandle);
            return data;
        }
    }
}
