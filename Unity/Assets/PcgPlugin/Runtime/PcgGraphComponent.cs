using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Serialization;
#if UNITY_EDITOR
using System.Linq;
#endif

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

        [FormerlySerializedAs("executionMode")]
        [SerializeField] private PcgCookMode cookMode = PcgCookMode.OnParameterChange;

        [SerializeField, Min(0.05f)]
        private float editModeCookInterval = 0.15f;

        [SerializeField]
        private List<PcgParameterOverride> m_ParameterOverrides = new();

        [SerializeField]
        private List<PcgMeshBinding> m_MeshBindings = new();

        private float m_NextEditModeCookTime;
        private bool m_PreviewCookPending;
        private bool m_CookInProgress;
        private bool m_ForceFullQuality;

#if UNITY_EDITOR
        private static readonly HashSet<PcgGraphComponent> s_EditModePreviewCooks = new();

        /// <summary>Editor-only preview mesh bindings (Graph Editor / Inspector).</summary>
        public static System.Func<PcgGraphComponent, IReadOnlyList<PcgPreviewMeshBinding>> EditorResolvePreviewMeshBindings;
#endif

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
        public List<PcgMeshBinding> MeshBindings => m_MeshBindings;
        public PcgCookMode CookMode => cookMode;

        /// <summary>
        /// Edit Mode: <see cref="PcgCookMode.EveryFrame"/> is downgraded to
        /// <see cref="PcgCookMode.OnParameterChange"/> (V107). Play Mode keeps EveryFrame.
        /// </summary>
        public PcgCookMode EffectiveCookMode =>
            !Application.isPlaying && cookMode == PcgCookMode.EveryFrame
                ? PcgCookMode.OnParameterChange
                : cookMode;

        public PcgGraphDocument Document => m_Document;
        public bool HasGraph => graphAsset != null && !string.IsNullOrEmpty(graphAsset.GraphJson);

#if UNITY_EDITOR
        /// <summary>Set by Editor bridge to include live Graph Editor node data when Run is pressed.</summary>
        public static System.Func<PcgGraphComponent, PcgPreviewQuality, string> EditorBuildExecutionJson;

        /// <summary>Invoked after a preview cook applies results (SceneView repaint).</summary>
        public static System.Action EditorAfterPreviewCookApplied;
#endif

        private void OnEnable()
        {
#if UNITY_EDITOR
            s_EditModePreviewCooks.Add(this);
#endif
            if (!Application.isPlaying && SupportsEditModePreview())
                RequestPreviewCook(immediate: true);
        }

        private void OnDisable()
        {
#if UNITY_EDITOR
            s_EditModePreviewCooks.Remove(this);
#endif
        }

        private void Start()
        {
            if (cookMode == PcgCookMode.RunOnStart && Application.isPlaying)
                Run();
        }

        private void Update()
        {
            if (!Application.isPlaying)
                return;

            if (cookMode == PcgCookMode.EveryFrame)
                Run(skipDocumentRefresh: true);
        }

#if UNITY_EDITOR
        /// <summary>Called by <c>PcgEditModeCookScheduler</c> in the Editor assembly.</summary>
        public static void TickAllEditModePreviewCooks()
        {
            foreach (var component in s_EditModePreviewCooks.ToArray())
            {
                if (component != null)
                    component.TickEditModePreviewCook();
            }
        }

        internal void TickEditModePreviewCook()
        {
            if (Application.isPlaying || !SupportsEditModePreview())
                return;

            if (EffectiveCookMode != PcgCookMode.OnParameterChange ||
                !m_PreviewCookPending ||
                Time.realtimeSinceStartup < m_NextEditModeCookTime)
            {
                return;
            }

            if (m_Document == null)
                RefreshDocument();

            if (Run(skipDocumentRefresh: true))
            {
                m_PreviewCookPending = false;
                EditorAfterPreviewCookApplied?.Invoke();
            }
            else if (!m_CookInProgress)
            {
                m_PreviewCookPending = false;
            }
            else
            {
                m_NextEditModeCookTime = Time.realtimeSinceStartup + 0.05f;
            }
        }
#endif

        public bool SupportsEditModePreview()
        {
            return cookMode == PcgCookMode.EveryFrame ||
                   cookMode == PcgCookMode.OnParameterChange;
        }

#if UNITY_EDITOR
        private void OnValidate()
        {
            // FormerlySerializedAs OnMouseUp = 5
            if ((int)cookMode == 5)
                cookMode = PcgCookMode.OnParameterChange;
        }
#endif

        /// <summary>
        /// Schedules a debounced cook for Edit Mode preview (OnParameterChange;
        /// EveryFrame is downgraded to the same path in Edit Mode).
        /// </summary>
        public void RequestPreviewCook(bool immediate = false)
        {
            if (!SupportsEditModePreview())
                return;

            if (immediate)
            {
                m_PreviewCookPending = false;
                if (m_Document == null)
                    RefreshDocument();
                if (Run(skipDocumentRefresh: true))
                {
                    m_NextEditModeCookTime = Time.realtimeSinceStartup + editModeCookInterval;
#if UNITY_EDITOR
                    EditorAfterPreviewCookApplied?.Invoke();
#endif
                }
                return;
            }

            m_PreviewCookPending = true;
            m_NextEditModeCookTime = Time.realtimeSinceStartup + editModeCookInterval;
        }

        public void RefreshDocument()
        {
            m_Document = null;
            m_GraphParameters = null;

            if (graphAsset == null)
                return;

            var json = PcgGraphAssetUtility.ReadLatestJson(graphAsset);
            if (string.IsNullOrWhiteSpace(json))
                return;

            if (!PcgGraphSerializer.TryFromJson(json, out m_Document, out var error))
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
            ApplyOverridesToDocument(m_Document);
        }

        public void ApplyOverridesToDocument(PcgGraphDocument doc)
        {
            if (doc == null || m_ParameterOverrides == null || m_GraphParameters == null)
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

                var node = FindNode(doc, param.targetNode);
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

        public bool Run() => Run(skipDocumentRefresh: false, forceFullQuality: true);

        public bool Run(bool skipDocumentRefresh) => Run(skipDocumentRefresh, forceFullQuality: false);

        public bool Run(bool skipDocumentRefresh, bool forceFullQuality)
        {
            if (m_CookInProgress)
                return false;

            if (!skipDocumentRefresh)
                RefreshDocument();

            if (m_Document == null)
            {
                Debug.LogError("[PCG] No graph asset assigned.", this);
                return false;
            }

            m_ForceFullQuality = forceFullQuality;
            m_CookInProgress = true;
            try
            {
                return RunInternal();
            }
            finally
            {
                m_CookInProgress = false;
                m_ForceFullQuality = false;
            }
        }

        private static PcgPreviewQuality CurrentPreviewQuality(bool forceFullQuality) =>
            forceFullQuality ? PcgPreviewQuality.Full : PcgPreviewQuality.Preview;

        private bool RunInternal()
        {
            var quality = CurrentPreviewQuality(m_ForceFullQuality);
            string json = null;
#if UNITY_EDITOR
            json = EditorBuildExecutionJson?.Invoke(this, quality);
#endif
            if (string.IsNullOrEmpty(json))
            {
                if (!TryBuildExecutionJson(quality, out json))
                    return false;
            }

            var previewBindings = EditorResolvePreviewMeshBindings?.Invoke(this);
            var result = PcgGraphLoader.ExecuteWithResolvedAssets(
                json, seed, gameObject, m_MeshBindings, previewBindings, quality);
            if (result == null)
                return false;

            return ApplyExecutionResult(result);
        }

        private bool TryBuildExecutionJson(PcgPreviewQuality quality, out string json)
        {
            json = null;
            if (!PcgGraphSerializer.TryFromJson(
                    PcgGraphSerializer.ToJson(m_Document, pretty: false),
                    out var execDoc,
                    out var error))
            {
                Debug.LogError($"[PCG] Failed to clone graph document: {error}", this);
                return false;
            }

            ApplyOverridesToDocument(execDoc);
            PcgGraphPreviewOverrides.Apply(execDoc, quality);
            json = PcgGraphSerializer.ToJson(execDoc, pretty: false);
            return true;
        }

        private bool ApplyExecutionResult(PcgGraphExecuteResult result)
        {
            var kind = PcgResultParser.DetectKind(result);

            switch (kind)
            {
                case PcgResultKind.Mesh:
                    if (!PcgResultParser.TryParseMeshBinary(result.MeshBinary, out var mesh, out var meshError))
                    {
                        Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                        return false;
                    }
                    ApplyMesh(mesh);
                    break;

                case PcgResultKind.Splines:
                    if (!PcgResultParser.TryParseSplines(result.Json, out var splines, out var splineError))
                    {
                        Debug.LogError($"[PCG] Failed to parse spline result: {splineError}");
                        return false;
                    }
                    ApplySplines(splines);
                    break;

                case PcgResultKind.Points:
                    if (!PcgResultParser.TryParsePoints(result.Json, out var parsed, out var parseError))
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
            ClearGeneratedMesh();
        }

        // --- Result rendering ---

        private readonly List<Vector3> m_Points = new();
        private readonly List<List<Vector3>> m_Splines = new();

        private void ApplyPoints(List<Vector3> points)
        {
            ClearGeneratedMesh();
            m_Points.Clear();
            m_Splines.Clear();
            m_Points.AddRange(points);
        }

        private void ApplySplines(List<List<Vector3>> splines)
        {
            ClearGeneratedMesh();
            m_Points.Clear();
            m_Splines.Clear();
            if (splines != null)
                m_Splines.AddRange(splines);
        }

        private Mesh m_GeneratedMesh;
        private MeshFilter m_MeshFilter;
        private MeshRenderer m_MeshRenderer;

        private void ClearGeneratedMesh()
        {
            if (m_GeneratedMesh != null)
            {
#if UNITY_EDITOR
                DestroyImmediate(m_GeneratedMesh);
#else
                Destroy(m_GeneratedMesh);
#endif
                m_GeneratedMesh = null;
            }

            EnsureMeshComponents();
            if (m_MeshFilter != null)
                m_MeshFilter.sharedMesh = null;
            if (m_MeshRenderer != null)
                m_MeshRenderer.enabled = false;
        }

        private void ApplyMesh(Mesh mesh)
        {
            if (mesh != null)
            {
                m_Points.Clear();
                m_Splines.Clear();
            }

            if (m_GeneratedMesh != null && m_GeneratedMesh != mesh)
            {
#if UNITY_EDITOR
                DestroyImmediate(m_GeneratedMesh);
#else
                Destroy(m_GeneratedMesh);
#endif
            }

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
