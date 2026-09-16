using System.Collections.Generic;
using System.Reflection;
using DJTechRuntime.PCG;
using DJTechEditor.PCG.Graph;
using NUnit.Framework;
using UnityEngine;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgGeneratedMaterialTests
    {
        private const string TestShaderName = "Hidden/PCG/Tests/MaterialProperties";
        private const string WhitePixelDataUri =
            "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVQIHWP4DwQACfsD/Qy7W+cAAAAASUVORK5CYII=";

        private const string CookJson = @"{
          ""mesh_metadata"": {
            ""pbrMaterials"": {
              ""paint"": {
                ""kind"": ""pcg.material"",
                ""name"": ""paint"",
                ""baseColor"": ""#ff8040"",
                ""metallic"": 0.75,
                ""roughness"": 0.2,
                ""opacity"": 1.0,
                ""alphaMode"": ""opaque"",
                ""doubleSided"": true,
                ""unityShaderName"": ""Missing/PCG/TestShader"",
                ""unityPropertiesJson"": ""{}""
              }
            }
          }
        }";

        [Test]
        public void MaterialManifest_OptsOutOfIsolatedPreview()
        {
            Assert.That(PcgNodeManifest.TryGet("Material", out var material), Is.True);
            Assert.That(material.supportsPreview, Is.False);
        }

        [Test]
        public void ParseDefinitions_ReadsMeshMetadataMaterialLibrary()
        {
            var definitions = PcgGeneratedMaterialSet.ParseDefinitions(CookJson);

            Assert.That(definitions.ContainsKey("paint"), Is.True);
            Assert.That(definitions["paint"]["roughness"], Is.EqualTo(0.2d));
        }

        [Test]
        public void Rebuild_MapsPortablePbrAndInvertsRoughnessToSmoothness()
        {
            var set = new PcgGeneratedMaterialSet();
            try
            {
                set.Rebuild(CookJson);
                Assert.That(set.TryGet("paint", out var material), Is.True);
                Assert.That(material, Is.Not.Null);

                if (material.HasProperty("_Metallic"))
                    Assert.That(material.GetFloat("_Metallic"), Is.EqualTo(0.75f).Within(0.001f));
                if (material.HasProperty("_Smoothness"))
                    Assert.That(material.GetFloat("_Smoothness"), Is.EqualTo(0.8f).Within(0.001f));
                else if (material.HasProperty("_Glossiness"))
                    Assert.That(material.GetFloat("_Glossiness"), Is.EqualTo(0.8f).Within(0.001f));

                var colorProperty = material.HasProperty("_BaseColor") ? "_BaseColor" : "_Color";
                if (material.HasProperty(colorProperty))
                {
                    var color = material.GetColor(colorProperty);
                    Assert.That(color.r, Is.EqualTo(1f).Within(0.01f));
                    Assert.That(color.g, Is.EqualTo(128f / 255f).Within(0.01f));
                }
            }
            finally
            {
                set.Dispose();
            }
        }

        [Test]
        public void Rebuild_CustomShaderPropertiesOverridePortableValues()
        {
            var shader = Shader.Find(TestShaderName);
            Assert.That(shader, Is.Not.Null, $"Test Shader not imported: {TestShaderName}");

            var properties = new Dictionary<string, object>
            {
                ["_Color"] = Property("Color", new List<object> { 0.1f, 0.2f, 0.3f, 0.4f }),
                ["_TestVector"] = Property("Vector", new List<object> { 1f, 2f, 3f, 4f }),
                ["_TestFloat"] = Property("Float", 0.625f),
                ["_TestRange"] = Property("Range", 1.75f),
                ["_TestInt"] = Property("Int", 7),
                ["_MainTex"] = Property("Texture", WhitePixelDataUri),
            };
            var definition = new Dictionary<string, object>
            {
                ["kind"] = "pcg.material",
                ["name"] = "custom",
                ["baseColor"] = "#ff0000",
                ["unityShaderName"] = TestShaderName,
                ["unityPropertiesJson"] = PcgMiniJson.Serialize(properties),
            };
            var root = new Dictionary<string, object>
            {
                ["materials"] = new Dictionary<string, object> { ["custom"] = definition },
            };

            var set = new PcgGeneratedMaterialSet();
            Material material = null;
            Texture texture = null;
            try
            {
                set.Rebuild(PcgMiniJson.Serialize(root));
                Assert.That(set.TryGet("custom", out material), Is.True);
                Assert.That(material.shader, Is.SameAs(shader));
                var color = material.GetColor("_Color");
                Assert.That(color.r, Is.EqualTo(0.1f).Within(0.001f));
                Assert.That(color.g, Is.EqualTo(0.2f).Within(0.001f));
                Assert.That(color.b, Is.EqualTo(0.3f).Within(0.001f));
                Assert.That(color.a, Is.EqualTo(0.4f).Within(0.001f));
                Assert.That(material.GetVector("_TestVector"), Is.EqualTo(new Vector4(1f, 2f, 3f, 4f)));
                Assert.That(material.GetFloat("_TestFloat"), Is.EqualTo(0.625f).Within(0.001f));
                Assert.That(material.GetFloat("_TestRange"), Is.EqualTo(1.75f).Within(0.001f));
                Assert.That(material.GetInt("_TestInt"), Is.EqualTo(7));
                texture = material.GetTexture("_MainTex");
                Assert.That(texture, Is.Not.Null);
                Assert.That(texture, Is.TypeOf<Texture2D>());
                Assert.That(((Texture2D)texture).width, Is.EqualTo(1));
                Assert.That(((Texture2D)texture).height, Is.EqualTo(1));
            }
            finally
            {
                set.Dispose();
            }

            Assert.That(material == null, Is.True, "Generated Material should be destroyed on dispose.");
            Assert.That(texture == null, Is.True, "Owned data-URI Texture should be destroyed on dispose.");
        }

        [Test]
        public void Rebuild_MissingCustomShaderFallsBackToPortableShader()
        {
            var set = new PcgGeneratedMaterialSet();
            try
            {
                set.Rebuild(CookJson);
                Assert.That(set.TryGet("paint", out var material), Is.True);
                Assert.That(material.shader.name, Is.Not.EqualTo("Missing/PCG/TestShader"));
                Assert.That(new[]
                {
                    "Universal Render Pipeline/Lit",
                    "Standard",
                    "Hidden/InternalErrorShader",
                }, Does.Contain(material.shader.name));
            }
            finally
            {
                set.Dispose();
            }
        }

        [Test]
        public void ResolveMaterialBindings_ExplicitThenGeneratedThenFallback()
        {
            var shader = Shader.Find("Hidden/InternalErrorShader") ?? Shader.Find("Standard");
            Assert.That(shader, Is.Not.Null);
            var fallback = new Material(shader) { name = "Fallback" };
            var generated = new Material(shader) { name = "Generated" };
            var explicitMaterial = new Material(shader) { name = "Explicit" };
            try
            {
                var generatedMaterials = new Dictionary<string, Material>
                {
                    ["paint"] = generated,
                    ["roof"] = generated,
                };
                var bindings = new List<PcgMaterialBinding>
                {
                    new() { materialName = "paint", material = explicitMaterial },
                };

                var resolved = PcgGraphComponent.ResolveMaterialBindings(
                    new[] { "paint", "roof", "missing" },
                    3,
                    bindings,
                    generatedMaterials,
                    fallback);

                Assert.That(resolved[0], Is.SameAs(explicitMaterial));
                Assert.That(resolved[1], Is.SameAs(generated));
                Assert.That(resolved[2], Is.SameAs(fallback));
            }
            finally
            {
                Object.DestroyImmediate(explicitMaterial);
                Object.DestroyImmediate(generated);
                Object.DestroyImmediate(fallback);
            }
        }

        [Test]
        public void DisableEnable_KeepsGeneratedMaterialsAliveUntilDestroy()
        {
            var shader = Shader.Find("Hidden/InternalErrorShader") ?? Shader.Find("Standard");
            Assert.That(shader, Is.Not.Null);
            var gameObject = new GameObject("PCG generated material lifetime test");
            var previousDefer = PcgGraphComponent.EditorShouldDeferPreviewCookOnEnable;
            try
            {
                PcgGraphComponent.EditorShouldDeferPreviewCookOnEnable = () => true;
                var renderer = gameObject.AddComponent<MeshRenderer>();
                var component = gameObject.AddComponent<PcgGraphComponent>();
                var set = new PcgGeneratedMaterialSet();
                var material = new Material(shader) { name = "Generated" };
                SetGeneratedMaterial(set, "paint", material);
                SetGeneratedMaterialSet(component, set);
                renderer.sharedMaterial = material;

                component.enabled = false;
                component.enabled = true;
                gameObject.SetActive(false);
                gameObject.SetActive(true);

                Assert.That(material == null, Is.False);
                Assert.That(renderer.sharedMaterial, Is.SameAs(material));
                Object.DestroyImmediate(gameObject);
                Assert.That(material == null, Is.True, "Generated material should be released on destroy.");
                gameObject = null;
            }
            finally
            {
                PcgGraphComponent.EditorShouldDeferPreviewCookOnEnable = previousDefer;
                if (gameObject != null)
                    Object.DestroyImmediate(gameObject);
            }
        }

        [TestCase("pcg-resource://textures/wooden-cabin/cedar-clapboard-albedo.png", "textures/wooden-cabin/cedar-clapboard-albedo")]
        [TestCase("/assets/textures/wooden-cabin/cedar-clapboard-albedo.png", "textures/wooden-cabin/cedar-clapboard-albedo")]
        [TestCase("PCG-RESOURCE://textures/wood.png", "textures/wood")]
        public void PortableResourceLocator_MapsToResourcesKey(string storage, string expected)
        {
            Assert.That(PcgTextureAssetUtil.TryGetPortableResourceKey(storage, out var key), Is.True);
            Assert.That(key, Is.EqualTo(expected));
        }

        [TestCase("https://example.com/assets/textures/wood.png")]
        [TestCase("pcg-resource://../secret.png")]
        [TestCase("")]
        public void PortableResourceLocator_RejectsNonPortableOrUnsafeValues(string storage)
        {
            Assert.That(PcgTextureAssetUtil.TryGetPortableResourceKey(storage, out _), Is.False);
        }

        [Test]
        public void Rebuild_LoadsWebAssetLocatorFromUnityResources()
        {
            var definition = new Dictionary<string, object>
            {
                ["kind"] = "pcg.material",
                ["name"] = "cabin_wood",
                ["baseColorMap"] = "/assets/textures/wooden-cabin/cedar-clapboard-albedo.png",
            };
            var root = new Dictionary<string, object>
            {
                ["materials"] = new Dictionary<string, object> { ["cabin_wood"] = definition },
            };
            var set = new PcgGeneratedMaterialSet();
            try
            {
                set.Rebuild(PcgMiniJson.Serialize(root));
                Assert.That(set.TryGet("cabin_wood", out var material), Is.True);
                var property = material.HasProperty("_BaseMap") ? "_BaseMap" : "_MainTex";
                Assert.That(material.GetTexture(property), Is.Not.Null);
            }
            finally
            {
                set.Dispose();
            }
        }

        private static void SetGeneratedMaterial(PcgGeneratedMaterialSet set, string name, Material material)
        {
            var field = typeof(PcgGeneratedMaterialSet).GetField("m_Materials", BindingFlags.Instance | BindingFlags.NonPublic);
            Assert.That(field, Is.Not.Null);
            var materials = (Dictionary<string, Material>)field.GetValue(set);
            materials[name] = material;
        }

        private static void SetGeneratedMaterialSet(PcgGraphComponent component, PcgGeneratedMaterialSet set)
        {
            var field = typeof(PcgGraphComponent).GetField("m_GeneratedMaterialSet", BindingFlags.Instance | BindingFlags.NonPublic);
            Assert.That(field, Is.Not.Null);
            field.SetValue(component, set);
        }

        private static Dictionary<string, object> Property(string type, object value)
        {
            return new Dictionary<string, object>
            {
                ["type"] = type,
                ["value"] = value,
            };
        }
    }
}
