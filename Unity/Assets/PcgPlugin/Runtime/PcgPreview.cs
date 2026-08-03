using System;
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
        private readonly List<IReadOnlyList<Vector3>> _splineViews = new();
        private Mesh _mesh;
        private MeshFilter _meshFilter;
        private MeshRenderer _meshRenderer;

        /// <summary>Monotonic geometry generation used by the Editor preview cache.</summary>
        public int Revision { get; private set; }

        /// <summary>Read-only point snapshot for the Editor renderer.</summary>
        public IReadOnlyList<Vector3> Points => _points;

        /// <summary>Read-only spline snapshots for the Editor renderer.</summary>
        public IReadOnlyList<IReadOnlyList<Vector3>> Splines => _splineViews;

        public float GizmoSize => gizmoSize;
        public Color GizmoColor => gizmoColor;
        public Color SplineColor => splineColor;

#if UNITY_EDITOR
        /// <summary>
        /// Editor bridge for the batched Scene View renderer. Returning true means the
        /// bridge handled the draw, including an intentional fail-closed fallback.
        /// </summary>
        public static Func<PcgPreview, Camera, bool> EditorDrawPreview;
#endif

        public void ClearAll()
        {
            _points.Clear();
            _splines.Clear();
            _splineViews.Clear();
            SetMesh(null);
        }

        /// <summary>
        /// Clears point/spline gizmos without touching the Lit MeshFilter used by cooks.
        /// </summary>
        public void ClearGizmosOnly()
        {
            _points.Clear();
            _splines.Clear();
            _splineViews.Clear();
            IncrementRevision();
        }

        public void SetPoints(IEnumerable<Vector3> points)
        {
            _points.Clear();
            _splines.Clear();
            _splineViews.Clear();
            // Do not clear shared MeshFilter — GraphComponent owns Lit mesh preview.
            // RuntimeRunner / replacement paths must ClearAll() before switching result kinds.
            if (points != null)
                _points.AddRange(points);
            IncrementRevision();
        }

        public void SetSplines(IEnumerable<List<Vector3>> splines)
        {
            _points.Clear();
            _splines.Clear();
            _splineViews.Clear();
            if (splines != null)
            {
                foreach (var spline in splines)
                {
                    var copy = spline != null ? new List<Vector3>(spline) : new List<Vector3>();
                    _splines.Add(copy);
                    _splineViews.Add(copy.AsReadOnly());
                }
            }
            IncrementRevision();
        }

        public void SetMesh(Mesh mesh)
        {
            _points.Clear();
            _splines.Clear();
            _splineViews.Clear();
            _mesh = mesh;
            EnsureMeshComponents();
            _meshFilter.sharedMesh = mesh;

            if (_meshRenderer != null)
                _meshRenderer.enabled = mesh != null;

            IncrementRevision();
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
#if UNITY_EDITOR
            if (!Application.isPlaying &&
                EditorDrawPreview != null &&
                EditorDrawPreview(this, Camera.current))
            {
                return;
            }
#endif

            Gizmos.color = gizmoColor;
            foreach (var p in _points)
                Gizmos.DrawSphere(transform.TransformPoint(p), gizmoSize);

            Gizmos.color = splineColor;
            var pointRadius = Mathf.Max(0.02f, gizmoSize * 0.35f);
            foreach (var spline in _splines)
            {
                for (var i = 0; i < spline.Count; i++)
                {
                    var world = transform.TransformPoint(spline[i]);
                    Gizmos.DrawSphere(world, pointRadius);
                    if (i > 0)
                    {
                        Gizmos.DrawLine(
                            transform.TransformPoint(spline[i - 1]),
                            world);
                    }
                }
            }
        }

        private void IncrementRevision()
        {
            unchecked
            {
                Revision++;
            }
        }
    }
}
