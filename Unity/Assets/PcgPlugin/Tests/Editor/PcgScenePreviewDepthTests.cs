using System.IO;
using DJTechEditor.PCG.Rendering;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgScenePreviewDepthTests
    {
        private const int RenderSize = 32;
        private const float NearDistance = 1f;
        private const float FarDistance = 2f;

        [TestCase(false, false, CompareFunction.LessEqual)]
        [TestCase(false, true, CompareFunction.GreaterEqual)]
        [TestCase(true, false, CompareFunction.Always)]
        [TestCase(true, true, CompareFunction.Always)]
        public void ResolveDepthCompare_UsesOnePolicyForBothDepthConventions(
            bool alwaysOnTop,
            bool usesReversedZBuffer,
            CompareFunction expected)
        {
            Assert.That(
                PcgScenePreviewRenderer.ResolveDepthCompare(alwaysOnTop, usesReversedZBuffer),
                Is.EqualTo(expected));
        }

        [Test]
        public void PreviewShaders_UseUnitOffsetAndDoNotContainDistanceScaledBias()
        {
            var wire = ReadProductFile("Editor/Shaders/PcgPolygonWireOverlay.shader");
            var point = ReadProductFile("Editor/Shaders/PcgPolygonPointOverlay.shader");
            var renderer = ReadProductFile("Editor/Rendering/PcgScenePreviewRenderer.cs");

            AssertShaderDepthContract(wire, requireCoverageDiscard: true);
            AssertShaderDepthContract(point, requireCoverageDiscard: false);
            Assert.That(wire, Does.Contain("ZWrite [_ZWrite]"));
            Assert.That(point, Does.Contain("ZWrite Off"));
            Assert.That(renderer, Does.Not.Contain("DepthBias"));
            Assert.That(renderer, Does.Not.Contain("0.0005"));
            Assert.That(renderer, Does.Contain("ResolveDepthCompare(style.AlwaysOnTop)"));
            Assert.That(renderer, Does.Contain("s_LineMaterial.SetInt(s_ZWriteId, style.AlwaysOnTop ? 0 : 1)"));
        }

        [Test]
        public void PreviewShaders_ImportAndExposePassZero()
        {
            var wire = Shader.Find("Hidden/PcgPolygonWireOverlay");
            var point = Shader.Find("Hidden/PcgPolygonPointOverlay");
            Assert.NotNull(wire, "Wire preview shader failed to import.");
            Assert.NotNull(point, "Point preview shader failed to import.");

            var wireMaterial = new Material(wire) { hideFlags = HideFlags.HideAndDontSave };
            var pointMaterial = new Material(point) { hideFlags = HideFlags.HideAndDontSave };
            try
            {
                Assert.IsTrue(wireMaterial.SetPass(0), "Wire preview pass 0 failed to bind.");
                Assert.IsTrue(pointMaterial.SetPass(0), "Point preview pass 0 failed to bind.");
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(wireMaterial);
                UnityEngine.Object.DestroyImmediate(pointMaterial);
            }
        }

        [Test]
        public void GpuProjection_OrdersNearAndFarDepthForCurrentPlatform()
        {
            AssumeGraphicsAvailable();

            var cameraObject = new GameObject("PcgScenePreviewDepthProjectionTestCamera");
            try
            {
                var camera = cameraObject.AddComponent<Camera>();
                camera.orthographic = true;
                camera.orthographicSize = 2f;
                camera.nearClipPlane = 0.1f;
                camera.farClipPlane = 10f;
                camera.transform.position = Vector3.zero;
                camera.transform.rotation = Quaternion.identity;

                var projection = GL.GetGPUProjectionMatrix(camera.projectionMatrix, true);
                var nearDepth = ProjectDepth(projection, NearDistance);
                var farDepth = ProjectDepth(projection, FarDistance);

                if (SystemInfo.usesReversedZBuffer)
                    Assert.That(nearDepth, Is.GreaterThan(farDepth));
                else
                    Assert.That(nearDepth, Is.LessThan(farDepth));
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(cameraObject);
            }
        }

        [Test]
        public void LegacyClipBias_ModelGrowsWithPerspectiveDistance()
        {
            AssumeGraphicsAvailable();

            var cameraObject = new GameObject("PcgScenePreviewLegacyBiasTestCamera");
            try
            {
                var camera = cameraObject.AddComponent<Camera>();
                camera.orthographic = false;
                camera.fieldOfView = 60f;
                camera.aspect = 1f;
                camera.nearClipPlane = 0.08480565f;
                camera.farClipPlane = 16961.13f;

                var projection = GL.GetGPUProjectionMatrix(camera.projectionMatrix, true);
                var previousPullback = 0f;
                foreach (var distance in new[] { 1f, 5f, 10f, 25f, 50f, 100f })
                {
                    var pullback = EstimateLegacyClipBiasPullback(
                        projection,
                        distance,
                        SystemInfo.usesReversedZBuffer);
                    Assert.That(
                        pullback,
                        Is.GreaterThan(previousPullback),
                        "Legacy clip bias pullback did not grow at " + distance + " m.");
                    if (distance >= 25f)
                        Assert.That(pullback, Is.GreaterThan(1f));
                    previousPullback = pullback;
                }
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(cameraObject);
            }
        }

        [TestCase(1f, 2f)]
        [TestCase(5f, 25f)]
        [TestCase(25f, 50f)]
        [TestCase(50f, 100f)]
        public void GpuOverlayShaders_KeepNearLineAndPointVisible(
            float nearDistance,
            float farDistance)
        {
            AssumeGraphicsAvailable();

            var compare = PcgScenePreviewRenderer.ResolveDepthCompare(
                alwaysOnTop: false,
                usesReversedZBuffer: SystemInfo.usesReversedZBuffer);
            var alwaysOnTopCompare = PcgScenePreviewRenderer.ResolveDepthCompare(
                alwaysOnTop: true,
                usesReversedZBuffer: SystemInfo.usesReversedZBuffer);
            Assert.That(alwaysOnTopCompare, Is.EqualTo(CompareFunction.Always));

            var edgeData = PcgScenePreviewMeshBuilder.BuildEdges(
                new[] { new Vector3(-0.8f, 0f, 0f), new Vector3(0.8f, 0f, 0f) },
                new[] { 0, 1 });
            var pointData = PcgScenePreviewMeshBuilder.BuildPoints(new[] { Vector3.zero });
            var edgeMesh = edgeData.CreateMesh("PcgScenePreviewDepthTestEdges");
            var pointMesh = pointData.CreateMesh("PcgScenePreviewDepthTestPoints");
            try
            {
                var nearLine = RenderOverlayPair(
                    Shader.Find("Hidden/PcgPolygonWireOverlay"),
                    edgeMesh,
                    compare,
                    writeDepth: true,
                    line: true,
                    nearDistance,
                    farDistance);
                var nearPoint = RenderOverlayPair(
                    Shader.Find("Hidden/PcgPolygonPointOverlay"),
                    pointMesh,
                    compare,
                    writeDepth: false,
                    line: false,
                    nearDistance,
                    farDistance);

                AssertNearColor(nearLine, "near line");
                AssertNearColor(nearPoint, "near point");
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(edgeMesh);
                UnityEngine.Object.DestroyImmediate(pointMesh);
            }
        }

        private static void AssertShaderDepthContract(string source, bool requireCoverageDiscard)
        {
            Assert.That(source, Does.Contain("Offset 0, -1"));
            Assert.That(source, Does.Not.Contain("_DepthBias"));
            Assert.That(source, Does.Not.Contain("0.0005"));
            Assert.That(source, Does.Not.Contain("clip.z"));
            if (requireCoverageDiscard)
                Assert.That(source, Does.Contain("clip(coverage - 1e-4)"));
        }

        private static Color RenderOverlayPair(
            Shader shader,
            Mesh mesh,
            CompareFunction compare,
            bool writeDepth,
            bool line,
            float nearDistance,
            float farDistance)
        {
            Assert.NotNull(shader, line ? "Wire shader is unavailable." : "Point shader is unavailable.");
            Assert.NotNull(mesh, line ? "Wire test mesh is unavailable." : "Point test mesh is unavailable.");

            var target = new RenderTexture(RenderSize, RenderSize, 24, RenderTextureFormat.ARGB32)
            {
                name = "PcgScenePreviewDepthTestTarget",
                hideFlags = HideFlags.HideAndDontSave,
            };
            var nearMaterial = new Material(shader)
            {
                name = line
                    ? "PcgScenePreviewDepthTestNearWireMaterial"
                    : "PcgScenePreviewDepthTestNearPointMaterial",
                hideFlags = HideFlags.HideAndDontSave,
            };
            var farMaterial = new Material(shader)
            {
                name = line
                    ? "PcgScenePreviewDepthTestFarWireMaterial"
                    : "PcgScenePreviewDepthTestFarPointMaterial",
                hideFlags = HideFlags.HideAndDontSave,
            };
            var cameraObject = new GameObject("PcgScenePreviewDepthTestCamera");
            var camera = cameraObject.AddComponent<Camera>();
            var previousActive = RenderTexture.active;
            Mesh surfaceMesh = null;
            Material surfaceMaterial = null;

            try
            {
                target.Create();
                camera.enabled = false;
                camera.orthographic = true;
                camera.orthographicSize = 2f;
                camera.nearClipPlane = 0.1f;
                camera.farClipPlane = Mathf.Max(10f, farDistance + 1f);
                camera.clearFlags = CameraClearFlags.SolidColor;
                camera.backgroundColor = Color.black;
                camera.cullingMask = 0;
                camera.targetTexture = target;
                ConfigureOverlayMaterial(nearMaterial, compare, writeDepth, line, Color.green);
                ConfigureOverlayMaterial(farMaterial, compare, writeDepth, line, Color.red);

                RenderTexture.active = target;
                GL.Clear(
                    true,
                    true,
                    Color.black,
                    SystemInfo.usesReversedZBuffer ? 0f : 1f);
                GL.PushMatrix();
                try
                {
                    GL.LoadProjectionMatrix(GL.GetGPUProjectionMatrix(camera.projectionMatrix, true));
                    GL.modelview = camera.worldToCameraMatrix;
                    if (!line)
                    {
                        surfaceMesh = CreateSurfaceMesh();
                        surfaceMaterial = CreateSurfaceMaterial();
                        surfaceMaterial.SetPass(0);
                        Graphics.DrawMeshNow(
                            surfaceMesh,
                            Matrix4x4.Translate(new Vector3(0f, 0f, -NearDistance)));
                    }

                    if (line)
                    {
                        nearMaterial.SetPass(0);
                        Graphics.DrawMeshNow(
                            mesh,
                            Matrix4x4.Translate(new Vector3(0f, 0f, -NearDistance)));
                        farMaterial.SetPass(0);
                        Graphics.DrawMeshNow(
                            mesh,
                            Matrix4x4.Translate(new Vector3(0f, 0f, -FarDistance)));
                    }
                    else
                    {
                        farMaterial.SetPass(0);
                        Graphics.DrawMeshNow(
                            mesh,
                            Matrix4x4.Translate(new Vector3(0f, 0f, -FarDistance)));
                    }
                }
                finally
                {
                    GL.PopMatrix();
                }

                var pixel = new Texture2D(1, 1, TextureFormat.RGBA32, false)
                {
                    hideFlags = HideFlags.HideAndDontSave,
                };
                try
                {
                    pixel.ReadPixels(
                        new Rect(RenderSize / 2, RenderSize / 2, 1, 1),
                        0,
                        0,
                        false);
                    pixel.Apply(false, false);
                    return pixel.GetPixel(0, 0);
                }
                finally
                {
                    UnityEngine.Object.DestroyImmediate(pixel);
                }
            }
            finally
            {
                RenderTexture.active = previousActive;
                UnityEngine.Object.DestroyImmediate(nearMaterial);
                UnityEngine.Object.DestroyImmediate(farMaterial);
                UnityEngine.Object.DestroyImmediate(surfaceMaterial);
                UnityEngine.Object.DestroyImmediate(surfaceMesh);
                UnityEngine.Object.DestroyImmediate(cameraObject);
                target.Release();
                UnityEngine.Object.DestroyImmediate(target);
            }
        }

        private static void ConfigureOverlayMaterial(
            Material material,
            CompareFunction compare,
            bool writeDepth,
            bool line,
            Color color)
        {
            material.SetInt("_ZTest", (int)compare);
            material.SetColor("_Color", color);
            material.SetVector("_Viewport", new Vector4(RenderSize, RenderSize, 0f, 0f));
            if (line)
            {
                material.SetFloat("_LineWidth", 12f);
                material.SetInt("_ZWrite", writeDepth ? 1 : 0);
            }
            else
            {
                material.SetFloat("_PointSize", 18f);
                material.SetFloat("_UseWorldSize", 0f);
            }
        }

        private static Mesh CreateSurfaceMesh()
        {
            var mesh = new Mesh
            {
                name = "PcgScenePreviewDepthTestSurface",
                hideFlags = HideFlags.HideAndDontSave,
            };
            mesh.vertices = new[]
            {
                new Vector3(-1.5f, -1.5f, 0f),
                new Vector3(-1.5f, 1.5f, 0f),
                new Vector3(1.5f, -1.5f, 0f),
                new Vector3(1.5f, 1.5f, 0f),
            };
            mesh.triangles = new[] { 0, 1, 2, 2, 1, 3 };
            mesh.RecalculateBounds();
            return mesh;
        }

        private static Material CreateSurfaceMaterial()
        {
            var shader = Shader.Find("Hidden/Internal-Colored");
            Assert.NotNull(shader, "Opaque depth test shader is unavailable.");
            var material = new Material(shader)
            {
                name = "PcgScenePreviewDepthTestSurfaceMaterial",
                hideFlags = HideFlags.HideAndDontSave,
            };
            material.SetInt("_ZTest", (int)CompareFunction.Always);
            material.SetInt("_ZWrite", 1);
            material.SetInt("_Cull", (int)CullMode.Off);
            material.SetInt("_SrcBlend", (int)BlendMode.One);
            material.SetInt("_DstBlend", (int)BlendMode.Zero);
            material.SetColor("_Color", Color.green);
            return material;
        }

        private static float ProjectDepth(Matrix4x4 projection, float distance)
        {
            var clip = projection * new Vector4(0f, 0f, -distance, 1f);
            return clip.z / clip.w;
        }

        private static float EstimateLegacyClipBiasPullback(
            Matrix4x4 projection,
            float distance,
            bool usesReversedZBuffer)
        {
            var clip = projection * new Vector4(0f, 0f, -distance, 1f);
            clip.z += (usesReversedZBuffer ? 1f : -1f) * 0.0005f * clip.w;
            var biasedView = projection.inverse * clip;
            var biasedDistance = -biasedView.z / biasedView.w;
            return distance - biasedDistance;
        }

        private static void AssertNearColor(Color color, string label)
        {
            Assert.That(color.g, Is.GreaterThan(color.r + 0.25f), label + " actual=" + color);
        }

        private static void AssumeGraphicsAvailable()
        {
            Assume.That(
                SystemInfo.graphicsDeviceType,
                Is.Not.EqualTo(GraphicsDeviceType.Null),
                "GPU depth regression requires an active graphics device.");
        }

        private static string ReadProductFile(string relativePath)
        {
            var path = Path.Combine(Application.dataPath, "PcgPlugin", relativePath);
            Assert.That(File.Exists(path), Is.True, "Missing product file: " + path);
            return File.ReadAllText(path);
        }
    }
}
