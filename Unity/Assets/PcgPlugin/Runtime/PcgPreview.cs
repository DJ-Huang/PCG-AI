using System.Collections.Generic;
using UnityEngine;

#if UNITY_EDITOR
using UnityEditor;
#endif

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Draws PCG results in the Scene view: points, splines, or generated mesh.
    /// </summary>
    [ExecuteInEditMode]
    public class PcgPreview : MonoBehaviour
    {
        [SerializeField] private float gizmoSize = 0.2f;
        [SerializeField] private Color gizmoColor = new(0.2f, 0.8f, 1f);
        [SerializeField] private Color splineColor = new(1f, 0.85f, 0.2f);
        [SerializeField] private Material meshMaterial;

        private readonly List<Vector3> _points = new();
        private readonly List<List<Vector3>> _splines = new();
        private Mesh _mesh;
        private MeshFilter _meshFilter;
        private MeshRenderer _meshRenderer;

        public void ClearAll()
        {
            _points.Clear();
            _splines.Clear();
            SetMesh(null);
        }

        public void SetPoints(IEnumerable<Vector3> points)
        {
            _points.Clear();
            _splines.Clear();
            SetMesh(null);
            _points.AddRange(points);
        }

        public void SetSplines(IEnumerable<List<Vector3>> splines)
        {
            _points.Clear();
            _splines.Clear();
            SetMesh(null);
            if (splines != null)
                _splines.AddRange(splines);
        }

        public void SetMesh(Mesh mesh)
        {
            _points.Clear();
            _splines.Clear();
            _mesh = mesh;
            EnsureMeshComponents();
            _meshFilter.sharedMesh = mesh;

            if (_meshRenderer != null)
                _meshRenderer.enabled = mesh != null;
        }

        private void EnsureMeshComponents()
        {
            if (_meshFilter == null)
                _meshFilter = GetComponent<MeshFilter>() ?? gameObject.AddComponent<MeshFilter>();
            if (_meshRenderer == null)
            {
                _meshRenderer = GetComponent<MeshRenderer>() ?? gameObject.AddComponent<MeshRenderer>();
                if (_meshRenderer.sharedMaterial == null)
                {
                    var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
                    _meshRenderer.sharedMaterial = meshMaterial != null
                        ? meshMaterial
                        : new Material(shader) { color = new Color(0.55f, 0.75f, 0.95f) };
                }
            }
        }

        private void OnDrawGizmos()
        {
            Gizmos.color = gizmoColor;
            foreach (var p in _points)
                Gizmos.DrawSphere(transform.position + p, gizmoSize);

            Gizmos.color = splineColor;
            foreach (var spline in _splines)
            {
                for (var i = 1; i < spline.Count; i++)
                {
                    Gizmos.DrawLine(
                        transform.position + spline[i - 1],
                        transform.position + spline[i]);
                }
            }
        }
    }
}
