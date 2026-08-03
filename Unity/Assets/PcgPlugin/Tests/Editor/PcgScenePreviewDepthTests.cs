using System.Collections;
using System.IO;
using DJTechEditor.PCG.Rendering;
using UnityEditor;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.TestTools;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgScenePreviewDepthTests
    {
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
            Assert.That(renderer, Does.Contain("ResolveDepthCompare(style.AlwaysOnTop, camera)"));
            Assert.That(renderer, Does.Contain("s_LineMaterial.SetInt(s_ZWriteId, style.AlwaysOnTop ? 0 : 1)"));
        }

        [Test]
        public void PreviewShaders_ImportAndExposePassZero()
        {
            var wire = Shader.Find("Hidden/PcgPolygonWireOverlay");
            var point = Shader.Find("Hidden/PcgPolygonPointOverlay");
            var depth = Shader.Find("Hidden/PcgPolygonDepthOnly");
            Assert.NotNull(wire, "Wire preview shader failed to import.");
            Assert.NotNull(point, "Point preview shader failed to import.");
            Assert.NotNull(depth, "Depth-only preview shader failed to import.");

            var wireMaterial = new Material(wire) { hideFlags = HideFlags.HideAndDontSave };
            var pointMaterial = new Material(point) { hideFlags = HideFlags.HideAndDontSave };
            var depthMaterial = new Material(depth) { hideFlags = HideFlags.HideAndDontSave };
            try
            {
                Assert.IsTrue(wireMaterial.SetPass(0), "Wire preview pass 0 failed to bind.");
                Assert.IsTrue(pointMaterial.SetPass(0), "Point preview pass 0 failed to bind.");
                Assert.IsTrue(depthMaterial.SetPass(0), "Depth-only preview pass 0 failed to bind.");
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(wireMaterial);
                UnityEngine.Object.DestroyImmediate(pointMaterial);
                UnityEngine.Object.DestroyImmediate(depthMaterial);
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
        public void ProjectionDepthCompare_FollowsTheMatrixActuallyUsedByTheShader()
        {
            var cameraObject = new GameObject("PcgScenePreviewProjectionCompareTestCamera");
            try
            {
                var camera = cameraObject.AddComponent<Camera>();
                camera.nearClipPlane = 0.1f;
                camera.farClipPlane = 100f;

                var rawCompare = PcgScenePreviewRenderer.ResolveDepthCompare(
                    alwaysOnTop: false,
                    camera.projectionMatrix,
                    camera.nearClipPlane,
                    camera.farClipPlane);
                var gpuCompare = PcgScenePreviewRenderer.ResolveDepthCompare(
                    alwaysOnTop: false,
                    GL.GetGPUProjectionMatrix(camera.projectionMatrix, true),
                    camera.nearClipPlane,
                    camera.farClipPlane);

                Assert.That(rawCompare, Is.EqualTo(CompareFunction.LessEqual));
                Assert.That(
                    gpuCompare,
                    Is.EqualTo(SystemInfo.usesReversedZBuffer
                        ? CompareFunction.GreaterEqual
                        : CompareFunction.LessEqual));
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

        [UnityTest]
        public IEnumerator SceneViewGpuOverlayShaders_KeepNearLineAndPointVisible()
        {
            AssumeGraphicsAvailable();
            var sceneView = SceneView.lastActiveSceneView;
            Assume.That(sceneView, Is.Not.Null, "SceneView GPU regression requires an open SceneView.");

            var probe = new SceneViewDepthProbe();
            SceneView.duringSceneGui += probe.OnSceneGui;
            SceneView.RepaintAll();
            try
            {
                for (var frame = 0; frame < 20 && !probe.Completed; frame++)
                    yield return null;

                Assert.That(probe.Completed, Is.True, probe.Error ?? "SceneView depth probe did not complete.");
                Assert.That(probe.Error, Is.Null, probe.Error);
                for (var i = 0; i < probe.LineSamples.Length; i++)
                    AssertNearColor(probe.LineSamples[i], "SceneView near line distance index " + i);
                AssertRedColor(probe.CoplanarLineSample, "SceneView coplanar line remains visible over opaque surface");
                AssertNearColor(probe.SurfaceHiddenLineSample, "SceneView opaque surface hides far line");
                AssertNearColor(probe.PointSample, "SceneView opaque surface hides far point");
                AssertRedColor(probe.AlwaysOnTopLineSample, "AlwaysOnTop line remains visible over opaque surface");
                AssertRedColor(probe.AlwaysOnTopPointSample, "AlwaysOnTop point remains visible over opaque surface");
            }
            finally
            {
                SceneView.duringSceneGui -= probe.OnSceneGui;
                probe.Dispose();
            }
        }

        private sealed class SceneViewDepthProbe
        {
            private readonly Mesh m_EdgeMesh;
            private readonly Mesh m_PointMesh;
            private readonly Mesh m_SurfaceMesh;
            private readonly Material m_NearLineMaterial;
            private readonly Material m_FarLineMaterial;
            private readonly Material m_AlwaysOnTopLineMaterial;
            private readonly Material m_PointMaterial;
            private readonly Material m_AlwaysOnTopPointMaterial;
            private readonly Material m_SurfaceMaterial;
            private int m_RepaintCount;

            public readonly Color[] LineSamples = new Color[3];
            public Color CoplanarLineSample { get; private set; }
            public Color SurfaceHiddenLineSample { get; private set; }
            public Color PointSample { get; private set; }
            public Color AlwaysOnTopLineSample { get; private set; }
            public Color AlwaysOnTopPointSample { get; private set; }
            public bool Completed { get; private set; }
            public string Error { get; private set; }

            public SceneViewDepthProbe()
            {
                var edgeData = PcgScenePreviewMeshBuilder.BuildEdges(
                    new[] { new Vector3(-0.4f, 0f, 0f), new Vector3(0.4f, 0f, 0f) },
                    new[] { 0, 1 });
                var pointData = PcgScenePreviewMeshBuilder.BuildPoints(new[] { Vector3.zero });
                m_EdgeMesh = edgeData.CreateMesh("PcgSceneViewDepthProbeEdges");
                m_PointMesh = pointData.CreateMesh("PcgSceneViewDepthProbePoints");
                m_SurfaceMesh = CreateSurfaceMesh();

                var wireShader = Shader.Find("Hidden/PcgPolygonWireOverlay");
                var pointShader = Shader.Find("Hidden/PcgPolygonPointOverlay");
                Assert.NotNull(wireShader, "Wire preview shader is unavailable.");
                Assert.NotNull(pointShader, "Point preview shader is unavailable.");

                m_NearLineMaterial = CreateMaterial(wireShader, "PcgSceneViewDepthProbeNearLine");
                m_FarLineMaterial = CreateMaterial(wireShader, "PcgSceneViewDepthProbeFarLine");
                m_AlwaysOnTopLineMaterial = CreateMaterial(wireShader, "PcgSceneViewDepthProbeAlwaysOnTopLine");
                m_PointMaterial = CreateMaterial(pointShader, "PcgSceneViewDepthProbePoint");
                m_AlwaysOnTopPointMaterial = CreateMaterial(pointShader, "PcgSceneViewDepthProbeAlwaysOnTopPoint");
                m_SurfaceMaterial = CreateSurfaceMaterial();
            }

            public void OnSceneGui(SceneView sceneView)
            {
                if (Completed || Event.current == null || Event.current.type != EventType.Repaint)
                    return;
                if (m_RepaintCount++ > 0)
                    return;

                try
                {
                    var camera = sceneView.camera;
                    var target = RenderTexture.active;
                    if (camera == null || target == null)
                    {
                        Error = "SceneView camera or active render target is unavailable.";
                        Completed = true;
                        return;
                    }

                    var view = camera.worldToCameraMatrix;
                    // SceneView/Tuanjie's shader path expects the raw Camera
                    // projection; its SV_POSITION conversion supplies API depth.
                    var projection = camera.projectionMatrix;
                    var compare = PcgScenePreviewRenderer.ResolveDepthCompare(
                        alwaysOnTop: false,
                        projection,
                        camera.nearClipPlane,
                        camera.farClipPlane);
                    var baseDistance = Mathf.Max(camera.nearClipPlane * 4f, 0.5f);
                    var maxDistance = Mathf.Max(baseDistance, camera.farClipPlane * 0.4f);
                    baseDistance = Mathf.Min(baseDistance, maxDistance / 24f);
                    var distances = new[] { baseDistance, baseDistance * 4f, baseDistance * 16f };

                    GL.PushMatrix();
                    try
                    {
                        for (var i = 0; i < distances.Length; i++)
                        {
                            var nearDistance = distances[i];
                            var farDistance = nearDistance * 1.5f;
                            var localY = 0f;
                            var nearLocalToWorld = camera.cameraToWorldMatrix * Matrix4x4.Translate(
                                new Vector3(0f, localY, -nearDistance));
                            var farLocalToWorld = camera.cameraToWorldMatrix * Matrix4x4.Translate(
                                new Vector3(0f, localY, -farDistance));

                            ConfigureMaterial(
                                m_NearLineMaterial,
                                CompareFunction.Always,
                                Color.green,
                                projection,
                                view,
                                nearLocalToWorld,
                                camera,
                                line: true);
                            ConfigureMaterial(
                                m_FarLineMaterial,
                                compare,
                                Color.red,
                                projection,
                                view,
                                farLocalToWorld,
                                camera,
                                line: true);
                            m_NearLineMaterial.SetPass(0);
                            Graphics.DrawMeshNow(m_EdgeMesh, nearLocalToWorld);
                            m_FarLineMaterial.SetPass(0);
                            Graphics.DrawMeshNow(m_EdgeMesh, farLocalToWorld);
                            LineSamples[i] = ReadPixel(camera, target, new Vector3(0f, localY, -nearDistance));
                        }

                        var pointY = 0f;
                        var pointDistance = distances[0];
                        var surfaceLocalToWorld = camera.cameraToWorldMatrix * Matrix4x4.Translate(
                            new Vector3(0f, pointY, -pointDistance));
                        var farPointLocalToWorld = camera.cameraToWorldMatrix * Matrix4x4.Translate(
                            new Vector3(0f, pointY, -(pointDistance * 1.5f)));

                        m_SurfaceMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_SurfaceMesh, surfaceLocalToWorld);
                        ConfigureMaterial(
                            m_NearLineMaterial,
                            compare,
                            Color.red,
                            projection,
                            view,
                            surfaceLocalToWorld,
                            camera,
                            line: true);
                        m_NearLineMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_EdgeMesh, surfaceLocalToWorld);
                        CoplanarLineSample = ReadPixel(
                            camera,
                            target,
                            new Vector3(0f, pointY, -pointDistance));

                        m_SurfaceMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_SurfaceMesh, surfaceLocalToWorld);
                        ConfigureMaterial(
                            m_FarLineMaterial,
                            compare,
                            Color.red,
                            projection,
                            view,
                            farPointLocalToWorld,
                            camera,
                            line: true);
                        m_FarLineMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_EdgeMesh, farPointLocalToWorld);
                        SurfaceHiddenLineSample = ReadPixel(
                            camera,
                            target,
                            new Vector3(0f, pointY, -pointDistance));

                        m_SurfaceMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_SurfaceMesh, surfaceLocalToWorld);
                        ConfigureMaterial(
                            m_PointMaterial,
                            compare,
                            Color.red,
                            projection,
                            view,
                            farPointLocalToWorld,
                            camera,
                            line: false);
                        m_PointMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_PointMesh, farPointLocalToWorld);
                        PointSample = ReadPixel(camera, target, new Vector3(0f, pointY, -pointDistance));

                        m_SurfaceMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_SurfaceMesh, surfaceLocalToWorld);
                        ConfigureMaterial(
                            m_AlwaysOnTopLineMaterial,
                            CompareFunction.Always,
                            Color.red,
                            projection,
                            view,
                            farPointLocalToWorld,
                            camera,
                            line: true);
                        m_AlwaysOnTopLineMaterial.SetInt("_ZWrite", 0);
                        m_AlwaysOnTopLineMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_EdgeMesh, farPointLocalToWorld);
                        AlwaysOnTopLineSample = ReadPixel(
                            camera,
                            target,
                            new Vector3(0f, pointY, -pointDistance));

                        m_SurfaceMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_SurfaceMesh, surfaceLocalToWorld);
                        ConfigureMaterial(
                            m_AlwaysOnTopPointMaterial,
                            CompareFunction.Always,
                            Color.red,
                            projection,
                            view,
                            farPointLocalToWorld,
                            camera,
                            line: false);
                        m_AlwaysOnTopPointMaterial.SetPass(0);
                        Graphics.DrawMeshNow(m_PointMesh, farPointLocalToWorld);
                        AlwaysOnTopPointSample = ReadPixel(
                            camera,
                            target,
                            new Vector3(0f, pointY, -pointDistance));
                    }
                    finally
                    {
                        GL.PopMatrix();
                    }

                    Completed = true;
                }
                catch (System.Exception exception)
                {
                    Error = exception.ToString();
                    Completed = true;
                }
            }

            public void Dispose()
            {
                UnityEngine.Object.DestroyImmediate(m_NearLineMaterial);
                UnityEngine.Object.DestroyImmediate(m_FarLineMaterial);
                UnityEngine.Object.DestroyImmediate(m_AlwaysOnTopLineMaterial);
                UnityEngine.Object.DestroyImmediate(m_PointMaterial);
                UnityEngine.Object.DestroyImmediate(m_AlwaysOnTopPointMaterial);
                UnityEngine.Object.DestroyImmediate(m_SurfaceMaterial);
                UnityEngine.Object.DestroyImmediate(m_EdgeMesh);
                UnityEngine.Object.DestroyImmediate(m_PointMesh);
                UnityEngine.Object.DestroyImmediate(m_SurfaceMesh);
            }

            private static Material CreateMaterial(Shader shader, string name)
            {
                return new Material(shader)
                {
                    name = name,
                    hideFlags = HideFlags.HideAndDontSave,
                };
            }

            private static void ConfigureMaterial(
                Material material,
                CompareFunction compare,
                Color color,
                Matrix4x4 projection,
                Matrix4x4 view,
                Matrix4x4 localToWorld,
                Camera camera,
                bool line)
            {
                material.SetInt("_ZTest", (int)compare);
                material.SetColor("_Color", color);
                material.SetVector("_Viewport", new Vector4(camera.pixelWidth, camera.pixelHeight, 0f, 0f));
                if (line)
                {
                    material.SetFloat("_LineWidth", 20f);
                    material.SetInt("_ZWrite", 1);
                }
                else
                {
                    material.SetFloat("_PointSize", 24f);
                    material.SetFloat("_UseWorldSize", 0f);
                }
            }

            private static Color ReadPixel(Camera camera, RenderTexture target, Vector3 localPoint)
            {
                var worldPoint = camera.cameraToWorldMatrix.MultiplyPoint(localPoint);
                var screenPoint = camera.WorldToScreenPoint(worldPoint);
                var x = Mathf.Clamp(Mathf.RoundToInt(screenPoint.x), 0, target.width - 1);
                var y = Mathf.Clamp(Mathf.RoundToInt(screenPoint.y), 0, target.height - 1);
                var pixel = new Texture2D(1, 1, TextureFormat.RGBA32, false)
                {
                    hideFlags = HideFlags.HideAndDontSave,
                };
                try
                {
                    pixel.ReadPixels(new Rect(x, y, 1, 1), 0, 0, false);
                    pixel.Apply(false, false);
                    return pixel.GetPixel(0, 0);
                }
                finally
                {
                    UnityEngine.Object.DestroyImmediate(pixel);
                }
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

        private static void AssertRedColor(Color color, string label)
        {
            Assert.That(color.r, Is.GreaterThan(color.g + 0.25f), label + " actual=" + color);
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
