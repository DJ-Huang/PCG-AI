using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace DJTechRuntime.PCG.Examples.ClassicKnife
{
    /// <summary>
    /// Slow studio rock turntable for the Classic Knife PCG assembly.
    /// Optional explode-parts using ClassicKnifeProfileAdapter offsets.
    /// </summary>
    [ExecuteInEditMode]
    public sealed class ClassicKnifePresenter : MonoBehaviour
    {
        [SerializeField] private PcgGraphComponent graph;
        [SerializeField] private bool enableStudioRock = true;
        [SerializeField] private float studioRockDegreesPerSecond = 12f;
        [SerializeField] private float explodeDistance = 1f;
        [SerializeField] private float explodeLerpSpeed = 4f;
        [SerializeField] private bool playInEditMode;

        private readonly List<Transform> m_PartRoots = new();
        private readonly List<Vector3> m_ExplodeTargets = new();
        private bool m_Exploded;
        private bool m_SplitDone;

        public bool Exploded => m_Exploded;

        private void OnEnable()
        {
            if (graph == null)
                graph = GetComponent<PcgGraphComponent>();
        }

        private void Update()
        {
            if (!Application.isPlaying && !playInEditMode)
                return;

            if (enableStudioRock && Application.isPlaying)
                transform.Rotate(0f, studioRockDegreesPerSecond * Time.deltaTime, 0f, Space.World);

            if (!m_SplitDone)
                TrySplitSubmeshes();

            if (m_PartRoots.Count == 0)
                return;

            var step = explodeLerpSpeed * Time.deltaTime;
            for (var i = 0; i < m_PartRoots.Count; i++)
            {
                var root = m_PartRoots[i];
                if (root == null)
                    continue;
                var target = m_Exploded ? m_ExplodeTargets[i] : Vector3.zero;
                root.localPosition = Vector3.Lerp(root.localPosition, target, step);
            }
        }

        public void ExplodeParts()
        {
            m_Exploded = !m_Exploded;
            TrySplitSubmeshes();
        }

        public void SetExploded(bool exploded)
        {
            m_Exploded = exploded;
            TrySplitSubmeshes();
        }

        private void TrySplitSubmeshes()
        {
            if (m_SplitDone || graph == null)
                return;

            var mf = graph.GetComponent<MeshFilter>();
            var mr = graph.GetComponent<MeshRenderer>();
            if (mf == null || mr == null || mf.sharedMesh == null)
                return;

            var mesh = mf.sharedMesh;
            if (mesh.subMeshCount <= 1)
                return;

            m_PartRoots.Clear();
            m_ExplodeTargets.Clear();

            var materials = mr.sharedMaterials;
            for (var i = 0; i < mesh.subMeshCount; i++)
            {
                var partGo = new GameObject($"KnifePart_{i}");
                partGo.transform.SetParent(transform, false);
                var partMf = partGo.AddComponent<MeshFilter>();
                var partMr = partGo.AddComponent<MeshRenderer>();

                var sub = new Mesh { name = $"ClassicKnifePart_{i}" };
                sub.vertices = mesh.vertices;
                sub.normals = mesh.normals;
                if (mesh.uv != null && mesh.uv.Length > 0)
                    sub.uv = mesh.uv;
                if (mesh.colors != null && mesh.colors.Length > 0)
                    sub.colors = mesh.colors;
                sub.SetTriangles(mesh.GetTriangles(i), 0);
                sub.RecalculateBounds();
                partMf.sharedMesh = sub;

                if (i < materials.Length)
                    partMr.sharedMaterial = materials[i];

                m_PartRoots.Add(partGo.transform);
                m_ExplodeTargets.Add(ResolveExplodeOffset(i, materials) * explodeDistance);
            }

            mr.enabled = false;
            m_SplitDone = true;
        }

        private static Vector3 ResolveExplodeOffset(int index, Material[] materials)
        {
            if (index < materials.Length && materials[index] != null)
            {
                var name = materials[index].name.ToLowerInvariant();
                if (name.Contains("fade_blade"))
                    return ClassicKnifeProfileAdapter.ExplodeOffsets["blade_body"];
                if (name.Contains("gold_ferrule"))
                    return ClassicKnifeProfileAdapter.ExplodeOffsets["ferrule"];
                if (name.Contains("grip"))
                    return ClassicKnifeProfileAdapter.ExplodeOffsets["tang_grip"];
                if (name.Contains("dark_furniture"))
                    return ClassicKnifeProfileAdapter.ExplodeOffsets["hardware"];
            }

            var offsets = ClassicKnifeProfileAdapter.ExplodeOffsets.Values.ToList();
            return offsets[index % offsets.Count];
        }
    }
}

#if UNITY_EDITOR
namespace DJTechRuntime.PCG.Examples.ClassicKnife.Editor
{
    using UnityEditor;

    [CustomEditor(typeof(ClassicKnifePresenter))]
    public sealed class ClassicKnifePresenterEditor : Editor
    {
        public override void OnInspectorGUI()
        {
            DrawDefaultInspector();
            var p = (ClassicKnifePresenter)target;
            EditorGUILayout.Space();
            if (GUILayout.Button(p.Exploded ? "Collapse parts" : "Explode parts"))
                p.ExplodeParts();
        }
    }
}
#endif
