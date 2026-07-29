using System.Collections.Generic;
using System.Linq;
using DJTechEditor.PCG.Graph;
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
        public void Graph_V2_RoundTrip_PreservesInlineInterfaceAnchorPlacement()
        {
            var doc = new PcgGraphDocument
            {
                version = "2.0",
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "sg",
                        name = "Anchored",
                        inputs =
                        {
                            new PcgSubgraphPort
                            {
                                id = "in",
                                name = "Input",
                                pinType = "SpatialMesh",
                                anchorPlaced = true,
                                anchorX = 12.5f,
                                anchorY = -48f,
                            },
                        },
                    },
                },
            };

            var json = PcgGraphSerializer.ToJson(doc, pretty: false);
            Assert.That(PcgGraphSerializer.TryFromJson(json, out var parsed, out var error), Is.True, error);
            var port = parsed.subgraphs[0].inputs[0];
            Assert.That(port.anchorPlaced, Is.True);
            Assert.That(port.anchorX, Is.EqualTo(12.5f));
            Assert.That(port.anchorY, Is.EqualTo(-48f));
        }

        [Test]
        public void LinkedSubgraphInterfaceSync_AddsNewSourcePortsToParentNodeRecord()
        {
            var node = new PcgGraphNodeRecord
            {
                id = "linked",
                type = PcgStructuralNodeTypes.SubgraphAsset,
                subgraphInterface = new PcgSubgraphInterfaceSnapshot
                {
                    inputs = { new PcgSubgraphPort { id = "in_1", name = "Input 1", pinType = "SpatialMesh" } },
                    outputs = { new PcgSubgraphPort { id = "out_1", name = "Output 1", pinType = "SpatialMesh" } },
                },
            };
            var source = new PcgSubgraphInterfaceSnapshot
            {
                inputs =
                {
                    new PcgSubgraphPort { id = "in_1", name = "Input 1", pinType = "SpatialMesh" },
                    new PcgSubgraphPort { id = "in_2", name = "Input 2", pinType = "SpatialMesh" },
                },
                outputs =
                {
                    new PcgSubgraphPort { id = "out_1", name = "Output 1", pinType = "SpatialMesh" },
                    new PcgSubgraphPort { id = "out_2", name = "Output 2", pinType = "SpatialMesh" },
                },
            };

            var result = PcgExternalSubgraphInterfaceSync.ReconcileNodeRecord(
                node,
                new List<PcgGraphEdgeRecord>(),
                source);

            Assert.That(result.Compatible, Is.True);
            Assert.That(node.subgraphInterface.inputs.ConvertAll(port => port.id),
                Is.EqualTo(new[] { "in_1", "in_2" }));
            Assert.That(node.subgraphInterface.outputs.ConvertAll(port => port.id),
                Is.EqualTo(new[] { "out_1", "out_2" }));
        }

        [Test]
        public void LinkedSubgraphInterfaceSync_PreservesConnectedRemovedPortAsGhost()
        {
            var node = new PcgGraphNodeRecord
            {
                id = "linked",
                type = PcgStructuralNodeTypes.SubgraphAsset,
                subgraphInterface = new PcgSubgraphInterfaceSnapshot
                {
                    inputs =
                    {
                        new PcgSubgraphPort { id = "removed", name = "Removed", pinType = "SpatialMesh" },
                    },
                },
            };
            var parentEdges = new List<PcgGraphEdgeRecord>
            {
                new()
                {
                    id = "edge",
                    source = "source",
                    target = "linked",
                    sourceHandle = "out",
                    targetHandle = "removed",
                },
            };

            var result = PcgExternalSubgraphInterfaceSync.ReconcileNodeRecord(
                node,
                parentEdges,
                new PcgSubgraphInterfaceSnapshot());

            Assert.That(result.Compatible, Is.False);
            Assert.That(result.GhostHandles, Does.Contain("removed"));
            Assert.That(node.subgraphInterface.inputs.ConvertAll(port => port.id),
                Is.EqualTo(new[] { "removed" }));
        }

        [Test]
        public void LinkedSubgraphInterfaceSync_ReconcilesRecordsInHiddenSubgraphScopes()
        {
            const string guid = "0123456789abcdef0123456789abcdef";
            var linked = new PcgGraphNodeRecord
            {
                id = "linked",
                type = PcgStructuralNodeTypes.SubgraphAsset,
                data = MakeGuidData(guid),
                subgraphInterface = new PcgSubgraphInterfaceSnapshot
                {
                    inputs =
                    {
                        new PcgSubgraphPort { id = "in_1", name = "Input", pinType = "Any" },
                        new PcgSubgraphPort { id = "stale", name = "Stale", pinType = "Any" },
                    },
                },
            };
            var document = new PcgGraphDocument
            {
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "hidden",
                        nodes = { linked },
                    },
                },
            };

            var report = PcgExternalSubgraphInterfaceSync.ReconcileDocument(
                document,
                (string assetGuid, out PcgSubgraphInterfaceSnapshot snapshot, out string contentHash, out string schemaVersion, out string error) =>
                {
                    snapshot = new PcgSubgraphInterfaceSnapshot
                    {
                        inputs =
                        {
                            new PcgSubgraphPort { id = "in_1", name = "Input", pinType = "Any" },
                        },
                    };
                    contentHash = "hash";
                    schemaVersion = PcgSubgraphAssetMigration.Version10;
                    error = null;
                    return assetGuid == guid;
                });

            Assert.That(report.Compatible, Is.True, string.Join("\n", report.Errors));
            Assert.That(report.VisitedNodes, Is.EqualTo(1));
            Assert.That(report.UpdatedNodes, Is.EqualTo(1));
            Assert.That(linked.subgraphInterface.inputs.ConvertAll(port => port.id),
                Is.EqualTo(new[] { "in_1" }));
        }

        [Test]
        public void InlineSubgraphInstance_ExposesUntypedInputsAndTypedOutputsForInspector()
        {
            var definition = new PcgSubgraphDefinition
            {
                id = "typed",
                name = "Typed",
                inputs =
                {
                    new PcgSubgraphPort { id = "mesh", name = "Mesh", pinType = "SpatialMesh" },
                    new PcgSubgraphPort { id = "points", name = "Points", pinType = "SpatialPoint" },
                },
                outputs =
                {
                    new PcgSubgraphPort { id = "result", name = "Result", pinType = "SpatialGeometry" },
                },
            };
            var instance = new PcgSubgraphNodeView(definition, PcgSubgraphNodeKind.Instance);

            var snapshot = instance.InterfaceSnapshot;

            Assert.That(snapshot.inputs.ConvertAll(port => port.pinType),
                Is.EqualTo(new[] { "Any", "Any" }));
            Assert.That(snapshot.outputs.ConvertAll(port => port.pinType),
                Is.EqualTo(new[] { "SpatialGeometry" }));
        }

        [Test]
        public void ExecutionBuilder_IgnoresDanglingUnmatchedForEachBranch()
        {
            var document = new PcgGraphDocument
            {
                nodes =
                {
                    new PcgGraphNodeRecord { id = "box", type = "CreateBoxMesh" },
                    new PcgGraphNodeRecord { id = "output", type = "Output" },
                    new PcgGraphNodeRecord { id = "dangling_begin", type = "ForEachBegin" },
                    new PcgGraphNodeRecord { id = "dangling_body", type = "TransformMesh" },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "active",
                        source = "box",
                        target = "output",
                        sourceHandle = "out",
                        targetHandle = "in",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "wip",
                        source = "dangling_begin",
                        target = "dangling_body",
                        sourceHandle = "out",
                        targetHandle = "in",
                    },
                },
            };

            Assert.That(
                PcgExecutionDocumentBuilder.TryBuild(
                    document,
                    null,
                    out var flat,
                    out _,
                    out var error),
                Is.True,
                error);
            Assert.That(flat.nodes.ConvertAll(node => node.id),
                Is.EqualTo(new[] { "box", "output" }));
            Assert.That(flat.edges.ConvertAll(edge => edge.id),
                Is.EqualTo(new[] { "active" }));
        }

        [Test]
        public void SubgraphAsset_LegacyEmptyInterface_IsRepairedOnParse()
        {
            const string legacyJson = @"{
  ""version"": ""1.0"",
  ""name"": ""Legacy"",
  ""inputs"": [],
  ""outputs"": [],
  ""nodes"": [
    { ""id"": ""subgraph_input"", ""type"": ""SubgraphInput"", ""position"": { ""x"": 120, ""y"": 80 }, ""data"": {} },
    { ""id"": ""subgraph_output"", ""type"": ""SubgraphOutput"", ""position"": { ""x"": 120, ""y"": 320 }, ""data"": {} }
  ],
  ""edges"": [],
  ""subgraphs"": []
}";

            Assert.That(
                PcgSubgraphAssetSerializer.TryFromJson(legacyJson, out var parsed, out var error),
                Is.True,
                error);
            Assert.That(parsed.inputs.Count, Is.EqualTo(1));
            Assert.That(parsed.inputs[0].id, Is.EqualTo("in_1"));
            Assert.That(parsed.outputs.Count, Is.EqualTo(1));
            Assert.That(parsed.outputs[0].id, Is.EqualTo("out_1"));
            Assert.That(parsed.nodes.Count(node => node.type == "Output"), Is.EqualTo(1));
            Assert.That(parsed.nodes.Any(node => node.type == PcgStructuralNodeTypes.SubgraphOutput), Is.False);
            Assert.That(parsed.edges.Count, Is.EqualTo(1));
            Assert.That(parsed.edges[0].sourceHandle, Is.EqualTo("in_1"));
            Assert.That(parsed.edges[0].targetHandle, Is.EqualTo("in"));
        }

        [Test]
        public void SubgraphAsset_V2_RoundTrip_PreservesParametersAndContentHash()
        {
            var asset = PcgSubgraphAssetDocument.CreateEmpty("Demo");
            asset.version = PcgSubgraphAssetMigration.Version20;
            asset.parameters.Add(new PcgGraphParameter
            {
                id = "width",
                name = "Width",
                type = "number",
                defaultValue = "2",
                targetNode = "box",
                targetProperty = "width",
            });
            asset.nodes.Add(new PcgGraphNodeRecord
            {
                id = "box",
                type = "CreateBoxMesh",
                data = new PcgNodeData(),
            });

            var json = PcgSubgraphAssetSerializer.ToJson(asset, pretty: false);
            Assert.That(PcgSubgraphAssetSerializer.TryFromJson(json, out var parsed, out var error), Is.True, error);
            Assert.That(parsed.version, Is.EqualTo(PcgSubgraphAssetMigration.Version20));
            Assert.That(parsed.parameters, Has.Count.EqualTo(1));
            Assert.That(parsed.parameters[0].id, Is.EqualTo("width"));
            Assert.That(string.IsNullOrEmpty(parsed.contentHash), Is.False);
        }

        [Test]
        public void SubgraphDefinition_RoundTrip_PreservesParameters()
        {
            var doc = new PcgGraphDocument
            {
                version = "2.0",
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "sg",
                        name = "Parametric",
                        parameters =
                        {
                            new PcgGraphParameter
                            {
                                id = "height",
                                name = "Height",
                                type = "number",
                                defaultValue = "3",
                                targetNode = "box",
                                targetProperty = "height",
                            },
                        },
                    },
                },
            };

            var json = PcgGraphSerializer.ToJson(doc, pretty: false);
            Assert.That(PcgGraphSerializer.TryFromJson(json, out var parsed, out var error), Is.True, error);
            Assert.That(parsed.subgraphs[0].parameters[0].id, Is.EqualTo("height"));
        }

        [Test]
        public void SubgraphParameterResolver_AppliesInstanceOverrideToInternalNode()
        {
            var definition = new PcgSubgraphDefinition
            {
                id = "sg",
                parameters =
                {
                    new PcgGraphParameter
                    {
                        id = "width",
                        name = "Width",
                        type = "number",
                        defaultValue = "1",
                        targetNode = "box",
                        targetProperty = "width",
                    },
                },
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                },
            };
            var instanceData = new PcgNodeData();
            instanceData.SetRaw("subgraphId", "sg");
            PcgSubgraphInstanceParameterStorage.SetOverrideValue(instanceData, definition.parameters[0], 4.5f);

            var document = new PcgGraphDocument
            {
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = instanceData,
                    },
                },
                subgraphs = { definition },
            };

            PcgSubgraphParameterResolver.ApplyInstanceOverrides(document);
            Assert.That(definition.nodes[0].data.GetRaw("width")?.ToString(), Is.EqualTo("4.5"));
        }

        [Test]
        public void GraphParameterUtility_PromoteBindingsMovesParametersIntoSubgraphDefinition()
        {
            var rootParams = new List<PcgGraphParameter>
            {
                new()
                {
                    id = "p1",
                    name = "Width",
                    type = "number",
                    defaultValue = "2",
                    targetNode = "box",
                    targetProperty = "width",
                },
            };
            var definition = new PcgSubgraphDefinition
            {
                id = "sg",
                nodes =
                {
                    new PcgGraphNodeRecord { id = "box", type = "CreateBoxMesh", data = new PcgNodeData() },
                },
            };

            PcgGraphParameterUtility.PromoteBindings(rootParams, definition, new HashSet<string> { "box" });

            Assert.That(rootParams, Is.Empty);
            Assert.That(definition.parameters, Has.Count.EqualTo(1));
            Assert.That(definition.parameters[0].targetNode, Is.EqualTo("box"));
        }

        [Test]
        public void SubgraphAsset_RoundTrip_PreservesAnchorPlacement()
        {
            var asset = PcgSubgraphAssetDocument.CreateEmpty("Anchors");
            asset.inputs[0].anchorPlaced = true;
            asset.inputs[0].anchorX = -120.25f;
            asset.inputs[0].anchorY = 44.5f;

            var json = PcgSubgraphAssetSerializer.ToJson(asset, pretty: false);
            Assert.That(PcgSubgraphAssetSerializer.TryFromJson(json, out var parsed, out var error),
                Is.True, error);
            Assert.That(parsed.inputs[0].anchorPlaced, Is.True);
            Assert.That(parsed.inputs[0].anchorX, Is.EqualTo(-120.25f));
            Assert.That(parsed.inputs[0].anchorY, Is.EqualTo(44.5f));
        }

        [Test]
        public void SaveRepair_RemovesMissingEndpointsAndInvalidInterfaceHandles()
        {
            var asset = PcgSubgraphAssetDocument.CreateEmpty("Repair");
            asset.nodes.Add(new PcgGraphNodeRecord
            {
                id = "box",
                type = "CreateBoxMesh",
                data = new PcgNodeData(),
            });
            asset.edges.Clear();
            asset.edges.Add(new PcgGraphEdgeRecord
            {
                id = "valid",
                source = "subgraph_input",
                target = "box",
                sourceHandle = "in_1",
                targetHandle = "in",
            });
            asset.edges.Add(new PcgGraphEdgeRecord
            {
                id = "missing_node",
                source = "subgraph_input",
                target = "deleted",
                sourceHandle = "in_1",
                targetHandle = "in",
            });
            asset.edges.Add(new PcgGraphEdgeRecord
            {
                id = "removed_port",
                source = "subgraph_input",
                target = "box",
                sourceHandle = "removed",
                targetHandle = "in",
            });

            var report = PcgGraphIntegrityRepair.RepairForSave(asset);

            Assert.That(report.RemovedOrphanEdges, Is.EqualTo(2));
            Assert.That(asset.edges.ConvertAll(edge => edge.id), Is.EqualTo(new[] { "valid" }));
            Assert.That(report.RemovedEdgePaths, Does.Contain("<asset>/missing_node"));
            Assert.That(report.RemovedEdgePaths, Does.Contain("<asset>/removed_port"));
        }

        [Test]
        public void SubgraphDefinitionCloner_ClonesRootDefinitionWithNewId()
        {
            var original = new PcgSubgraphDefinition
            {
                id = "sg_a",
                name = "Original",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                },
            };
            var counter = 0;
            Assert.That(PcgSubgraphDefinitionCloner.TryCloneDefinitionClosure(
                    "sg_a",
                    new[] { original },
                    () => $"sg_clone_{++counter}",
                    out var result),
                Is.True);
            Assert.That(result.RootDefinitionId, Is.EqualTo("sg_clone_1"));
            Assert.That(result.Definitions, Has.Count.EqualTo(1));
            Assert.That(result.Definitions[0].id, Is.EqualTo("sg_clone_1"));
            Assert.That(result.Definitions[0].name, Is.EqualTo("Original"));
            Assert.That(result.Definitions[0].nodes[0].id, Is.EqualTo("box"));
            Assert.That(ReferenceEquals(original, result.Definitions[0]), Is.False);
        }

        [Test]
        public void SubgraphDefinitionCloner_ClonesNestedClosureAndRemapsSubgraphIds()
        {
            var nested = new PcgSubgraphDefinition
            {
                id = "sg_nested",
                name = "Nested",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "inner",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                },
            };
            var root = new PcgSubgraphDefinition
            {
                id = "sg_root",
                name = "Root",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "child",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphIdData("sg_nested"),
                    },
                },
            };
            var counter = 0;
            Assert.That(PcgSubgraphDefinitionCloner.TryCloneDefinitionClosure(
                    "sg_root",
                    new[] { root, nested },
                    () => $"sg_clone_{++counter}",
                    out var result),
                Is.True);
            Assert.That(result.Definitions, Has.Count.EqualTo(2));
            var clonedRoot = result.Definitions.First(definition => definition.id == "sg_clone_1");
            var clonedNested = result.Definitions.First(definition => definition.id == "sg_clone_2");
            Assert.That(
                clonedRoot.nodes[0].data.GetRaw("subgraphId")?.ToString(),
                Is.EqualTo("sg_clone_2"));
            Assert.That(clonedNested.nodes[0].id, Is.EqualTo("inner"));
            Assert.That(
                nested.nodes[0].data.GetRaw("subgraphId")?.ToString(),
                Is.EqualTo("sg_nested"));
        }

        [Test]
        public void SubgraphDefinitionCloner_EditingCloneDoesNotMutateOriginal()
        {
            var original = new PcgSubgraphDefinition
            {
                id = "sg_a",
                name = "Original",
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "box",
                        type = "CreateBoxMesh",
                        data = new PcgNodeData(),
                    },
                },
            };
            var counter = 0;
            Assert.That(PcgSubgraphDefinitionCloner.TryCloneDefinitionClosure(
                    "sg_a",
                    new[] { original },
                    () => $"sg_clone_{++counter}",
                    out var result),
                Is.True);
            result.Definitions[0].name = "Mutated";
            result.Definitions[0].nodes[0].id = "changed";
            Assert.That(original.name, Is.EqualTo("Original"));
            Assert.That(original.nodes[0].id, Is.EqualTo("box"));
        }

        private static PcgNodeData MakeSubgraphIdData(string subgraphId)
        {
            var data = new PcgNodeData();
            data.SetRaw("subgraphId", subgraphId);
            return data;
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
        public void Resolve_UnconnectedRemovedSnapshotPort_IsIgnored()
        {
            const string guid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            var asset = MakeSimpleInterfaceAsset();
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
                        subgraphInterface = new PcgSubgraphInterfaceSnapshot
                        {
                            inputs =
                            {
                                new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" },
                                new PcgSubgraphPort { id = "stale", name = "Stale", pinType = "Any" },
                            },
                            outputs =
                            {
                                new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" },
                            },
                        },
                    },
                },
            };
            var assetJson = PcgSubgraphAssetSerializer.ToJson(asset, pretty: false);

            Assert.That(PcgExternalSubgraphResolver.TryResolveToInline(
                authoring,
                (string assetGuid, out string json, out string error) =>
                {
                    json = assetJson;
                    error = null;
                    return assetGuid == guid;
                },
                out var result), Is.True, result?.Error);
        }

        [Test]
        public void Resolve_ConnectedRemovedSnapshotPort_StillFailsClosed()
        {
            const string guid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            var asset = MakeSimpleInterfaceAsset();
            var authoring = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
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
                        type = PcgStructuralNodeTypes.SubgraphAsset,
                        data = MakeGuid(guid),
                        subgraphInterface = new PcgSubgraphInterfaceSnapshot
                        {
                            inputs =
                            {
                                new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" },
                                new PcgSubgraphPort { id = "stale", name = "Stale", pinType = "Any" },
                            },
                            outputs =
                            {
                                new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" },
                            },
                        },
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "connected_stale",
                        source = "source",
                        target = "inst",
                        sourceHandle = "out",
                        targetHandle = "stale",
                    },
                },
            };
            var assetJson = PcgSubgraphAssetSerializer.ToJson(asset, pretty: false);

            Assert.That(PcgExternalSubgraphResolver.TryResolveToInline(
                authoring,
                (string assetGuid, out string json, out string error) =>
                {
                    json = assetJson;
                    error = null;
                    return assetGuid == guid;
                },
                out var result), Is.False);
            Assert.That(result.Error, Does.Contain("source removed input port 'stale'"));
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

        [Test]
        public void Flatten_MapsMultipleRootInputsToDifferentInternalNodes()
        {
            var doc = new PcgGraphDocument
            {
                version = "2.0",
                nodes =
                {
                    new PcgGraphNodeRecord { id = "source_a", type = "CreateBoxMesh", data = new PcgNodeData() },
                    new PcgGraphNodeRecord { id = "source_b", type = "CreateBoxMesh", data = new PcgNodeData() },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("sg"),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "root_a", source = "source_a", target = "inst",
                        sourceHandle = "out", targetHandle = "in_a",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "root_b", source = "source_b", target = "inst",
                        sourceHandle = "out", targetHandle = "in_b",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "sg",
                        name = "Two Inputs",
                        inputs =
                        {
                            new PcgSubgraphPort { id = "in_a", name = "A", pinType = "SpatialMesh" },
                            new PcgSubgraphPort { id = "in_b", name = "B", pinType = "SpatialMesh" },
                        },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "input", type = PcgStructuralNodeTypes.SubgraphInput, data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "modify_a", type = "TransformMesh", data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord
                            {
                                id = "modify_b", type = "TransformMesh", data = new PcgNodeData(),
                            },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "inner_a", source = "input", target = "modify_a",
                                sourceHandle = "in_a", targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "inner_b", source = "input", target = "modify_b",
                                sourceHandle = "in_b", targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(PcgGraphFlattener.TryFlattenForExecution(doc, out var flat, out var error), Is.True, error);
            Assert.That(
                flat.edges.Exists(edge =>
                    edge.source == "source_a" &&
                    edge.target == "inst/modify_a" &&
                    edge.targetHandle == "in"),
                Is.True);
            Assert.That(
                flat.edges.Exists(edge =>
                    edge.source == "source_b" &&
                    edge.target == "inst/modify_b" &&
                    edge.targetHandle == "in"),
                Is.True);
        }

        [Test]
        public void InlineSubgraphInstance_BuildsAllDeclaredInputPorts()
        {
            var definition = new PcgSubgraphDefinition
            {
                id = "sg",
                name = "Two Inputs",
                inputs =
                {
                    new PcgSubgraphPort { id = "in_a", name = "A", pinType = "SpatialMesh" },
                    new PcgSubgraphPort { id = "in_b", name = "B", pinType = "SpatialMesh" },
                },
            };

            var instance = new PcgSubgraphNodeView(definition, PcgSubgraphNodeKind.Instance);
            instance.Initialize("inst", UnityEngine.Vector2.zero);

            var inputA = instance.FindInputPort("in_a");
            var inputB = instance.FindInputPort("in_b");
            Assert.That(inputA, Is.Not.Null);
            Assert.That(inputB, Is.Not.Null);
            Assert.That(inputA, Is.Not.SameAs(inputB));
            Assert.That(inputA.userData, Is.EqualTo("in_a"));
            Assert.That(inputB.userData, Is.EqualTo("in_b"));
        }

        [Test]
        public void Flatten_ResolvesDirectSubgraphInputOutputPassthrough()
        {
            var doc = new PcgGraphDocument
            {
                version = "2.0",
                nodes =
                {
                    new PcgGraphNodeRecord { id = "source", type = "CreateBoxMesh", data = new PcgNodeData() },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("sg"),
                    },
                    new PcgGraphNodeRecord { id = "sink", type = "Output", data = new PcgNodeData() },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "root_in", source = "source", target = "inst",
                        sourceHandle = "out", targetHandle = "in",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "root_out", source = "inst", target = "sink",
                        sourceHandle = "out", targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "sg",
                        name = "Passthrough",
                        inputs = { new PcgSubgraphPort { id = "in", name = "In", pinType = "SpatialMesh" } },
                        outputs = { new PcgSubgraphPort { id = "out", name = "Out", pinType = "SpatialMesh" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "input", type = PcgStructuralNodeTypes.SubgraphInput, data = new PcgNodeData(),
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
                                id = "inner", source = "input", target = "output",
                                sourceHandle = "in", targetHandle = "out",
                            },
                        },
                    },
                },
            };

            Assert.That(PcgGraphFlattener.TryFlattenForExecution(doc, out var flat, out var error), Is.True, error);
            Assert.That(
                flat.edges.Exists(edge =>
                    edge.source == "source" &&
                    edge.target == "sink" &&
                    edge.sourceHandle == "out" &&
                    edge.targetHandle == "in"),
                Is.True);
        }

        [Test]
        public void AssetLoad_RepairsInterfaceOnlySubgraphMissingPassthroughEdges()
        {
            var json = @"{
  ""version"": ""1.0"",
  ""name"": ""Windows_floor"",
  ""inputs"": [{""id"": ""in_1"", ""name"": ""GroupDelete"", ""pinType"": ""Any""}],
  ""outputs"": [{""id"": ""out_1"", ""name"": ""Output"", ""pinType"": ""Any""}],
  ""nodes"": [
    {""id"": ""subgraph_output"", ""type"": ""SubgraphOutput"", ""position"": {""x"": 0, ""y"": 0}, ""data"": {}},
    {""id"": ""subgraph_input"", ""type"": ""SubgraphInput"", ""position"": {""x"": 0, ""y"": -195}, ""data"": {}}
  ],
  ""edges"": [],
  ""subgraphs"": []
}";

            Assert.That(PcgSubgraphAssetSerializer.TryFromJson(json, out var asset, out var error), Is.True, error);
            var output = asset.nodes.Single(node => node.type == "Output");
            Assert.That(asset.nodes.Any(node => node.type == PcgStructuralNodeTypes.SubgraphOutput), Is.False);
            Assert.That(
                asset.edges.Exists(edge =>
                    edge.source == "subgraph_input" &&
                    edge.target == output.id &&
                    edge.sourceHandle == "in_1" &&
                    edge.targetHandle == "in"),
                Is.True);

            var doc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord { id = "source", type = "CreateBoxMesh", data = new PcgNodeData() },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("sg"),
                    },
                    new PcgGraphNodeRecord { id = "sink", type = "Output", data = new PcgNodeData() },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "root_in", source = "source", target = "inst",
                        sourceHandle = "out", targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "root_out", source = "inst", target = "sink",
                        sourceHandle = "out_1", targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "sg",
                        name = "Windows_floor",
                        inputs = asset.inputs.Select(port => new PcgSubgraphPort
                        {
                            id = port.id,
                            name = port.name,
                            pinType = port.pinType,
                        }).ToList(),
                        outputs = asset.outputs.Select(port => new PcgSubgraphPort
                        {
                            id = port.id,
                            name = port.name,
                            pinType = port.pinType,
                        }).ToList(),
                        nodes = asset.nodes.Select(node => node.Clone()).ToList(),
                        edges = new List<PcgGraphEdgeRecord>(),
                    },
                },
            };

            Assert.That(PcgGraphFlattener.TryFlattenForExecution(doc, out var flat, out error), Is.True, error);
            Assert.That(
                flat.edges.Exists(edge =>
                    edge.source == "source" &&
                    edge.target == "sink" &&
                    edge.sourceHandle == "out" &&
                    edge.targetHandle == "in"),
                Is.True);
        }

        [Test]
        public void ExecutionBuilder_SubgraphOrdinaryOutput_DoesNotReplacePreviewSink()
        {
            var doc = new PcgGraphDocument
            {
                version = "3.0",
                nodes =
                {
                    new PcgGraphNodeRecord { id = "source", type = "CreateBoxMesh", data = new PcgNodeData() },
                    new PcgGraphNodeRecord
                    {
                        id = "inst",
                        type = PcgStructuralNodeTypes.Subgraph,
                        data = MakeSubgraphId("windows"),
                    },
                    new PcgGraphNodeRecord
                    {
                        id = "__pcg_preview_sink__",
                        type = "Output",
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "root_in", source = "source", target = "inst",
                        sourceHandle = "out", targetHandle = "in_1",
                    },
                    new PcgGraphEdgeRecord
                    {
                        id = "root_out", source = "inst", target = "__pcg_preview_sink__",
                        sourceHandle = "out_1", targetHandle = "in",
                    },
                },
                subgraphs =
                {
                    new PcgSubgraphDefinition
                    {
                        id = "windows",
                        inputs = { new PcgSubgraphPort { id = "in_1", pinType = "Any" } },
                        outputs = { new PcgSubgraphPort { id = "out_1", pinType = "Any" } },
                        nodes =
                        {
                            new PcgGraphNodeRecord
                            {
                                id = "input", type = PcgStructuralNodeTypes.SubgraphInput,
                                data = new PcgNodeData(),
                            },
                            new PcgGraphNodeRecord { id = "begin", type = "ForEachBegin", data = new PcgNodeData() },
                            new PcgGraphNodeRecord { id = "end", type = "ForEachEnd", data = new PcgNodeData() },
                            new PcgGraphNodeRecord { id = "output", type = "Output", data = new PcgNodeData() },
                        },
                        edges =
                        {
                            new PcgGraphEdgeRecord
                            {
                                id = "i1", source = "input", target = "begin",
                                sourceHandle = "in_1", targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "i2", source = "begin", target = "end",
                                sourceHandle = "out", targetHandle = "in",
                            },
                            new PcgGraphEdgeRecord
                            {
                                id = "i3", source = "end", target = "output",
                                sourceHandle = "out", targetHandle = "in",
                            },
                        },
                    },
                },
            };

            Assert.That(
                PcgExecutionDocumentBuilder.TryBuild(doc, null, out var flat, out _, out var error),
                Is.True,
                error);
            Assert.That(flat.nodes.Any(node => node.id == "inst/output"), Is.False);
            Assert.That(flat.nodes.Any(node => node.id == "inst/begin"), Is.True);
            Assert.That(flat.nodes.Any(node => node.id == "inst/end"), Is.True);
            Assert.That(flat.nodes.Any(node => node.id == "__pcg_preview_sink__"), Is.True);
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

        private static PcgSubgraphAssetDocument MakeSimpleInterfaceAsset()
        {
            return new PcgSubgraphAssetDocument
            {
                name = "Simple",
                inputs =
                {
                    new PcgSubgraphPort { id = "in", name = "In", pinType = "Any" },
                },
                outputs =
                {
                    new PcgSubgraphPort { id = "out", name = "Out", pinType = "Any" },
                },
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
                        id = "output",
                        type = PcgStructuralNodeTypes.SubgraphOutput,
                        data = new PcgNodeData(),
                    },
                },
                edges =
                {
                    new PcgGraphEdgeRecord
                    {
                        id = "passthrough",
                        source = "input",
                        target = "output",
                        sourceHandle = "in",
                        targetHandle = "out",
                    },
                },
            };
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
