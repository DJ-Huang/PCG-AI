using UnityEngine;
using UnityEngine.Rendering;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Draws a scatter <see cref="PcgInstanceList"/> with
    /// <see cref="Graphics.RenderMeshIndirect"/> and a GPU transform buffer.
    /// </summary>
    internal sealed class PcgScatterGpuInstancer
    {
        private const int MatrixStrideBytes = 64;
        private static readonly int TransformBufferId = Shader.PropertyToID("_PcgTransformBuffer");

        private Mesh m_PrototypeMesh;
        private Material m_DrawMaterial;
        private GraphicsBuffer m_TransformBuffer;
        private GraphicsBuffer m_ArgsBuffer;
        private MaterialPropertyBlock m_PropertyBlock;
        private Bounds m_WorldBounds;
        private int m_InstanceCount;
        private int m_Layer;

        public bool IsActive =>
            m_InstanceCount > 0 &&
            m_PrototypeMesh != null &&
            m_DrawMaterial != null &&
            m_TransformBuffer != null &&
            m_ArgsBuffer != null;

        public void Set(PcgInstanceList instanceList, Material sourceMaterial, int layer, Matrix4x4 localToWorld)
        {
            Clear();
            if (instanceList == null || instanceList.Count == 0)
                return;

            m_PrototypeMesh = instanceList.PrototypeMesh;
            m_DrawMaterial = PcgScatterInstancingMaterial.CreateFromSource(sourceMaterial);
            if (m_PrototypeMesh == null || m_DrawMaterial == null)
            {
                Clear();
                return;
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

            m_ArgsBuffer = new GraphicsBuffer(
                GraphicsBuffer.Target.IndirectArguments,
                1,
                GraphicsBuffer.IndirectDrawIndexedArgs.size);
            m_ArgsBuffer.SetData(new[]
            {
                new GraphicsBuffer.IndirectDrawIndexedArgs
                {
                    indexCountPerInstance = m_PrototypeMesh.GetIndexCount(0),
                    instanceCount = (uint)m_InstanceCount,
                    startIndex = m_PrototypeMesh.GetIndexStart(0),
                    baseVertexIndex = m_PrototypeMesh.GetBaseVertex(0),
                    startInstance = 0
                }
            });

            m_PropertyBlock ??= new MaterialPropertyBlock();
            m_PropertyBlock.SetBuffer(TransformBufferId, m_TransformBuffer);
        }

        public void Clear()
        {
            ReleaseBuffer(ref m_TransformBuffer);
            ReleaseBuffer(ref m_ArgsBuffer);

            if (m_DrawMaterial != null)
            {
#if UNITY_EDITOR
                if (!Application.isPlaying)
                    Object.DestroyImmediate(m_DrawMaterial);
                else
#endif
                    Object.Destroy(m_DrawMaterial);
                m_DrawMaterial = null;
            }

            m_PrototypeMesh = null;
            m_InstanceCount = 0;
        }

        public void Draw(Camera camera)
        {
            if (!IsActive || camera == null)
                return;

            var renderParams = new RenderParams(m_DrawMaterial)
            {
                layer = m_Layer,
                worldBounds = m_WorldBounds,
                matProps = m_PropertyBlock,
                shadowCastingMode = ShadowCastingMode.On,
                receiveShadows = true,
                camera = camera
            };

            Graphics.RenderMeshIndirect(renderParams, m_PrototypeMesh, m_ArgsBuffer, 1, 0);
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

        private static void ReleaseBuffer(ref GraphicsBuffer buffer)
        {
            if (buffer == null)
                return;

            buffer.Release();
            buffer = null;
        }
    }
}
