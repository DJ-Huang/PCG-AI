using System.Collections.Generic;
using DJTechRuntime.PCG;
using NUnit.Framework;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgSubgraphSerializationTests
    {
        [Test]
        public void Graph_V1_RoundTrip_PreservesNodesAndEdges()
        {
            const string json = @"{
  ""version"":""1.0"",
  ""nodes"":[{""id"":""n1"",""type"":""Output"",""position"":{""x"":1,""y"":2},""data"":{}}],
  ""edges"":[],
  ""parameters"":[],
  ""subgraphs"":[]
}";
            Assert.That(PcgGraphSerializer.TryFromJson(json, out var doc, out var error), Is.True, error);
            var again = PcgGraphSerializer.ToJson(doc, pretty: false);
            Assert.That(PcgGraphSerializer.TryFromJson(again, out var doc2, out error), Is.True, error);
            Assert.That(doc2.version, Is.EqualTo("1.0"));
            Assert.That(doc2.nodes[0].id, Is.EqualTo("n1"));
        }

        [Test]
        public void Graph_V3_RoundTrip_PreservesSubgraphAssetSnapshot()
        {
            var doc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "ext1",
                        type = PcgStructuralNodeTypes.SubgraphAsset,
                        position = new PcgGraphPosition { x = 10, y = 20 },
                        data = MakeGuidData("0123456789abcdef0123456789abcdef"),
                        subgraphInterface = new PcgSubgraphInterfaceSnapshot
                        {
                            name = "Linked",
                            inputs = { new PcgSubgraphPort { id = "in_a", name = "In", pinType = "SpatialMesh" } },
                            outputs = { new PcgSubgraphPort { id = "out_a", name = "Out", pinType = "SpatialMesh" } },
                        },
                    },
                },
            };

            var json = PcgGraphSerializer.ToJson(doc, pretty: false);
            Assert.That(json, Does.Contain("subgraphInterface"));
            Assert.That(PcgGraphSerializer.TryFromJson(json, out var parsed, out var error), Is.True, error);
            Assert.That(parsed.version, Is.EqualTo("3.0"));
            Assert.That(parsed.nodes[0].subgraphInterface.name, Is.EqualTo("Linked"));
            Assert.That(parsed.nodes[0].subgraphInterface.inputs[0].id, Is.EqualTo("in_a"));
            Assert.That(parsed.nodes[0].data.GetRaw("assetGuid")?.ToString(),
                Is.EqualTo("0123456789abcdef0123456789abcdef"));
        }

        [Test]
        public void SubgraphAsset_RoundTrip_RejectsParameters()
        {
            var asset = PcgSubgraphAssetDocument.CreateEmpty("Demo");
            asset.inputs.Add(new PcgSubgraphPort { id = "mesh", name = "Mesh", pinType = "SpatialMesh" });
            var json = PcgSubgraphAssetSerializer.ToJson(asset, pretty: false);
            Assert.That(PcgSubgraphAssetSerializer.TryFromJson(json, out var parsed, out var error), Is.True, error);
            Assert.That(parsed.name, Is.EqualTo("Demo"));
            Assert.That(parsed.inputs[0].id, Is.EqualTo("mesh"));

            var withParams = json.TrimEnd('}') + ",\"parameters\":[]}";
            Assert.That(PcgSubgraphAssetSerializer.TryFromJson(withParams, out _, out error), Is.False);
            Assert.That(error, Does.Contain("parameters"));
        }

        private static PcgNodeData MakeGuidData(string guid)
        {
            var data = new PcgNodeData();
            data.SetRaw("assetGuid", guid);
            return data;
        }
    }

    public sealed class PcgExternalSubgraphResolverTests
    {
        [Test]
        public void Resolve_SingleAsset_RewritesToInlineDefinition()
        {
            const string guid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            var asset = new PcgSubgraphAssetDocument
            {
                name = "Box",
                inputs = { new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" } },
                outputs = { new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" } },
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "input", type = PcgStructuralNodeTypes.SubgraphInput,
                        position = new PcgGraphPosition(), data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "output", type = PcgStructuralNodeTypes.SubgraphOutput,
                        position = new PcgGraphPosition(), data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e1", source = "input", target = "output",
                        sourceHandle = "in", targetHandle = "out",
                    },
                },
            };
            var assetJson = PcgSubgraphAssetSerializer.ToJson(asset, pretty: false);

            var authoring = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.SubgraphAsset,
                        data = MakeGuid(guid),
                        subgraphInterface = PcgSubgraphInterfaceSnapshot.FromDefinition(asset.ToRootDefinition()),
                    },
                },
            };

            var loads = 0;
            Assert.That(PcgExternalSubgraphResolver.TryResolveToInline(
                authoring,
                (string g, out string json, out string error) =>
                {
                    loads++;
                    json = assetJson;
                    error = null;
                    return g == guid;
                },
                out var result), Is.True, result?.Error);

            Assert.That(loads, Is.EqualTo(1));
            Assert.That(result.Document.nodes[0].type, Is.EqualTo(PcgStructuralNodeTypes.Subgraph));
            Assert.That(result.Document.subgraphs.Count, Is.EqualTo(1));
            Assert.That(result.DependencyGuids, Is.EquivalentTo(new[] { guid }));
        }

        [Test]
        public void Resolve_Cycle_FailsClosed()
        {
            const string guidA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            const string guidB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

            var assetA = MakePassthroughAsset("A", guidB);
            var assetB = MakePassthroughAsset("B", guidA);
            var map = new Dictionary<string, string>
            {
                [guidA] = PcgSubgraphAssetSerializer.ToJson(assetA, pretty: false),
                [guidB] = PcgSubgraphAssetSerializer.ToJson(assetB, pretty: false),
            };

            var authoring = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.SubgraphAsset,
                        data = MakeGuid(guidA),
                        subgraphInterface = new PcgSubgraphInterfaceSnapshot
                        {
                            name = "A",
                            inputs = { new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" } },
                            outputs = { new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" } },
                        },
                    },
                },
            };

            Assert.That(PcgExternalSubgraphResolver.TryResolveToInline(
                authoring,
                (string g, out string json, out string error) =>
                {
                    error = null;
                    return map.TryGetValue(g, out json);
                },
                out var result), Is.False);
            Assert.That(result.Error, Does.Contain("Recursive"));
        }

        [Test]
        public void Flatten_ProducesInstancePrefixedIds()
        {
            var doc = new PcgGraphDocument
            {
                version = "2.0",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("sg"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "sink",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e_root",
                        source = "inst",
                        target = "sink",
                        sourceHandle = "out",
                        targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "sg",
                        name = "SG",
                        inputs = { new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "input", type = PcgStructuralNodeTypes.SubgraphInput, data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "box", type = "CreateBoxMesh", data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "output", type = PcgStructuralNodeTypes.SubgraphOutput, data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "e1", source = "input", target = "box",
                                sourceHandle = "in", targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "e2", source = "box", target = "output",
                                sourceHandle = "out", targetHandle = "out",
                            },
                        },
                    },
                },
            };

            Assert.That(PcgGraphFlattener.TryFlattenForExecution(doc, out var flat, out var error), Is.True, error);
            Assert.That(flat.version, Is.EqualTo("2.0"));
            Assert.That(flat.subgraphs, Is.Empty);
            Assert.That(flat.nodes.Exists(node => node.id == "inst/box"), Is.True);
            Assert.That(flat.nodes.Exists(node => node.type == PcgStructuralNodeTypes.Subgraph), Is.False);
            Assert.That(flat.nodes.Exists(node => node.id == "sink"), Is.True);
        }

        private static PcgSubgraphAssetDocument MakePassthroughAsset(string name, string nestedGuid)
        {
            var doc = new PcgSubgraphAssetDocument
            {
                name = name,
                inputs = { new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" } },
                outputs = { new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" } },
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "input", type = PcgStructuralNodeTypes.SubgraphInput, data = new PcgNodeData(),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "nested",
                        type = PcgStructuralNodeTypes.SubgraphAsset,
                        data = MakeGuid(nestedGuid),
                        subgraphInterface = new PcgSubgraphInterfaceSnapshot
                        {
                            name = "Nested",
                            inputs = { new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" } },
                            outputs = { new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" } },
                        },
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "output", type = PcgStructuralNodeTypes.SubgraphOutput, data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "e1", source = "input", target = "nested",
                        sourceHandle = "in", targetHandle = "in",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "e2", source = "nested", target = "output",
                        sourceHandle = "out", targetHandle = "out",
                    },
                },
            };
            return doc;
        }

        private static PcgNodeData MakeGuid(string guid)
        {
            var data = new PcgNodeData();
            data.SetRaw("assetGuid", guid);
            return data;
        }

        private static PcgNodeData MakeSubgraphId(string id)
        {
            var data = new PcgNodeData();
            data.SetRaw("subgraphId", id);
            return data;
        }
    }
}
