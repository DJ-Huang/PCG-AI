using System.IO;
using System.Linq;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgHeightFieldIntegrationTests
    {
        [Test]
        public void TerrainDemo_CooksHighResolutionMeshWithNormalsAndUv()
        {
            var json = ReadExample("terrain-demo.pcg");
            var result = PcgGraphLoader.Execute(json, 42);

            Assert.That(result, Is.Not.Null);
            Assert.That(result.Kind, Is.EqualTo(PcgExecuteKind.Mesh));
            Assert.That(result.VertexCount, Is.EqualTo(257 * 257));
            Assert.That(result.IndexCount, Is.EqualTo(256 * 256 * 6));
            Assert.That(
                PcgResultParser.TryParseMeshBinary(
                    result.MeshBinary, out var mesh, out var error),
                Is.True,
                error);
            Assert.That(mesh.vertexCount, Is.EqualTo(257 * 257));
            Assert.That(mesh.indexFormat, Is.EqualTo(IndexFormat.UInt32));
            Assert.That(mesh.normals.Length, Is.EqualTo(mesh.vertexCount));
            Assert.That(mesh.uv.Length, Is.EqualTo(mesh.vertexCount));
            Object.DestroyImmediate(mesh);
        }

        [Test]
        public void ProjectScatterDemo_CooksTypedPointsWithSurfaceNormals()
        {
            var json = ReadExample("terrain-project-scatter-demo.pcg");
            var result = PcgGraphLoader.Execute(json, 42);

            Assert.That(result, Is.Not.Null);
            Assert.That(result.Kind, Is.EqualTo(PcgExecuteKind.Points));
            Assert.That(result.PointCount, Is.EqualTo(256));
            Assert.That(
                PcgResultParser.TryParsePointBinary(
                    result.PointBinary, out var points, out var error),
                Is.True,
                error);
            Assert.That(points, Has.Count.EqualTo(256));
            Assert.That(points.TrueForAll(point => point.HasNormal), Is.True);
            Assert.That(points.Exists(point => point.Position.y >= 13.99f), Is.True);
        }

        [Test]
        public void HostTerrainBinding_ImportsTypedHeightFieldAndInvalidatesNativeCache()
        {
            var terrainData = new TerrainData
            {
                heightmapResolution = 9,
                size = new Vector3(8f, 10f, 8f),
            };
            var normalized = new float[9, 9];
            for (var z = 0; z < 9; z++)
            {
                for (var x = 0; x < 9; x++)
                    normalized[z, x] = x / 20f;
            }
            terrainData.SetHeights(0, 0, normalized);

            var terrainObject = Terrain.CreateTerrainGameObject(terrainData);
            try
            {
                var graph = HostTerrainGraph();
                var uploads = PcgTerrainResolver.CollectFromGraphJson(
                    graph, terrainObject, null);
                Assert.That(uploads, Has.Count.EqualTo(1));
                Assert.That(uploads[0].Heights[8], Is.EqualTo(4f).Within(2e-4f));
                Assert.That(uploads[0].Mask.All(value => value == 1f), Is.True);

                PcgNative.ClearCookCache();
                var first = PcgGraphLoader.Execute(graph, 42, null, null, null, uploads);
                Assert.That(first, Is.Not.Null);
                Assert.That(first.Kind, Is.EqualTo(PcgExecuteKind.Mesh));
                Assert.That(
                    PcgHeightFieldBinaryParser.TryParse(
                        first.HeightFieldBinary, out var firstSurface, out var firstError),
                    Is.True,
                    firstError);
                Assert.That(firstSurface.Layers["height"].Values[8],
                    Is.EqualTo(4f).Within(2e-4f));

                normalized[4, 4] = 0.9f;
                terrainData.SetHeights(0, 0, normalized);
                uploads = PcgTerrainResolver.CollectFromGraphJson(
                    graph, terrainObject, null);
                var second = PcgGraphLoader.Execute(graph, 42, null, null, null, uploads);
                Assert.That(
                    PcgHeightFieldBinaryParser.TryParse(
                        second.HeightFieldBinary, out var secondSurface, out var secondError),
                    Is.True,
                    secondError);
                var changedIndex = 4 * uploads[0].ResolutionX + 4;
                Assert.That(secondSurface.Layers["height"].Values[changedIndex],
                    Is.EqualTo(9f).Within(2e-4f));
            }
            finally
            {
                Object.DestroyImmediate(terrainObject);
                Object.DestroyImmediate(terrainData);
            }
        }

        [Test]
        public void GraphComponent_TerrainMode_AppliesHeightFieldOnly()
        {
            var terrainData = new TerrainData
            {
                heightmapResolution = 9,
                size = new Vector3(8f, 10f, 8f),
            };
            var terrainObject = Terrain.CreateTerrainGameObject(terrainData);
            terrainObject.transform.position = new Vector3(-4f, 0f, -4f);
            var asset = ScriptableObject.CreateInstance<PcgGraphAsset>();
            try
            {
                asset.SetGraphJson(GeneratedTerrainGraph());
                var component = terrainObject.AddComponent<PcgGraphComponent>();
                component.GraphAsset = asset;
                component.SetHostOutputMode(PcgHostOutputMode.Terrain, requestCook: false);

                PcgGraphCookCache.Clear();
                PcgNative.ClearCookCache();
                Assert.That(component.Run(skipDocumentRefresh: false, forceSynchronous: true), Is.True);
                Assert.That(component.TerrainApplyGeneration, Is.EqualTo(1));
                Assert.That(terrainObject.GetComponent<MeshFilter>()?.sharedMesh, Is.Null,
                    "Terrain host mode must not create a mesh preview.");

                var heights = terrainData.GetHeights(0, 0, 9, 9);
                Assert.That(heights[0, 0], Is.EqualTo(0.2f).Within(2e-5f));
                Assert.That(heights[4, 4], Is.EqualTo(0.2f).Within(2e-5f));
                Assert.That(heights[8, 8], Is.EqualTo(0.2f).Within(2e-5f));

                Assert.That(component.Run(skipDocumentRefresh: true, forceSynchronous: true), Is.True);
                Assert.That(component.TerrainApplyGeneration, Is.EqualTo(1),
                    "Unchanged cook must not write TerrainData again.");
            }
            finally
            {
                Object.DestroyImmediate(terrainObject);
                Object.DestroyImmediate(terrainData);
                Object.DestroyImmediate(asset);
            }
        }

        [Test]
        public void UnityTerrainAdapter_ExplicitlyResamplesMismatchedResolution()
        {
            var terrainData = new TerrainData
            {
                heightmapResolution = 33,
                size = new Vector3(16f, 20f, 16f),
            };
            var terrainObject = Terrain.CreateTerrainGameObject(terrainData);
            var graphSpace = new GameObject("PCG Terrain Resample Space");
            try
            {
                var surface = MakeRampSurface(17, 16f, 10f);
                var adapter = new PcgUnityTerrainAdapter(
                    terrainObject.GetComponent<Terrain>(), graphSpace.transform);
                Assert.That(
                    adapter.TryExportSurface(surface, out var report, out var error),
                    Is.True,
                    error);
                Assert.That(report.Applied, Is.True);
                Assert.That(report.Resampled, Is.True);
                Assert.That(report.ClampedSamples, Is.Zero);

                var heights = terrainData.GetHeights(0, 0, 33, 33);
                Assert.That(heights[0, 0], Is.EqualTo(0f).Within(2e-5f));
                Assert.That(heights[16, 16], Is.EqualTo(0.25f).Within(2e-5f));
                Assert.That(heights[32, 32], Is.EqualTo(0.5f).Within(2e-5f));

                Assert.That(
                    adapter.TryExportSurface(surface, out var repeat, out error),
                    Is.True,
                    error);
                Assert.That(repeat.SkippedUnchanged, Is.True);
                Assert.That(repeat.Applied, Is.False);
            }
            finally
            {
                Object.DestroyImmediate(graphSpace);
                Object.DestroyImmediate(terrainObject);
                Object.DestroyImmediate(terrainData);
            }
        }

        [Test]
        public void HostTerrainFingerprint_IncludesGridTransformMetadata()
        {
            var upload = new PcgHeightFieldUpload
            {
                SlotId = "host",
                ResolutionX = 2,
                ResolutionZ = 2,
                SizeX = 1.0,
                SizeZ = 1.0,
                Heights = new[] { 0f, 0f, 0f, 0f },
                Mask = new[] { 1f, 1f, 1f, 1f },
            };
            var before = PcgTerrainResolver.ComputeFingerprint(new[] { upload });
            upload.CenterX = 3.0;
            var after = PcgTerrainResolver.ComputeFingerprint(new[] { upload });
            Assert.That(after, Is.Not.EqualTo(before));
        }

        [Test]
        public void NativeHeightFieldSidecar_RetriesWhenInitialBufferIsTooSmall()
        {
            const string graph = @"{
  ""version"":""1.0"",
  ""nodes"":[
    {""id"":""base"",""type"":""HeightField"",""data"":{""sizeX"":1024,""sizeZ"":1024,""gridSpacing"":1,""initialHeight"":2,""initialMask"":1}},
    {""id"":""out"",""type"":""Output"",""data"":{}}
  ],
  ""edges"":[{""id"":""e1"",""source"":""base"",""target"":""out""}]
}";

            PcgNative.ClearCookCache();
            var result = PcgGraphLoader.Execute(graph, 42, null, null, null, null);
            Assert.That(result, Is.Not.Null);
            Assert.That(result.HeightFieldBinary, Is.Not.Null);
            Assert.That(result.HeightFieldBinary.Length, Is.GreaterThan(PcgNative.OutHeightFieldBufSize));
            Assert.That(
                PcgHeightFieldBinaryParser.TryParse(
                    result.HeightFieldBinary, out var surface, out var error),
                Is.True,
                error);
            Assert.That(surface.ResolutionX, Is.EqualTo(1025));
            Assert.That(surface.ResolutionZ, Is.EqualTo(1025));
        }

        private static string ReadExample(string fileName)
        {
            var path = Path.Combine(
                Application.dataPath, "PcgPlugin", "Examples", "PCGDemo", fileName);
            Assert.That(File.Exists(path), Is.True, path);
            return File.ReadAllText(path);
        }

        private static PcgHostTerrainSurface MakeRampSurface(
            int resolution,
            float size,
            float maxHeight)
        {
            var heights = new float[resolution * resolution];
            var mask = new float[heights.Length];
            for (var z = 0; z < resolution; z++)
            {
                for (var x = 0; x < resolution; x++)
                {
                    var index = z * resolution + x;
                    heights[index] = x / (float)(resolution - 1) * maxHeight;
                    mask[index] = 1f;
                }
            }

            var surface = new PcgHostTerrainSurface
            {
                ResolutionX = resolution,
                ResolutionZ = resolution,
                SizeX = size,
                SizeZ = size,
                CenterX = size * 0.5,
                CenterY = 0.0,
                CenterZ = size * 0.5,
                Sampling = PcgHostTerrainSampling.Corner,
                Orientation = PcgHostTerrainOrientation.ZX,
            };
            surface.SetLayer(new PcgHostTerrainLayer
            {
                Name = "height",
                TupleSize = 1,
                BorderType = PcgHostTerrainBorderType.Streak,
                Values = heights,
            });
            surface.SetLayer(new PcgHostTerrainLayer
            {
                Name = "mask",
                TupleSize = 1,
                BorderType = PcgHostTerrainBorderType.Streak,
                Values = mask,
            });
            return surface;
        }

        private static string HostTerrainGraph() => @"{
  ""version"":""1.0"",
  ""nodes"":[
    {""id"":""host"",""type"":""GetTerrainData"",""data"":{""bindingKey"":""targetTerrain""}},
    {""id"":""noise"",""type"":""HeightFieldNoise"",""data"":{""amplitude"":0,""elementSize"":2}},
    {""id"":""convert"",""type"":""ConvertHeightField"",""data"":{}},
    {""id"":""out"",""type"":""Output"",""data"":{}}
  ],
  ""edges"":[
    {""id"":""e1"",""source"":""host"",""target"":""noise""},
    {""id"":""e2"",""source"":""noise"",""target"":""convert""},
    {""id"":""e3"",""source"":""convert"",""target"":""out""}
  ]
}";

        private static string GeneratedTerrainGraph() => @"{
  ""version"":""1.0"",
  ""nodes"":[
    {""id"":""base"",""type"":""HeightField"",""data"":{""sizeX"":8,""sizeZ"":8,""gridSpacing"":1,""initialHeight"":2,""initialMask"":1}},
    {""id"":""out"",""type"":""Output"",""data"":{}}
  ],
  ""edges"":[
    {""id"":""e1"",""source"":""base"",""target"":""out""}
  ]
}";
    }
}
