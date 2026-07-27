using DJTechRuntime.PCG;
using NUnit.Framework;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgResultParserSplineTests
    {
        [Test]
        public void TryParseSplines_EmptyArray_ReturnsTrueWithZeroSplines()
        {
            Assert.That(
                PcgResultParser.TryParseSplines("{\"splines\":[]}", out var splines, out var error),
                Is.True,
                error);
            Assert.That(splines, Is.Not.Null);
            Assert.That(splines.Count, Is.EqualTo(0));
        }

        [Test]
        public void TryParseSplines_MissingArray_ReturnsFalse()
        {
            Assert.That(
                PcgResultParser.TryParseSplines("{}", out _, out var error),
                Is.False);
            Assert.That(error, Does.Contain("splines"));
        }
    }
}
