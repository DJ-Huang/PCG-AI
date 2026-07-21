using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Engine-neutral scatter instance batch (positions/rotations/scales → local TRS matrices).
    /// Built from pcg-core point binary; consumed by merged-mesh or GPU instancing backends.
    /// </summary>
    public sealed class PcgInstanceList
    {
        public Mesh PrototypeMesh { get; private set; }
        public Matrix4x4[] LocalMatrices { get; private set; }

        public int Count => LocalMatrices?.Length ?? 0;

        public static bool TryBuild(
            IReadOnlyList<PcgScatterPoint> points,
            Mesh prototypeMesh,
            float globalScale,
            out PcgInstanceList instanceList)
        {
            instanceList = null;
            if (points == null || points.Count == 0 || prototypeMesh == null)
                return false;

            var matrices = new Matrix4x4[points.Count];
            for (var i = 0; i < points.Count; i++)
                matrices[i] = BuildLocalMatrix(points[i], globalScale);

            instanceList = new PcgInstanceList
            {
                PrototypeMesh = prototypeMesh,
                LocalMatrices = matrices
            };
            return true;
        }

        public static Matrix4x4 BuildLocalMatrix(PcgScatterPoint point, float globalScale)
        {
            var rotation = Quaternion.identity;
            if (point.HasRotation)
                rotation = point.Rotation;
            else if (point.HasNormal && point.Normal.sqrMagnitude > 1e-8f)
                rotation = Quaternion.FromToRotation(Vector3.up, point.Normal.normalized);

            var perPointScale = point.HasScale ? point.Scale : Vector3.one;
            var scale = new Vector3(
                globalScale * perPointScale.x,
                globalScale * perPointScale.y,
                globalScale * perPointScale.z);
            return Matrix4x4.TRS(point.Position, rotation, scale);
        }
    }
}
