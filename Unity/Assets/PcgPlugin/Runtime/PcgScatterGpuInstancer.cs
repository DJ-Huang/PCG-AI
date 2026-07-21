using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// One indirect draw for a non-empty prototype submesh.
    /// </summary>
    internal readonly struct PcgScatterDrawCommand
    {
        public readonly int SubMeshIndex;
        public readonly uint IndexCount;
        public readonly uint StartIndex;
        public readonly uint BaseVertexIndex;

        public PcgScatterDrawCommand(
            int subMeshIndex,
            uint indexCount,
            uint startIndex,
            uint baseVertexIndex)
        {
            SubMeshIndex = subMeshIndex;
            IndexCount = indexCount;
            StartIndex = startIndex;
            BaseVertexIndex = baseVertexIndex;
        }
    }

    /// <summary>
    /// Draws a scatter <see cref="PcgInstanceList"/> with
    /// <see cref="Graphics.RenderMeshIndirect"/> and a GPU transform buffer.
    /// Each non-empty prototype submesh becomes one indirect command sharing the
    /// same instance transforms.
    /// </summary>
    internal sealed class PcgScatterGpuInstancer
    {
        private const int MatrixStrideBytes = 64;
        private static readonly int TransformBufferId = Shader.PropertyToID("_PcgTransformBuffer");

        private Mesh m_PrototypeMesh;
        private Material[] m_DrawMaterials;
        private PcgScatterDrawCommand[] m_Commands;
        private GraphicsBuffer m_TransformBuffer;
        private GraphicsBuffer m_ArgsBuffer;
        private MaterialPropertyBlock m_PropertyBlock;
        private Bounds m_WorldBounds;
        private int m_InstanceCount;
        private int m_Layer;

        public bool IsActive =>
            m_InstanceCount > 0 &&
            m_PrototypeMesh != null &&
            m_Commands != null &&
            m_Commands.Length > 0 &&
            m_DrawMaterials != null &&
            m_DrawMaterials.Length == m_Commands.Length &&
            m_TransformBuffer != null &&
            m_ArgsBuffer != null;

        public int CommandCount => m_Commands?.Length ?? 0;
        public int InstanceCount => m_InstanceCount;
        public Mesh PrototypeMesh => m_PrototypeMesh;

        public void Set(
            PcgInstanceList instanceList,
            Material[] sourceMaterials,
            int layer,
            Matrix4x4 localToWorld)
        {
            Clear();
            if (instanceList == null || instanceList.Count == 0)
                return;

            m_PrototypeMesh = instanceList.PrototypeMesh;
            if (m_PrototypeMesh == null)
            {
                Clear();
                return;
            }

            m_Commands = BuildDrawCommands(m_PrototypeMesh);
            if (m_Commands.Length == 0)
            {
                Clear();
                return;
            }

            m_DrawMaterials = new Material[m_Commands.Length];
            for (var i = 0; i < m_Commands.Length; i++)
            {
                var source = ResolveSourceMaterial(sourceMaterials, m_Commands[i].SubMeshIndex);
                m_DrawMaterials[i] = PcgScatterInstancingMaterial.CreateFromSource(source);
                if (m_DrawMaterials[i] == null)
                {
                    Clear();
                    return;
                }
            }

            m_InstanceCount = instanceList.Count;
            m_Layer = layer;

            var worldMatrices = BuildWorldMatrices(instanceList.LocalMatrices, localToWorld);
            m_WorldBounds = ComputeWorldBounds(worldMatrices, m_PrototypeMesh.bounds);

            m_TransformBuffer = new GraphicsBuffer(
                GraphicsBuffer.Target.Structured,
                m_InstanceCount,
                MatrixStrideBytes);
            m_TransformBuffer.SetData(worldMatrices);

            var args = new GraphicsBuffer.IndirectDrawIndexedArgs[m_Commands.Length];
            for (var i = 0; i < m_Commands.Length; i++)
            {
                args[i] = new GraphicsBuffer.IndirectDrawIndexedArgs
                {
                    indexCountPerInstance = m_Commands[i].IndexCount,
                    instanceCount = (uint)m_InstanceCount,
                    startIndex = m_Commands[i].StartIndex,
                    baseVertexIndex = m_Commands[i].BaseVertexIndex,
                    startInstance = 0
                };
            }

            m_ArgsBuffer = new GraphicsBuffer(
                GraphicsBuffer.Target.IndirectArguments,
                m_Commands.Length,
                GraphicsBuffer.IndirectDrawIndexedArgs.size);
            m_ArgsBuffer.SetData(args);

            m_PropertyBlock ??= new MaterialPropertyBlock();
            m_PropertyBlock.SetBuffer(TransformBufferId, m_TransformBuffer);
        }

        /// <summary>
        /// Replace cloned draw materials without rebuilding transform/args buffers.
        /// </summary>
        public void RefreshDrawMaterials(Material[] sourceMaterials)
        {
            if (!IsActive || m_Commands == null || m_DrawMaterials == null)
                return;

            for (var i = 0; i < m_Commands.Length; i++)
            {
                var source = ResolveSourceMaterial(sourceMaterials, m_Commands[i].SubMeshIndex);
                var replacement = PcgScatterInstancingMaterial.CreateFromSource(source);
                if (replacement == null)
                    continue;

                DestroyMaterial(m_DrawMaterials[i]);
                m_DrawMaterials[i] = replacement;
            }
        }

        public void Clear()
        {
            ReleaseBuffer(ref m_TransformBuffer);
            ReleaseBuffer(ref m_ArgsBuffer);

            if (m_DrawMaterials != null)
            {
                for (var i = 0; i < m_DrawMaterials.Length; i++)
                    DestroyMaterial(m_DrawMaterials[i]);
                m_DrawMaterials = null;
            }

            m_Commands = null;
            m_PrototypeMesh = null;
            m_InstanceCount = 0;
        }

        public void Draw(Camera camera)
        {
            if (!IsActive || camera == null)
                return;

            for (var i = 0; i < m_Commands.Length; i++)
            {
                var renderParams = new RenderParams(m_DrawMaterials[i])
                {
                    layer = m_Layer,
                    worldBounds = m_WorldBounds,
                    matProps = m_PropertyBlock,
                    shadowCastingMode = ShadowCastingMode.On,
                    receiveShadows = true,
                    camera = camera
                };

                Graphics.RenderMeshIndirect(renderParams, m_PrototypeMesh, m_ArgsBuffer, 1, i);
            }
        }

        /// <summary>
        /// Pure mapping: one command per non-empty submesh, preserving original indices.
        /// </summary>
        internal static PcgScatterDrawCommand[] BuildDrawCommands(Mesh mesh)
        {
            if (mesh == null || mesh.subMeshCount <= 0)
                return System.Array.Empty<PcgScatterDrawCommand>();

            var commands = new List<PcgScatterDrawCommand>(mesh.subMeshCount);
            for (var subMesh = 0; subMesh < mesh.subMeshCount; subMesh++)
            {
                var indexCount = mesh.GetIndexCount(subMesh);
                if (indexCount == 0)
                    continue;

                commands.Add(new PcgScatterDrawCommand(
                    subMesh,
                    indexCount,
                    mesh.GetIndexStart(subMesh),
                    (uint)mesh.GetBaseVertex(subMesh)));
            }

            return commands.ToArray();
        }

        private static Material ResolveSourceMaterial(Material[] sourceMaterials, int subMeshIndex)
        {
            if (sourceMaterials == null || subMeshIndex < 0 || subMeshIndex >= sourceMaterials.Length)
                return null;
            return sourceMaterials[subMeshIndex];
        }

        private static Matrix4x4[] BuildWorldMatrices(Matrix4x4[] localMatrices, Matrix4x4 localToWorld)
        {
            var worldMatrices = new Matrix4x4[localMatrices.Length];
            for (var i = 0; i < localMatrices.Length; i++)
                worldMatrices[i] = localToWorld * localMatrices[i];
            return worldMatrices;
        }

        private static Bounds ComputeWorldBounds(Matrix4x4[] worldMatrices, Bounds meshBounds)
        {
            if (worldMatrices.Length == 0)
                return new Bounds(Vector3.zero, Vector3.zero);

            var bounds = EncapsulateTransformedMeshBounds(worldMatrices[0], meshBounds);
            for (var i = 1; i < worldMatrices.Length; i++)
                bounds.Encapsulate(EncapsulateTransformedMeshBounds(worldMatrices[i], meshBounds));
            return bounds;
        }

        private static Bounds EncapsulateTransformedMeshBounds(Matrix4x4 worldMatrix, Bounds meshBounds)
        {
            var center = worldMatrix.MultiplyPoint3x4(meshBounds.center);
            var extents = meshBounds.extents;
            var axisX = worldMatrix.GetColumn(0);
            var axisY = worldMatrix.GetColumn(1);
            var axisZ = worldMatrix.GetColumn(2);
            var worldExtents = new Vector3(
                Mathf.Abs(axisX.x) * extents.x + Mathf.Abs(axisY.x) * extents.y + Mathf.Abs(axisZ.x) * extents.z,
                Mathf.Abs(axisX.y) * extents.x + Mathf.Abs(axisY.y) * extents.y + Mathf.Abs(axisZ.y) * extents.z,
                Mathf.Abs(axisX.z) * extents.x + Mathf.Abs(axisY.z) * extents.y + Mathf.Abs(axisZ.z) * extents.z);

            return new Bounds(center, worldExtents * 2f);
        }

        private static void DestroyMaterial(Material material)
        {
            if (material == null)
                return;

#if UNITY_EDITOR
            if (!Application.isPlaying)
                Object.DestroyImmediate(material);
            else
#endif
                Object.Destroy(material);
        }

        private static void ReleaseBuffer(ref GraphicsBuffer buffer)
        {
            if (buffer == null)
                return;

            buffer.Release();
            buffer = null;
        }
    }
}
