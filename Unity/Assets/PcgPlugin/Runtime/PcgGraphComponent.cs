using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Unified PCG component — binds a <see cref="PcgGraphAsset"/>, exposes its
    /// parameters in the Inspector, executes the graph, and renders the result.
    /// </summary>
    [ExecuteInEditMode]
    [DisallowMultipleComponent]
    public sealed class PcgGraphComponent : MonoBehaviour
    {
        [SerializeField] private PcgGraphAsset graphAsset;
        [SerializeField] private int seed = 42;
        [SerializeField] private bool runOnStart = true;

        [SerializeField]
        private List<PcgParameterOverride> m_ParameterOverrides = new();

        [SerializeField] private float gizmoSize = 0.2f;
        [SerializeField] private Color gizmoColor = new(0.2f, 0.8f, 1f);
        [SerializeField] private Color splineColor = new(1f, 0.85f, 0.2f);
        [SerializeField] private Material meshMaterial;

        private PcgGraphDocument m_Document;
        private List<PcgGraphParameter> m_GraphParameters;

        public PcgGraphAsset GraphAsset
        {
            get => graphAsset;
            set
            {
                graphAsset = value;
                RefreshDocument();
            }
        }

        public List<PcgParameterOverride> ParameterOverrides => m_ParameterOverrides;
        public List<PcgGraphParameter> GraphParameters => m_GraphParameters;
        public PcgGraphDocument Document => m_Document;
        public bool HasGraph => graphAsset != null && !string.IsNullOrEmpty(graphAsset.GraphJson);

        private void Start()
        {
            if (runOnStart && Application.isPlaying)
                Run();
        }

        public void RefreshDocument()
        {
            m_Document = null;
            m_GraphParameters = null;

            if (!HasGraph)
                return;

            if (!PcgGraphSerializer.TryFromJson(graphAsset.GraphJson, out m_Document, out var error))
            {
                Debug.LogError($"[PCG] Failed to parse graph JSON: {error}", this);
                return;
            }

            m_GraphParameters = m_Document.parameters;
            SyncOverrideList();
        }

        private void SyncOverrideList()
        {
            if (m_GraphParameters == null)
                return;

            while (m_ParameterOverrides.Count < m_GraphParameters.Count)
                m_ParameterOverrides.Add(null);

            while (m_ParameterOverrides.Count > m_GraphParameters.Count)
                m_ParameterOverrides.RemoveAt(m_ParameterOverrides.Count - 1);

            for (var i = 0; i < m_GraphParameters.Count; i++)
            {
                var param = m_GraphParameters[i];
                var existing = m_ParameterOverrides[i];

                if (existing == null || existing.parameterId != param.id)
                    m_ParameterOverrides[i] = PcgParameterOverride.FromParameter(param);
            }
        }

        private void ApplyOverridesToDocument()
        {
            if (m_Document == null || m_ParameterOverrides == null || m_GraphParameters == null)
                return;

            var byId = new Dictionary<string, PcgParameterOverride>();
            foreach (var ov in m_ParameterOverrides)
            {
                if (ov != null && !string.IsNullOrEmpty(ov.parameterId))
                    byId[ov.parameterId] = ov;
            }

            foreach (var param in m_GraphParameters)
            {
                if (!param.exposed)
                    continue;

                if (!byId.TryGetValue(param.id, out var ov))
                    continue;

                if (string.IsNullOrEmpty(param.targetNode) || string.IsNullOrEmpty(param.targetProperty))
                    continue;

                var node = FindNode(m_Document, param.targetNode);
                if (node != null)
                    node.data.SetRaw(param.targetProperty, ov.GetValue());
            }
        }

        private static PcgGraphNodeRecord FindNode(PcgGraphDocument doc, string nodeId)
        {
            if (string.IsNullOrEmpty(nodeId))
                return null;

            foreach (var node in doc.nodes)
            {
                if (node.id == nodeId)
                    return node;
            }

            return null;
        }

        public bool Run()
        {
            if (m_Document == null)
                RefreshDocument();

            if (m_Document == null)
            {
                Debug.LogError("[PCG] No graph asset assigned.", this);
                return false;
            }

            ApplyOverridesToDocument();

            var json = PcgGraphSerializer.ToJson(m_Document, pretty: false);
            var result = PcgGraphLoader.Execute(json, seed);
            if (result == null)
                return false;

            var kind = PcgResultParser.DetectKind(result);

            switch (kind)
            {
                case PcgResultKind.Mesh:
                    if (!PcgResultParser.TryParseMesh(result, out var mesh, out var meshError))
                    {
                        Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                        return false;
                    }
                    ApplyMesh(mesh);
                    break;

                case PcgResultKind.Splines:
                    if (!PcgResultParser.TryParseSplines(result, out var splines, out var splineError))
                    {
                        Debug.LogError($"[PCG] Failed to parse spline result: {splineError}");
                        return false;
                    }
                    ApplySplines(splines);
                    break;

                case PcgResultKind.Points:
                    if (!PcgResultParser.TryParsePoints(result, out var parsed, out var parseError))
                    {
                        Debug.LogError($"[PCG] Failed to parse point result: {parseError}");
                        return false;
                    }
                    ApplyPoints(PcgResultParser.ToVector3List(parsed));
                    break;

                default:
                    Debug.LogError("[PCG] Unknown result JSON shape.");
                    return false;
            }

            return true;
        }

        public void ClearResults()
        {
            m_Points.Clear();
            m_Splines.Clear();
            ApplyMesh(null);
        }

        // --- Result rendering ---

        private readonly List<Vector3> m_Points = new();
        private readonly List<List<Vector3>> m_Splines = new();

        private void ApplyPoints(List<Vector3> points)
        {
            m_Points.Clear();
            m_Splines.Clear();
            ApplyMesh(null);
            m_Points.AddRange(points);
        }

        private void ApplySplines(List<List<Vector3>> splines)
        {
            m_Points.Clear();
            m_Splines.Clear();
            ApplyMesh(null);
            if (splines != null)
                m_Splines.AddRange(splines);
        }

        private Mesh m_GeneratedMesh;
        private MeshFilter m_MeshFilter;
        private MeshRenderer m_MeshRenderer;

        private void ApplyMesh(Mesh mesh)
        {
            m_Points.Clear();
            m_Splines.Clear();
            m_GeneratedMesh = mesh;
            EnsureMeshComponents();
            m_MeshFilter.sharedMesh = mesh;
            if (m_MeshRenderer != null)
                m_MeshRenderer.enabled = mesh != null;
        }

        private void EnsureMeshComponents()
        {
            if (m_MeshFilter == null)
                m_MeshFilter = GetComponent<MeshFilter>() ?? gameObject.AddComponent<MeshFilter>();
            if (m_MeshRenderer == null)
            {
                m_MeshRenderer = GetComponent<MeshRenderer>() ?? gameObject.AddComponent<MeshRenderer>();
                if (m_MeshRenderer.sharedMaterial == null)
                {
                    var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
                    m_MeshRenderer.sharedMaterial = meshMaterial != null
                        ? meshMaterial
                        : new Material(shader) { color = new Color(0.55f, 0.75f, 0.95f) };
                }
            }
        }

        private void OnDrawGizmos()
        {
            Gizmos.color = gizmoColor;
            foreach (var p in m_Points)
                Gizmos.DrawSphere(transform.position + p, gizmoSize);

            Gizmos.color = splineColor;
            foreach (var spline in m_Splines)
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
