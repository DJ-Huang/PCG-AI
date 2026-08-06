using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEditor;

namespace DJTechEditor.PCG.Tests
{
    public class PcgMeshyResolverTests
    {
        private string m_PreviousKey;

        [SetUp]
        public void SetUp()
        {
            m_PreviousKey = EditorPrefs.GetString(PcgMeshySettings.PrefKeyApiKey, "");
        }

        [TearDown]
        public void TearDown()
        {
            EditorPrefs.SetString(PcgMeshySettings.PrefKeyApiKey, m_PreviousKey ?? "");
            // Refresh static cache used by ApiKey getter.
            PcgMeshySettings.ApiKey = m_PreviousKey ?? "";
        }

        [Test]
        public void TryBuildGenerateRequest_WithoutApiKey_FailsWithKeyError()
        {
            PcgMeshySettings.ClearApiKey();

            var data = new PcgNodeData();
            data.SetRaw("imageUrl", "https://example.com/cup.png");

            var ok = PcgMeshyResolver.TryBuildGenerateRequest(
                "nodeA", data, out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("API key missing"));
        }

        [Test]
        public void TryBuildGenerateRequest_WithoutImage_FailsWithImageError()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var ok = PcgMeshyResolver.TryBuildGenerateRequest(
                "nodeA", new PcgNodeData(), out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("Source Image"));
        }

        [Test]
        public void TryBuildGenerateRequest_WithImageUrl_BuildsRequestAndCachePath()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("imageUrl", " https://example.com/cup.png ");
            data.SetRaw("aiModel", "meshy-5");
            data.SetRaw("enablePbr", true);
            data.SetRaw("shouldTexture", false);
            data.SetRaw("shouldRemesh", true);
            data.SetRaw("targetPolycount", 12000);

            var ok = PcgMeshyResolver.TryBuildGenerateRequest(
                "nodeA", data, out var request, out var path, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.ImageDataUri, Is.EqualTo("https://example.com/cup.png"));
            Assert.That(request.AiModel, Is.EqualTo("meshy-5"));
            Assert.That(request.EnablePbr, Is.True);
            Assert.That(request.ShouldTexture, Is.False);
            Assert.That(request.ShouldRemesh, Is.True);
            Assert.That(request.TargetPolycount, Is.EqualTo(12000));
            Assert.That(path, Does.EndWith(".glb"));
            Assert.That(path, Does.Contain("MeshyCache"));
        }

        [Test]
        public void TryBuildGenerateRequest_Defaults_AreLatestTexturedRemesh30k()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("imageUrl", "https://example.com/cup.png");

            var ok = PcgMeshyResolver.TryBuildGenerateRequest(
                "nodeA", data, out var request, out _, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.AiModel, Is.EqualTo("latest"));
            Assert.That(request.EnablePbr, Is.False);
            Assert.That(request.ShouldTexture, Is.True);
            Assert.That(request.ShouldRemesh, Is.True);
            Assert.That(request.TargetPolycount, Is.EqualTo(30000));
        }

        [Test]
        public void CachePath_IsDeterministic_AndParameterSensitive()
        {
            var data = new PcgNodeData();
            data.SetRaw("imageUrl", "https://example.com/cup.png");

            Assert.That(
                PcgMeshyResolver.TryGetCachedModelPath("nodeA", data, out var pathA1),
                Is.False);
            PcgMeshyResolver.TryGetCachedModelPath("nodeA", data, out var pathA2);
            Assert.That(pathA1, Is.EqualTo(pathA2));

            var otherPolycount = new PcgNodeData();
            otherPolycount.SetRaw("imageUrl", "https://example.com/cup.png");
            otherPolycount.SetRaw("targetPolycount", 5000);
            PcgMeshyResolver.TryGetCachedModelPath("nodeA", otherPolycount, out var pathB);

            var otherNode = new PcgNodeData();
            otherNode.SetRaw("imageUrl", "https://example.com/cup.png");
            PcgMeshyResolver.TryGetCachedModelPath("nodeB", otherNode, out var pathC);

            Assert.That(pathB, Is.Not.EqualTo(pathA1));
            Assert.That(pathC, Is.Not.EqualTo(pathA1));
        }
    }
}
