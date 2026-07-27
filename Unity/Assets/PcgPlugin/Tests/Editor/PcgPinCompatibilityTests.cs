using NUnit.Framework;
using DJTechEditor.PCG.Graph;

namespace DJTechEditor.PCG.Tests
{
    public class PcgPinCompatibilityTests
    {
        [Test]
        public void SpatialGeometry_Accepts_Spline_And_Mesh()
        {
            Assert.IsTrue(PcgNodeManifest.PinTypesCompatible("SpatialSpline", "SpatialGeometry"));
            Assert.IsTrue(PcgNodeManifest.PinTypesCompatible("SpatialMesh", "SpatialGeometry"));
            Assert.IsTrue(PcgNodeManifest.PinTypesCompatible("SpatialGeometry", "SpatialGeometry"));
        }

        [Test]
        public void SpatialGeometry_Rejects_Point_And_HeightField()
        {
            Assert.IsFalse(PcgNodeManifest.PinTypesCompatible("SpatialPoint", "SpatialGeometry"));
            Assert.IsFalse(PcgNodeManifest.PinTypesCompatible("HeightField", "SpatialGeometry"));
        }

        [Test]
        public void SortGeometry_Has_SpatialGeometry_Input()
        {
            Assert.IsTrue(PcgNodeManifest.HasCompatibleInputPin("SortGeometry", "SpatialSpline"));
            Assert.IsTrue(PcgNodeManifest.HasCompatibleInputPin("SortGeometry", "SpatialMesh"));
            Assert.IsFalse(PcgNodeManifest.HasCompatibleInputPin("SortGeometry", "SpatialPoint"));
            Assert.IsFalse(PcgNodeManifest.HasCompatibleInputPin("SortGeometry", "HeightField"));
        }
    }
}
