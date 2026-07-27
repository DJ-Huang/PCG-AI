using NUnit.Framework;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgVector3PropertyTests
    {
        [Test]
        public void ResolveFromNodeData_CanonicalDefaultPlusLegacyY_UsesLegacy()
        {
            var data = new PcgNodeData();
            data.SetRaw("translate", PcgVector3Property.Format(0f, 0f, 0f));
            data.SetRaw("translateY", 2f);

            var resolved = PcgVector3Property.ResolveFromNodeData(
                data, "translate", Vector3.zero);

            Assert.That(resolved, Is.EqualTo(new Vector3(0f, 2f, 0f)));
        }

        [Test]
        public void ResolveFromNodeData_ExplicitCanonical_WinsOverLegacy()
        {
            var data = new PcgNodeData();
            data.SetRaw("translate", PcgVector3Property.Format(1f, 0f, 0f));
            data.SetRaw("translateY", 2f);

            var resolved = PcgVector3Property.ResolveFromNodeData(
                data, "translate", Vector3.zero);

            Assert.That(resolved, Is.EqualTo(new Vector3(1f, 0f, 0f)));
        }
    }
}
