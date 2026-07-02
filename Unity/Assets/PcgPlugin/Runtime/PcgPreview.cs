using System.Collections.Generic;
using UnityEngine;

#if UNITY_EDITOR
using UnityEditor;
#endif

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Draws spawn points as Gizmos in the Scene view.
    /// Attach to a GameObject in the scene to visualize PCG results.
    /// </summary>
    [ExecuteInEditMode]
    public class PcgPreview : MonoBehaviour
    {
        [SerializeField] private float gizmoSize = 0.2f;
        [SerializeField] private Color gizmoColor = new(0.2f, 0.8f, 1f);

        private List<Vector3> _points = new();

        public void SetPoints(IEnumerable<Vector3> points)
        {
            _points.Clear();
            _points.AddRange(points);
        }

        private void OnDrawGizmos()
        {
            Gizmos.color = gizmoColor;
            foreach (var p in _points)
            {
                Gizmos.DrawSphere(transform.position + p, gizmoSize);
            }
        }
    }
}
