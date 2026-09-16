using System.Collections.Generic;
using DJTechEditor.PCG.Graph;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgMaterialShaderInspectorTests
    {
        [Test]
        public void ExtractProperties_CapturesEditableShaderParameters()
        {
            var shader = Shader.Find("Standard")
                         ?? Shader.Find("Universal Render Pipeline/Lit")
                         ?? Shader.Find("Hidden/InternalErrorShader");
            Assert.That(shader, Is.Not.Null);

            var json = PcgMaterialShaderInspector.ExtractProperties(shader, "{}");
            var properties = PcgMiniJson.Deserialize(json) as Dictionary<string, object>;

            Assert.That(properties, Is.Not.Null);
            Assert.That(properties.Count, Is.GreaterThan(0));
            foreach (var raw in properties.Values)
            {
                var property = raw as Dictionary<string, object>;
                Assert.That(property, Is.Not.Null);
                Assert.That(property.ContainsKey("type"), Is.True);
                Assert.That(property.ContainsKey("value"), Is.True);
            }
        }

        [Test]
        public void ExtractProperties_ProjectShaderCapturesAllSupportedTypesAndSkipsHidden()
        {
            var shader = Shader.Find("Hidden/PCG/Tests/MaterialProperties");
            Assert.That(shader, Is.Not.Null);

            var properties = Parse(PcgMaterialShaderInspector.ExtractProperties(shader, "{}"));

            AssertType(properties, "_Color", "Color");
            AssertType(properties, "_TestVector", "Vector");
            AssertType(properties, "_TestFloat", "Float");
            AssertType(properties, "_TestRange", "Range");
            // This Unity Shader importer reports ShaderLab Int properties as Float.
            // The inspector follows ShaderUtil's reflected type, while the runtime
            // material path still accepts explicit Int entries from engines that expose it.
            AssertType(properties, "_TestInt", "Float");
            AssertType(properties, "_MainTex", "Texture");
            Assert.That(properties.ContainsKey("_HiddenValue"), Is.False);

            var range = (Dictionary<string, object>)properties["_TestRange"];
            Assert.That(System.Convert.ToDouble(range["min"]), Is.EqualTo(0d).Within(0.001d));
            Assert.That(System.Convert.ToDouble(range["max"]), Is.EqualTo(2d).Within(0.001d));
        }

        [Test]
        public void ExtractProperties_ShaderSwitchPreservesOnlySameNameAndType()
        {
            var source = Shader.Find("Hidden/PCG/Tests/MaterialProperties");
            var target = Shader.Find("Hidden/PCG/Tests/MaterialPropertiesVariant");
            Assert.That(source, Is.Not.Null);
            Assert.That(target, Is.Not.Null);

            var previous = Parse(PcgMaterialShaderInspector.ExtractProperties(source, "{}"));
            ((Dictionary<string, object>)previous["_Color"])["value"] =
                new List<object> { 0.2f, 0.3f, 0.4f, 0.5f };
            ((Dictionary<string, object>)previous["_TestFloat"])["value"] = 0.33f;
            ((Dictionary<string, object>)previous["_TestRange"])["value"] = 1.9f;

            var switched = Parse(PcgMaterialShaderInspector.ExtractProperties(
                target,
                PcgMiniJson.Serialize(previous)));

            var color = (List<object>)((Dictionary<string, object>)switched["_Color"])["value"];
            Assert.That(System.Convert.ToDouble(color[0]), Is.EqualTo(0.2d).Within(0.001d));
            Assert.That(System.Convert.ToDouble(((Dictionary<string, object>)switched["_TestFloat"])["value"]),
                Is.EqualTo(0.33d).Within(0.001d));
            Assert.That(System.Convert.ToDouble(((Dictionary<string, object>)switched["_TestRange"])["value"]),
                Is.EqualTo(1.25d).Within(0.001d),
                "A same-name property with a different type must reset to the new Shader default.");
        }

        private static Dictionary<string, object> Parse(string json)
        {
            return PcgMiniJson.Deserialize(json) as Dictionary<string, object>;
        }

        private static void AssertType(
            IReadOnlyDictionary<string, object> properties,
            string propertyName,
            string expectedType)
        {
            Assert.That(properties.ContainsKey(propertyName), Is.True, propertyName);
            var property = properties[propertyName] as Dictionary<string, object>;
            Assert.That(property, Is.Not.Null, propertyName);
            Assert.That(property["type"], Is.EqualTo(expectedType), propertyName);
        }
    }
}
