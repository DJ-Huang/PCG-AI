using DJTechRuntime.PCG;
using NUnit.Framework;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgGraphExecutionPolicyTests
    {
        [Test]
        public void PrepareJson_NoEditorNode_ReturnsOriginalJson()
        {
            const string json = "{\"version\":\"1.0\",\"nodes\":[],\"edges\":[],\"parameters\":[],\"subgraphs\":[]}";
            Assert.That(PcgGraphExecutionPolicy.TryPrepareJson(json, out var prepared, out var error), Is.True, error);
            Assert.That(prepared, Is.SameAs(json));
        }

        [Test]
        public void PrepareJson_WithRegularOutput_StripsExportBranch()
        {
            const string json = @"{
  ""version"":""1.0"",
  ""nodes"":[
    {""id"":""source"",""type"":""BoxMesh"",""position"":{""x"":0,""y"":0},""data"":{}},
    {""id"":""export"",""type"":""ExportFBX"",""position"":{""x"":0,""y"":1},""data"":{}},
    {""id"":""output"",""type"":""Output"",""position"":{""x"":1,""y"":1},""data"":{}}
  ],
  ""edges"":[
    {""id"":""to_export"",""source"":""source"",""target"":""export"",""sourceHandle"":""out"",""targetHandle"":""in""},
    {""id"":""to_output"",""source"":""source"",""target"":""output"",""sourceHandle"":""out"",""targetHandle"":""in""}
  ],
  ""parameters"":[],""subgraphs"":[]
}";
            Assert.That(PcgGraphExecutionPolicy.TryPrepareJson(json, out var prepared, out var error), Is.True, error);
            Assert.That(PcgGraphSerializer.TryFromJson(prepared, out var document, out error), Is.True, error);
            Assert.That(document.nodes.Exists(node => node.id == "export"), Is.False);
            Assert.That(document.edges.Exists(edge => edge.id == "to_export"), Is.False);
            Assert.That(document.nodes.Exists(node => node.id == "output" && node.type == "Output"), Is.True);
        }

        [Test]
        public void PrepareJson_ReplacesRootAndSubgraphExportNodesWithPassiveOutputs()
        {
            const string json = @"{
  ""version"":""1.0"",
  ""nodes"":[{""id"":""root_export"",""type"":""ExportFBX"",""position"":{""x"":0,""y"":0},""data"":{""path"":""file.fbx""}}],
  ""edges"":[],
  ""parameters"":[],
  ""subgraphs"":[{
    ""id"":""sub"",""name"":""Sub"",""inputs"":[],""outputs"":[],
    ""nodes"":[{""id"":""sub_export"",""type"":""ExportFBX"",""position"":{""x"":0,""y"":0},""data"":{}}],
    ""edges"":[]
  }]
}";

            Assert.That(
                PcgGraphExecutionPolicy.TryPrepareJson(json, out var prepared, out var error),
                Is.True,
                error);
            Assert.That(PcgGraphSerializer.TryFromJson(prepared, out var document, out error), Is.True, error);
            Assert.That(document.nodes[0].type, Is.EqualTo("Output"));
            Assert.That(document.subgraphs[0].nodes[0].type, Is.EqualTo("Output"));
            Assert.That(prepared, Does.Not.Contain("ExportFBX"));
            Assert.That(prepared, Does.Not.Contain("file.fbx"));
        }
    }
}
