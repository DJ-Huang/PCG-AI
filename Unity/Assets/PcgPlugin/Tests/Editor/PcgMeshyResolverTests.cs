using System.IO;
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
        public void TryPrepareForCook_WithoutCache_FailsWithGenerateHint()
        {
            var data = new PcgNodeData();
            data.SetRaw("imageUrl", "https://example.com/cup.png");
            data.SetRaw("forceRegenerate", true);

            var json = PcgGraphSerializer.ToJson(new PcgGraphDocument
            {
                nodes =
                {
                    new PcgGraphNodeRecord
                    {
                        id = "nodeA",
                        type = PcgMeshyResolver.NodeType,
                        data = data,
                    },
                },
            });

            Assert.That(PcgMeshyResolver.TryPrepareForCook(ref json, out var error), Is.False);
            Assert.That(error, Does.Contain("Generate"));
            Assert.That(error, Does.Contain("never generate"));
        }

        [Test]
        public void TryPrepareForCook_WithSavedPath_UsesPathWithoutMatchingCache()
        {
            PcgMeshySettings.ClearApiKey();

            var tempDir = Path.Combine(Path.GetTempPath(), "pcg-meshy-tests");
            Directory.CreateDirectory(tempDir);
            var saved = Path.Combine(tempDir, "saved-cup.glb");
            File.WriteAllBytes(saved, new byte[] { 0x67, 0x6C, 0x54, 0x46 });

            try
            {
                var data = new PcgNodeData();
                data.SetRaw("imageUrl", "https://example.com/cup.png");
                data.SetRaw("path", saved);

                var json = PcgGraphSerializer.ToJson(new PcgGraphDocument
                {
                    nodes =
                    {
                        new PcgGraphNodeRecord
                        {
                            id = "nodeA",
                            type = PcgMeshyResolver.NodeType,
                            data = data,
                        },
                    },
                });

                Assert.That(
                    PcgMeshyResolver.TryPrepareForCook(ref json, out var error),
                    Is.True,
                    error);
                Assert.That(PcgGraphSerializer.TryFromJson(json, out var doc, out error), Is.True, error);
                Assert.That(
                    Path.GetFullPath(doc.nodes[0].data.GetRaw("path")?.ToString() ?? ""),
                    Is.EqualTo(Path.GetFullPath(saved)));
            }
            finally
            {
                if (File.Exists(saved))
                    File.Delete(saved);
            }
        }

        [Test]
        public void TryGetSavedModelPath_ResolvesProjectRelativeUnderAssets()
        {
            var projectRoot = PcgMeshyResolver.GetUnityProjectRoot();
            var relative = "Assets/PCG_MeshyResolverTest_Temp.glb";
            var absolute = Path.Combine(projectRoot, relative.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(absolute));
            File.WriteAllBytes(absolute, new byte[] { 0x67, 0x6C, 0x54, 0x46 });

            try
            {
                var data = new PcgNodeData();
                data.SetRaw("path", relative);
                Assert.That(PcgMeshyResolver.TryGetSavedModelPath(data, out var resolved), Is.True);
                Assert.That(Path.GetFullPath(resolved), Is.EqualTo(Path.GetFullPath(absolute)));
            }
            finally
            {
                if (File.Exists(absolute))
                    File.Delete(absolute);
            }
        }

        [Test]
        public void TryPrepareForCook_WithCache_InjectsPathWithoutApiKey()
        {
            PcgMeshySettings.ClearApiKey();

            var data = new PcgNodeData();
            data.SetRaw("imageUrl", "https://example.com/cup.png");
            PcgMeshyResolver.TryGetCachedModelPath("nodeA", data, out var cachePath);

            Directory.CreateDirectory(Path.GetDirectoryName(cachePath));
            File.WriteAllBytes(cachePath, new byte[] { 0x67, 0x6C, 0x54, 0x46 });

            try
            {
                var json = PcgGraphSerializer.ToJson(new PcgGraphDocument
                {
                    nodes =
                    {
                        new PcgGraphNodeRecord
                        {
                            id = "nodeA",
                            type = PcgMeshyResolver.NodeType,
                            data = data,
                        },
                    },
                });

                Assert.That(
                    PcgMeshyResolver.TryPrepareForCook(ref json, out var error),
                    Is.True,
                    error);
                Assert.That(PcgGraphSerializer.TryFromJson(json, out var doc, out error), Is.True, error);
                Assert.That(doc.nodes[0].data.GetRaw("path")?.ToString(), Is.EqualTo(cachePath));
            }
            finally
            {
                if (File.Exists(cachePath))
                    File.Delete(cachePath);
            }
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
            Assert.That(request.TargetFormats, Is.Not.Null);
            Assert.That(request.TargetFormats, Does.Contain("glb"));
            Assert.That(path, Does.EndWith(".glb"));
            Assert.That(path, Does.Contain("MeshyCache"));
        }

        [Test]
        public void TryBuildGenerateRequest_RespectsSaveFormatToggles()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("imageUrl", "https://example.com/cup.png");
            data.SetRaw("saveGlb", false);
            data.SetRaw("saveFbx", true);

            var ok = PcgMeshyResolver.TryBuildGenerateRequest(
                "nodeA", data, out var request, out var path, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.TargetFormats, Is.EquivalentTo(new[] { "fbx" }));
            Assert.That(path, Does.EndWith(".fbx"));
        }

        [Test]
        public void ReadSelected_DefaultsToGlbOnly()
        {
            var selected = PcgMeshySaveFormats.ReadSelected(new PcgNodeData());
            Assert.That(selected, Is.EquivalentTo(new[] { "glb" }));
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

        [Test]
        public void EndpointConstants_MatchMeshyDocumentedPaths()
        {
            Assert.That(PcgMeshyClient.ImageTo3dPath, Is.EqualTo("/openapi/v1/image-to-3d"));
            Assert.That(PcgMeshyClient.MultiImageTo3dPath, Is.EqualTo("/openapi/v1/multi-image-to-3d"));
            Assert.That(PcgMeshyClient.TextTo3dPath, Is.EqualTo("/openapi/v2/text-to-3d"));
            Assert.That(PcgMeshyClient.RemeshPath, Is.EqualTo("/openapi/v1/remesh"));
            Assert.That(PcgMeshyClient.ResizePath, Is.EqualTo("/openapi/v1/resize"));
            Assert.That(PcgMeshyClient.UvUnwrapPath, Is.EqualTo("/openapi/v1/uv-unwrap"));
            Assert.That(PcgMeshyClient.RetexturePath, Is.EqualTo("/openapi/v1/retexture"));
            Assert.That(PcgMeshyClient.TextToImagePath, Is.EqualTo("/openapi/v1/text-to-image"));
            Assert.That(PcgMeshyClient.ImageToImagePath, Is.EqualTo("/openapi/v1/image-to-image"));
        }

        [Test]
        public void TextTo3D_TryBuildGenerateRequest_WithoutApiKey_FailsWithKeyError()
        {
            PcgMeshySettings.ClearApiKey();

            var ok = PcgMeshyTextTo3DResolver.TryBuildGenerateRequest(
                "nodeA", new PcgNodeData(), out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("API key missing"));
        }

        [Test]
        public void TextTo3D_TryBuildGenerateRequest_RequiresPrompt()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var ok = PcgMeshyTextTo3DResolver.TryBuildGenerateRequest(
                "nodeA", new PcgNodeData(), out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("Prompt"));
        }

        [Test]
        public void TextTo3D_TryBuildGenerateRequest_BuildsDefaults()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("prompt", "a monster mask");

            var ok = PcgMeshyTextTo3DResolver.TryBuildGenerateRequest(
                "nodeA", data, out var request, out var path, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.Prompt, Is.EqualTo("a monster mask"));
            Assert.That(request.AiModel, Is.EqualTo("latest"));
            Assert.That(request.ModelType, Is.EqualTo("standard"));
            Assert.That(request.ShouldRemesh, Is.True);
            Assert.That(request.Topology, Is.EqualTo("triangle"));
            Assert.That(request.TargetPolycount, Is.EqualTo(30000));
            Assert.That(request.ShouldTexture, Is.True);
            Assert.That(request.TextureResolution, Is.EqualTo("2k"));
            Assert.That(request.RemoveLighting, Is.True);
            Assert.That(path, Does.EndWith(".glb"));
            Assert.That(path, Does.Contain("MeshyCache"));
        }

        [Test]
        public void MeshOps_FormatsFor_OnlyRemeshHonorsSaveToggles()
        {
            var remesh = new PcgNodeData();
            remesh.SetRaw("operation", "remesh");
            remesh.SetRaw("saveGlb", false);
            remesh.SetRaw("saveFbx", true);
            Assert.That(
                PcgMeshyMeshOpsResolver.FormatsFor(remesh),
                Is.EquivalentTo(new[] { "fbx" }));

            var resize = new PcgNodeData();
            resize.SetRaw("operation", "resize");
            resize.SetRaw("saveGlb", false);
            resize.SetRaw("saveFbx", true);
            Assert.That(
                PcgMeshyMeshOpsResolver.FormatsFor(resize),
                Is.EquivalentTo(new[] { "glb" }));

            var uv = new PcgNodeData();
            uv.SetRaw("operation", "uvUnwrap");
            Assert.That(
                PcgMeshyMeshOpsResolver.FormatsFor(uv),
                Is.EquivalentTo(new[] { "glb" }));
        }

        [Test]
        public void MeshOps_TryBuildGenerateRequest_RequiresUpstreamModel()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var ok = PcgMeshyMeshOpsResolver.TryBuildGenerateRequest(
                "nodeA", new PcgNodeData(), null, out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("upstream"));
        }

        [Test]
        public void MeshOps_TryBuildGenerateRequest_ResizeHeightMustBePositive()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("operation", "resize");
            data.SetRaw("resizeMode", "height");
            data.SetRaw("resizeHeight", 0.0);

            var ok = PcgMeshyMeshOpsResolver.TryBuildGenerateRequest(
                "nodeA", data, "data:application/octet-stream;base64,AAAA",
                out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("Resize Height"));
        }

        [Test]
        public void MeshOps_TryBuildGenerateRequest_RemeshBuildsRequest()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("operation", "remesh");
            data.SetRaw("topology", "quad");
            data.SetRaw("targetPolycount", 5000);

            var ok = PcgMeshyMeshOpsResolver.TryBuildGenerateRequest(
                "nodeA", data, "data:application/octet-stream;base64,AAAA",
                out var request, out var path, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.Operation, Is.EqualTo("remesh"));
            Assert.That(request.Topology, Is.EqualTo("quad"));
            Assert.That(request.TargetPolycount, Is.EqualTo(5000));
            Assert.That(path, Does.EndWith(".glb"));
        }

        [Test]
        public void Retexture_TryBuildGenerateRequest_RequiresStylePrompt()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var ok = PcgMeshyRetextureResolver.TryBuildGenerateRequest(
                "nodeA", new PcgNodeData(), "data:application/octet-stream;base64,AAAA",
                out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("Text Style Prompt"));
        }

        [Test]
        public void Retexture_TryBuildGenerateRequest_TextStyleBuildsRequest()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("textStylePrompt", "rusty metal");
            data.SetRaw("enablePbr", true);

            var ok = PcgMeshyRetextureResolver.TryBuildGenerateRequest(
                "nodeA", data, "data:application/octet-stream;base64,AAAA",
                out var request, out var path, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.TextStylePrompt, Is.EqualTo("rusty metal"));
            Assert.That(request.ImageStyleDataUri, Is.Null);
            Assert.That(request.EnablePbr, Is.True);
            Assert.That(path, Does.EndWith(".glb"));
        }

        [Test]
        public void ImageGen_TryBuildGenerateRequest_TextToImageBuildsRequest()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("prompt", "stone wall, seamless");

            var ok = PcgMeshyImageGenResolver.TryBuildGenerateRequest(
                "nodeA", data, out var request, out var path, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.ImageToImage, Is.False);
            Assert.That(request.Prompt, Is.EqualTo("stone wall, seamless"));
            Assert.That(request.AiModel, Is.EqualTo("nano-banana"));
            Assert.That(request.AspectRatio, Is.EqualTo("1:1"));
            Assert.That(request.ReferenceImageDataUris, Is.Empty);
            Assert.That(path, Does.EndWith(".png"));
        }

        [Test]
        public void ImageGen_TryBuildGenerateRequest_ImageToImageRequiresReference()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("mode", "imageToImage");
            data.SetRaw("prompt", "make it rainy");

            var ok = PcgMeshyImageGenResolver.TryBuildGenerateRequest(
                "nodeA", data, out _, out _, out var error);

            Assert.That(ok, Is.False);
            Assert.That(error, Does.Contain("Reference Image"));
        }

        [Test]
        public void ImageGen_TryBuildGenerateRequest_ImageToImageWithUrl()
        {
            PcgMeshySettings.ApiKey = "msy_test_key";

            var data = new PcgNodeData();
            data.SetRaw("mode", "imageToImage");
            data.SetRaw("prompt", "make it rainy");
            data.SetRaw("referenceImageUrl", "https://example.com/street.png");

            var ok = PcgMeshyImageGenResolver.TryBuildGenerateRequest(
                "nodeA", data, out var request, out _, out var error);

            Assert.That(ok, Is.True, error);
            Assert.That(request.ImageToImage, Is.True);
            Assert.That(
                request.ReferenceImageDataUris,
                Is.EquivalentTo(new[] { "https://example.com/street.png" }));
        }
    }
}
