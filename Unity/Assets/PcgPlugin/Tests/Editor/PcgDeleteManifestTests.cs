using NUnit.Framework;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgDeleteManifestTests
    {
        [Test]
        public void Delete_HasTabsLayout_AndSpatialGeometryPins()
        {
            Assert.IsTrue(PcgNodeManifest.TryGet("Delete", out var delete));
            Assert.That(delete.inspectorSectionLayout, Is.EqualTo("tabs"));
            Assert.That(delete.inspectorSections.Count, Is.GreaterThanOrEqualTo(5));
            Assert.IsTrue(PcgNodeManifest.HasCompatibleInputPin("Delete", "SpatialMesh"));
            Assert.IsTrue(PcgNodeManifest.HasCompatibleInputPin("Delete", "SpatialSpline"));
        }

        [Test]
        public void Delete_FloatingPointProperties_UseNumberManifestType()
        {
            Assert.IsTrue(PcgNodeManifest.TryGet("Delete", out var delete));
            foreach (var key in new[]
                     {
                         "boundingCenterX", "boundingCenterY", "boundingCenterZ",
                         "boundingSizeX", "boundingSizeY", "boundingSizeZ", "boundingRadius",
                         "normalDirX", "normalDirY", "normalDirZ", "normalSpread",
                         "degenerateTolerance", "randomPercent",
                     })
            {
                Assert.That(delete.properties[key].type, Is.EqualTo("number"), key);
            }

            var document = PcgGraphDefaults.CreatePipeline();
            document.nodes[0].type = "Delete";
            document.nodes[0].data = PcgNodeData.DefaultForType("Delete");
            var json = PcgGraphSerializer.ToJson(document, pretty: false);
            StringAssert.Contains("\"boundingCenterX\": 0", json);
            StringAssert.DoesNotContain("\"boundingCenterX\": \"0\"", json);
        }

        [Test]
        public void SortGeometry_KeepsFoldoutLayout()
        {
            Assert.IsTrue(PcgNodeManifest.TryGet("SortGeometry", out var sort));
            Assert.That(sort.inspectorSectionLayout, Is.EqualTo("foldouts"));
            Assert.IsTrue(sort.inspectorSections.Exists(s => s.foldout));
        }
    }
}
