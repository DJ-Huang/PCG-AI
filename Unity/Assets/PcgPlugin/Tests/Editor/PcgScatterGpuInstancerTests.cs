using System.Collections.Generic;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechEditor.PCG.Tests
{
    public sealed class PcgScatterGpuInstancerTests
    {
        [Test]
        public void BuildDrawCommands_TwoNonEmptySubmeshes_PreservesRangesAndIndices()
        {
            var mesh = BuildMeshWithSubmeshes(
                new[] { 3u, 6u },
                new[] { 0u, 3u },
                new[] { 0, 10 });

            var commands = PcgScatterGpuInstancer.BuildDrawCommands(mesh);

            Assert.AreEqual(2, commands.Length);
            Assert.AreEqual(0, commands[0].SubMeshIndex);
            Assert.AreEqual(3u, commands[0].IndexCount);
            Assert.AreEqual(0u, commands[0].StartIndex);
            Assert.AreEqual(0u, commands[0].BaseVertexIndex);
            Assert.AreEqual(1, commands[1].SubMeshIndex);
            Assert.AreEqual(6u, commands[1].IndexCount);
            Assert.AreEqual(3u, commands[1].StartIndex);
            Assert.AreEqual(10u, commands[1].BaseVertexIndex);

            Object.DestroyImmediate(mesh);
        }

        [Test]
        public void BuildDrawCommands_SkipsEmptyMiddleSubmesh_WithoutShiftingLaterSlots()
        {
            var mesh = BuildMeshWithSubmeshes(
                new[] { 3u, 0u, 9u },
                new[] { 0u, 3u, 3u },
                new[] { 0, 0, 4 });

            var commands = PcgScatterGpuInstancer.BuildDrawCommands(mesh);

            Assert.AreEqual(2, commands.Length);
            Assert.AreEqual(0, commands[0].SubMeshIndex);
            Assert.AreEqual(2, commands[1].SubMeshIndex);
            Assert.AreEqual(9u, commands[1].IndexCount);
            Assert.AreEqual(3u, commands[1].StartIndex);
            Assert.AreEqual(4u, commands[1].BaseVertexIndex);

            Object.DestroyImmediate(mesh);
        }

        [Test]
        public void ResolveMaterialBindings_FewerNamesEmptyNameAndMissingBinding_UseFallback()
        {
            var fallback = new Material(Shader.Find("Hidden/InternalErrorShader")
                                        ?? Shader.Find("Standard"));
            var bound = new Material(fallback) { name = "BoundMat" };
            var bindings = new List<PcgMaterialBinding>
            {
                new PcgMaterialBinding { materialName = "wall", material = bound }
            };

            var resolved = PcgGraphComponent.ResolveMaterialBindings(
                new[] { "wall", "", "roof" },
                subMeshCount: 4,
                bindings,
                fallback);

            Assert.AreEqual(4, resolved.Length);
            Assert.AreSame(bound, resolved[0]);
            Assert.AreSame(fallback, resolved[1]);
            Assert.AreSame(fallback, resolved[2]);
            Assert.AreSame(fallback, resolved[3]);

            Object.DestroyImmediate(fallback);
            Object.DestroyImmediate(bound);
        }

        [Test]
        public void RefreshDrawMaterials_DoesNotChangeInstanceOrCommandCount()
        {
            var mesh = new Mesh { name = "RefreshTest" };
            mesh.vertices = new[]
            {
                Vector3.zero, Vector3.right, Vector3.up,
                Vector3.forward, Vector3.forward + Vector3.right, Vector3.forward + Vector3.up
            };
            mesh.subMeshCount = 2;
            mesh.SetTriangles(new[] { 0, 1, 2 }, 0);
            mesh.SetTriangles(new[] { 3, 4, 5 }, 1);
            mesh.RecalculateBounds();

            var points = new List<PcgScatterPoint>
            {
                new PcgScatterPoint { Position = Vector3.zero },
                new PcgScatterPoint { Position = Vector3.one }
            };
            Assert.IsTrue(PcgInstanceList.TryBuild(points, mesh, 1f, out var instances));

            var matA = new Material(Shader.Find("Hidden/InternalErrorShader")
                                    ?? Shader.Find("Standard"));
            var matB = new Material(matA) { name = "B" };
            var instancer = new PcgScatterGpuInstancer();
            instancer.Set(instances, new[] { matA, matB }, 0, Matrix4x4.identity);

            Assume.That(instancer.IsActive, "Instancer inactive — shader may be missing in batch mode.");

            var commandCount = instancer.CommandCount;
            var instanceCount = instancer.InstanceCount;

            instancer.RefreshDrawMaterials(new[] { matB, matA });

            Assert.AreEqual(commandCount, instancer.CommandCount);
            Assert.AreEqual(instanceCount, instancer.InstanceCount);
            Assert.IsTrue(instancer.IsActive);

            instancer.Clear();
            Object.DestroyImmediate(mesh);
            Object.DestroyImmediate(matA);
            Object.DestroyImmediate(matB);
        }

        private static Mesh BuildMeshWithSubmeshes(
            uint[] indexCounts,
            uint[] indexStarts,
            int[] baseVertices)
        {
            Assert.AreEqual(indexCounts.Length, indexStarts.Length);
            Assert.AreEqual(indexCounts.Length, baseVertices.Length);

            var mesh = new Mesh { name = "CommandBuilderTest" };
            // Provide enough verts/indices for Unity to accept submesh descriptors.
            var maxIndex = 0;
            for (var i = 0; i < indexCounts.Length; i++)
            {
                if (indexCounts[i] == 0)
                    continue;
                maxIndex = Mathf.Max(maxIndex, (int)(indexStarts[i] + indexCounts[i]));
            }

            var triCount = Mathf.Max(maxIndex, 3);
            var verts = new Vector3[Mathf.Max(triCount, 3)];
            for (var i = 0; i < verts.Length; i++)
                verts[i] = new Vector3(i, 0f, 0f);
            mesh.vertices = verts;

            var allIndices = new int[Mathf.Max(maxIndex, 3)];
            for (var i = 0; i < allIndices.Length; i++)
                allIndices[i] = i % verts.Length;
            mesh.SetTriangles(allIndices, 0, false);

            mesh.subMeshCount = indexCounts.Length;
            for (var i = 0; i < indexCounts.Length; i++)
            {
                mesh.SetSubMesh(i, new SubMeshDescriptor(
                    (int)indexStarts[i],
                    (int)indexCounts[i])
                {
                    baseVertex = baseVertices[i],
                    topology = MeshTopology.Triangles
                }, MeshUpdateFlags.DontRecalculateBounds);
            }

            return mesh;
        }
    }
}
