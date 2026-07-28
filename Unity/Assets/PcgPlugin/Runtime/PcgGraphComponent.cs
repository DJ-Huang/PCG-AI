using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Serialization;

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

        [SerializeField]
        private PcgHostOutputMode hostOutputMode = PcgHostOutputMode.Mesh;

        [SerializeField]
        private bool showStampOverlays = true;

        [SerializeField]
        private bool showMaskOverlay = true;

        [SerializeField]
        private string maskOverlayLayer = "mask";

        [SerializeField, Range(0f, 1f)]
        private float maskOverlayOpacity = 0.55f;

        [SerializeField]
        private PcgStampLiveCookMode stampLiveCookMode = PcgStampLiveCookMode.OnRelease;

        [SerializeField, Min(0.05f)]
        private float editModeCookInterval = 0.15f;
        [SerializeField] private bool enableAsyncCookInEditor = true;

        [SerializeField]
        private List<PcgParameterOverride> m_ParameterOverrides = new();

        [SerializeField]
        private List<PcgMeshBinding> m_MeshBindings = new();

        [SerializeField]
        private List<PcgTerrainBinding> m_TerrainBindings = new();

        [SerializeField]
        private List<PcgMaterialBinding> m_MaterialBindings = new();

        [SerializeField]
        private List<PcgSplineBinding> m_SplineBindings = new();

        private float m_NextEditModeCookTime;
        private bool m_PreviewCookPending;
        private bool m_PreviewCookPriority;
        private bool m_CookInProgress;
        private bool m_AsyncCookInProgress;
        private CancellationTokenSource m_AsyncCookCts;
        private Task<AsyncCookResult> m_AsyncCookTask;
        private int m_AsyncCookGeneration;
        private string m_LastAsyncCookStatus = "idle";
        private string m_LastCookKey;
        private bool m_HasAppliedCookResult;
        private ulong m_LastMeshBinaryHash;
        private int m_TerrainApplyGeneration;
        [SerializeField] private string[] m_LastMaterialNames = Array.Empty<string>();
        private PcgPolygonPreviewData m_PolygonPreview;

        [NonSerialized] private PcgHostTerrainSurface m_LastCookedHeightField;
        [NonSerialized] private int m_LastCookedHeightFieldGeneration;

        /// <summary>Cached Sink n-gon topology for Scene View polygon wire (null when unavailable).</summary>
        public PcgPolygonPreviewData PolygonPreview => m_PolygonPreview;

#if UNITY_EDITOR
        private static readonly HashSet<PcgGraphComponent> s_EditModePreviewCooks = new();
        private bool m_DeferredEnablePreviewCook;

        /// <summary>
        /// Editor bridge: when true, <see cref="OnEnable"/> queues preview instead of a synchronous cook
        /// (e.g. while a scene is still opening).
        /// </summary>
        public static System.Func<bool> EditorShouldDeferPreviewCookOnEnable;

        public static void FlushDeferredEnablePreviewCooks()
        {
            foreach (var component in s_EditModePreviewCooks)
            {
                if (component == null || !component.m_DeferredEnablePreviewCook)
                    continue;

                component.m_DeferredEnablePreviewCook = false;
                // Scene/domain load is not a graph mutation. Recooking every component
                // here makes an idle Editor contend with a queue of cold native graphs.
                // The first explicit Run, node Preview, parameter edit, or graph edit
                // marks the component dirty and schedules the cook on demand.
                component.m_PreviewCookPending = false;
                component.m_PreviewCookPriority = false;
            }
        }

        /// <summary>Editor-only preview mesh bindings (Graph Editor / Inspector).</summary>
        public static System.Func<PcgGraphComponent, IReadOnlyList<PcgPreviewMeshBinding>> EditorResolvePreviewMeshBindings;
        public static System.Func<PcgGraphComponent, IReadOnlyList<PcgPreviewSplineBinding>> EditorResolvePreviewSplineBindings;
#endif

        [SerializeField, Min(0.001f)] private float scatterPointScale = 0.2f;
        [SerializeField] private Mesh scatterPointMesh;
        [SerializeField] private PcgScatterDisplayMode scatterDisplayMode = PcgScatterDisplayMode.MergedMesh;
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
        public List<PcgTerrainBinding> TerrainBindings => m_TerrainBindings;
        public List<PcgMaterialBinding> MaterialBindings => m_MaterialBindings;
        public List<PcgSplineBinding> SplineBindings => m_SplineBindings;
        public PcgCookMode CookMode => cookMode;
        public PcgHostOutputMode HostOutputMode => hostOutputMode;
        public bool ShowStampOverlays => showStampOverlays;
        public bool ShowMaskOverlay => showMaskOverlay;
        public string MaskOverlayLayer =>
            string.IsNullOrEmpty(maskOverlayLayer) ? "mask" : maskOverlayLayer;
        public float MaskOverlayOpacity => Mathf.Clamp01(maskOverlayOpacity);
        public PcgStampLiveCookMode StampLiveCookMode => stampLiveCookMode;
        public PcgScatterDisplayMode ScatterDisplayMode => scatterDisplayMode;

        /// <summary>
        /// Last Terrain-host HeightField surface (includes mask and other layers).
        /// Cleared on ClearResults or when a non-HeightField result is applied in Mesh host mode.
        /// </summary>
        public PcgHostTerrainSurface LastCookedHeightField => m_LastCookedHeightField;

        /// <summary>Increments whenever <see cref="LastCookedHeightField"/> is replaced or cleared.</summary>
        public int LastCookedHeightFieldGeneration => m_LastCookedHeightFieldGeneration;

        /// <summary>
        /// Editor bridge: Graph Editor is cooking a per-node preview subgraph for this component.
        /// Mesh/Points sinks under Terrain Host must not clear or rewrite TerrainData.
        /// </summary>
        public static System.Func<PcgGraphComponent, bool> EditorIsNodePreviewActive;

        public void SetHostOutputMode(PcgHostOutputMode mode, bool requestCook = true)
        {
            var changed = hostOutputMode != mode;
            hostOutputMode = mode;
            if (mode == PcgHostOutputMode.Terrain)
                ClearGeneratedMesh();

            if (!changed)
                return;

            InvalidateCookResult();
#if UNITY_EDITOR
            UnityEditor.EditorUtility.SetDirty(this);
#endif
            if (requestCook && SupportsEditModePreview())
                RequestPreviewCook(immediate: true);
        }

        /// <summary>Material slot names from the last successful mesh cook (empty until Run).</summary>
        public IReadOnlyList<string> LastMaterialNames => m_LastMaterialNames;
        public int TerrainApplyGeneration => m_TerrainApplyGeneration;

        /// <summary>
        /// Re-apply <see cref="MaterialBindings"/> / fallback without re-cooking.
        /// GPU mode only replaces cloned draw materials (no transform/args rebuild).
        /// </summary>
        public void RefreshAppliedMaterials()
        {
            if (scatterDisplayMode == PcgScatterDisplayMode.GpuInstancing &&
                m_GpuInstancers != null &&
                m_GpuInstancers.Count > 0)
            {
                for (var i = 0; i < m_GpuInstancers.Count; i++)
                {
                    var instancer = m_GpuInstancers[i];
                    if (instancer == null || !instancer.IsActive)
                        continue;
                    var names = i < m_CachedScatterMaterialNamesList.Count
                        ? m_CachedScatterMaterialNamesList[i]
                        : m_LastMaterialNames;
                    var subMeshCount = instancer.PrototypeMesh != null
                        ? instancer.PrototypeMesh.subMeshCount
                        : 0;
                    instancer.RefreshDrawMaterials(ResolveMaterialBindings(names, subMeshCount));
                }
                return;
            }

            EnsureMeshComponents();
            if (m_MeshRenderer == null)
                return;

            ApplyMaterialBindings(m_LastMaterialNames);
        }

        public void SetScatterDisplayMode(PcgScatterDisplayMode mode, bool requestCook = true)
        {
            if (scatterDisplayMode == mode)
                return;

            scatterDisplayMode = mode;
#if UNITY_EDITOR
            UnityEditor.EditorUtility.SetDirty(this);
#endif
            if (requestCook && SupportsEditModePreview())
                RequestPreviewCook(immediate: true);
        }

        public bool IsAsyncCookInProgress => m_AsyncCookInProgress;
        public string LastAsyncCookStatus => m_LastAsyncCookStatus;

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
        public static System.Func<PcgGraphComponent, string> EditorBuildExecutionJson;

        /// <summary>Invoked after a preview cook applies results (SceneView repaint).</summary>
        public static System.Action EditorAfterPreviewCookApplied;

        /// <summary>Last cook result JSON (node_stats / node_groups / node_attrs). Read by Graph Editor info panel.</summary>
        [NonSerialized] public string LastCookResultJson;
#endif

#if UNITY_EDITOR
        private void Reset()
        {
            scatterDisplayMode = PcgProjectSettings.DefaultScatterDisplayMode;
        }
#endif

        private void OnEnable()
        {
#if UNITY_EDITOR
            s_EditModePreviewCooks.Add(this);
#endif
            TryRestoreGpuInstancingFromCache();

            if (!Application.isPlaying && SupportsEditModePreview())
            {
#if UNITY_EDITOR
                if (EditorShouldDeferPreviewCookOnEnable != null &&
                    EditorShouldDeferPreviewCookOnEnable())
                {
                    m_DeferredEnablePreviewCook = true;
                }
                else
#endif
                {
                    RequestPreviewCook(immediate: true);
                }
            }
        }

        private void OnDisable()
        {
#if UNITY_EDITOR
            s_EditModePreviewCooks.Remove(this);
#endif
            PcgScatterRenderBridge.Unregister(this);
            ClearGpuInstancers();
            CancelAsyncCook(null, log: false);
        }

        private void OnDestroy()
        {
            PcgScatterRenderBridge.Unregister(this);
            ClearGpuInstancers();
            ClearScatterCpuCache();
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

        internal void DrawScatterGpu(Camera camera)
        {
            if (scatterDisplayMode != PcgScatterDisplayMode.GpuInstancing)
                return;

            if (m_GpuInstancers == null)
                return;
            for (var i = 0; i < m_GpuInstancers.Count; i++)
                m_GpuInstancers[i]?.Draw(camera);
        }

#if UNITY_EDITOR
        /// <summary>Called by <c>PcgEditModeCookScheduler</c> in the Editor assembly.</summary>
        public static void TickAllEditModePreviewCooks()
        {
            // Finish completed workers first so their results are applied before the
            // next queued graph starts. This path runs on every Editor update, so keep
            // it allocation-free.
            PcgGraphComponent active = null;
            foreach (var component in s_EditModePreviewCooks)
            {
                if (component == null)
                    continue;

                component.PumpAsyncCookCompletion();
                if (component.m_AsyncCookInProgress)
                    active = component;
            }

            // The native cache/cancellation state is process-global and not safe for
            // concurrent cooks. Let the current worker finish before starting another.
            if (active != null)
                return;

            PcgGraphComponent next = null;
            foreach (var component in s_EditModePreviewCooks)
            {
                if (component == null || !component.CanStartEditModePreviewCook())
                    continue;

                if (next == null ||
                    (component.m_PreviewCookPriority && !next.m_PreviewCookPriority) ||
                    (component.m_PreviewCookPriority == next.m_PreviewCookPriority &&
                     component.m_NextEditModeCookTime < next.m_NextEditModeCookTime))
                {
                    next = component;
                }
            }

            next?.TickEditModePreviewCook();
        }

        public static bool HasPendingEditModePreviewCooks()
        {
            foreach (var component in s_EditModePreviewCooks)
            {
                if (component != null &&
                    (component.m_AsyncCookInProgress || component.m_PreviewCookPending))
                {
                    return true;
                }
            }

            return false;
        }

        public static bool CancelAllEditModeAsyncCooks()
        {
            var cancelledAny = false;
            foreach (var component in s_EditModePreviewCooks)
            {
                if (component != null)
                    cancelledAny |= component.CancelAsyncCook("Esc");
            }

            return cancelledAny;
        }

        internal void TickEditModePreviewCook()
        {
            if (Application.isPlaying || !SupportsEditModePreview())
                return;

            // A superseded background cook may still be unwinding inside one
            // native node. Never wait for it on the Editor thread; Pump will
            // discard it by generation and the pending cook starts afterwards.
            if (m_AsyncCookInProgress)
            {
                if (m_PreviewCookPending)
                    m_NextEditModeCookTime = Time.realtimeSinceStartup + 0.05f;
                return;
            }

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
                m_PreviewCookPriority = false;
                if (!m_AsyncCookInProgress)
                    EditorAfterPreviewCookApplied?.Invoke();
            }
            else if (!IsCookBusy())
            {
                m_PreviewCookPending = false;
                m_PreviewCookPriority = false;
            }
            else
            {
                m_NextEditModeCookTime = Time.realtimeSinceStartup + 0.05f;
            }
        }

        private bool CanStartEditModePreviewCook() =>
            !Application.isPlaying &&
            SupportsEditModePreview() &&
            !m_AsyncCookInProgress &&
            EffectiveCookMode == PcgCookMode.OnParameterChange &&
            m_PreviewCookPending &&
            Time.realtimeSinceStartup >= m_NextEditModeCookTime;
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

            if (m_AsyncCookInProgress)
            {
                RequestAsyncCookCancellation(null, log: false);
                m_PreviewCookPending = true;
                m_PreviewCookPriority |= immediate;
                m_NextEditModeCookTime = immediate
                    ? Time.realtimeSinceStartup
                    : Time.realtimeSinceStartup + editModeCookInterval;
                return;
            }

            if (immediate)
            {
#if UNITY_EDITOR
                if (ShouldUseAsyncCook())
                {
                    // Queue instead of starting directly: all Editor components share
                    // one native cache and cancellation token. The scheduler starts this
                    // priority request on the next update without racing another cook.
                    m_PreviewCookPending = true;
                    m_PreviewCookPriority = true;
                    m_NextEditModeCookTime = Time.realtimeSinceStartup;
                    return;
                }
#endif
                m_PreviewCookPending = false;
                m_PreviewCookPriority = false;
                if (m_Document == null)
                    RefreshDocument();
                // "Immediate" means start now. When async Editor cooking is enabled,
                // Mesh + PolygonPreview are still applied atomically on completion,
                // without freezing the main thread on a complex preview graph.
                if (Run(skipDocumentRefresh: true))
                {
                    m_NextEditModeCookTime = Time.realtimeSinceStartup + editModeCookInterval;
#if UNITY_EDITOR
                    if (!m_AsyncCookInProgress)
                        EditorAfterPreviewCookApplied?.Invoke();
#endif
                }
                return;
            }

            m_PreviewCookPending = true;
            m_PreviewCookPriority = false;
            m_NextEditModeCookTime = Time.realtimeSinceStartup + editModeCookInterval;
        }

        public void RefreshDocument()
        {
            m_Document = null;
            m_GraphParameters = null;
            InvalidateCookResult();

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

                var node = PcgParameterTargetResolver.FindNode(doc, param.targetNode);
                if (node != null)
                    node.data.SetRaw(param.targetProperty, ov.GetValue());
            }
        }

        public bool Run() => Run(skipDocumentRefresh: false);

        public bool Run(bool skipDocumentRefresh) =>
            Run(skipDocumentRefresh, forceSynchronous: false);

        public bool Run(bool skipDocumentRefresh, bool forceSynchronous)
        {
            if (m_CookInProgress)
                return false;

            if (m_AsyncCookInProgress)
            {
                if (ShouldUseAsyncCook() || forceSynchronous)
                    CancelAsyncCook(null, log: false);
                else
                    return false;
            }

            if (!skipDocumentRefresh)
                RefreshDocument();

            if (m_Document == null)
            {
                Debug.LogError("[PCG] No graph asset assigned.", this);
                return false;
            }

            m_CookInProgress = true;
            try
            {
                return RunInternal(forceSynchronous);
            }
            finally
            {
                m_CookInProgress = false;
            }
        }

        private bool RunInternal(bool forceSynchronous = false)
        {
            string json = null;
#if UNITY_EDITOR
            json = EditorBuildExecutionJson?.Invoke(this);
#endif
            if (string.IsNullOrEmpty(json))
            {
                if (!TryBuildExecutionJson(out json))
                    return false;
            }

            if (!PcgGraphExecutionPolicy.TryPrepareJson(json, out json, out var prepareError))
            {
                Debug.LogError($"[PCG] Failed to prepare graph for execution: {prepareError}", this);
                return false;
            }

            var textures = PcgTextureResolver.CollectFromGraphJson(json);
            if (!PcgTextureGraphUtil.TryValidateTextureRequirements(json, textures, out var textureError))
            {
                Debug.LogError($"[PCG] {textureError}");
                return false;
            }

            var previewBindings = EditorResolvePreviewMeshBindings?.Invoke(this);
            var previewSplineBindings = EditorResolvePreviewSplineBindings?.Invoke(this);
            var meshes = PcgMeshResolver.CollectFromGraphJson(
                json, gameObject, m_MeshBindings, previewBindings);
            if (!PcgMeshGraphUtil.TryValidateMeshRequirements(json, meshes, out _))
                return false;

            var splines = PcgSplineResolver.CollectFromGraphJson(
                json, gameObject, m_SplineBindings, previewSplineBindings);
            var heightfields = PcgTerrainResolver.CollectFromGraphJson(
                json, gameObject, m_TerrainBindings);
            var terrainFingerprint = PcgTerrainResolver.ComputeFingerprint(heightfields);
            var cookKey = PcgGraphCookCache.BuildKey(json, seed, terrainFingerprint);
            if (TryReuseCachedCook(cookKey))
                return true;

            if (!forceSynchronous && ShouldUseAsyncCook())
            {
                StartAsyncCook(json, textures, meshes, splines, heightfields);
                return true;
            }

            var result = PcgGraphLoader.Execute(
                json, seed, textures, meshes, splines, heightfields);
            if (result == null)
                return false;

            PcgGraphCookCache.Store(cookKey, result);
            return CommitCookResult(cookKey, result);
        }

        private bool TryReuseCachedCook(string cookKey)
        {
            if (Application.isPlaying &&
                cookMode == PcgCookMode.EveryFrame &&
                cookKey == m_LastCookKey &&
                m_HasAppliedCookResult)
            {
                Debug.Log("[PCG] Cook perf: skipped execute (EveryFrame, same key).");
                return true;
            }

            if (!PcgGraphCookCache.TryGet(cookKey, out var cached) || !CommitCookResult(cookKey, cached))
                return false;

            PcgCookPerfLog.LogCacheHit(cached.Perf);
            return true;
        }

        private bool CommitCookResult(string cookKey, PcgGraphExecuteResult result)
        {
            if (!ApplyExecutionResult(result))
                return false;

            m_LastCookKey = cookKey;
            m_HasAppliedCookResult = true;
            // Sync / cache-hit cooks also clear a sticky prior async failure so the
            // Inspector status matches the mesh that was just applied.
            m_LastAsyncCookStatus = "completed";
#if UNITY_EDITOR
            MarkCookResultPersistable(m_GeneratedMesh);
#endif
            return true;
        }

        private void InvalidateCookResult()
        {
            m_LastCookKey = null;
            m_HasAppliedCookResult = false;
            m_LastMeshBinaryHash = 0;
            m_LastMaterialNames = Array.Empty<string>();
        }

        private bool TryBuildExecutionJson(out string json)
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

#if UNITY_EDITOR
            var loader = PcgExecutionDocumentBuilder.CreateEditorAssetDatabaseLoader();
#else
            PcgExternalSubgraphLoader loader = null;
#endif
            if (!PcgExecutionDocumentBuilder.TryBuildJson(
                    execDoc,
                    loader,
                    out json,
                    out _,
                    out var buildError,
                    pretty: false))
            {
                Debug.LogError($"[PCG] Failed to build execution document: {buildError}", this);
                return false;
            }

            return true;
        }

        private bool ApplyExecutionResult(PcgGraphExecuteResult result)
        {
            m_PolygonPreview = null;
            var kind = PcgResultParser.DetectKind(result);

#if UNITY_EDITOR
            LastCookResultJson = result.Json;
#endif

            if (hostOutputMode == PcgHostOutputMode.Terrain)
            {
                var isHeightField = kind == PcgResultKind.HeightField ||
                    (result.HeightFieldBinary != null && result.HeightFieldBinary.Length > 0);
                if (isHeightField)
                    return ApplyTerrainHostResult(result, kind);

#if UNITY_EDITOR
                // Node Preview of Mesh/Points under Terrain Host: keep TerrainData,
                // treat result as overlay/debug only (stamp volume preview path).
                // Keep LastCookedHeightField so mask overlay can still draw.
                if (EditorIsNodePreviewActive?.Invoke(this) == true &&
                    (kind == PcgResultKind.Mesh || kind == PcgResultKind.Points || kind == PcgResultKind.Splines))
                {
                    ApplyTerrainHostOverlayPreview(result, kind);
                    return true;
                }
#endif
                Debug.LogError(
                    "[PCG] Host Output Mode = Terrain expects a HeightField Output " +
                    "(wire HeightField → Output; do not Convert → Mesh). " +
                    "Mesh node Preview under Terrain Host is overlay-only — use Scene stamp gizmo " +
                    "or clear Node Preview to cook the full terrain graph.",
                    this);
                // Do not keep a stale Node Preview HeightField (mask tint would stick).
                ClearLastCookedHeightField();
                return false;
            }

            ClearLastCookedHeightField();

            switch (kind)
            {
                case PcgResultKind.Mesh:
                {
                    // Feed group stats JSON to visualizer
                    if (!string.IsNullOrEmpty(result.Json))
                    {
                        var gv = GetComponent<PcgGroupVisualizer>();
                        if (gv == null)
                            gv = gameObject.AddComponent<PcgGroupVisualizer>();
                        gv.SetResultJson(result.Json);
                    }

                    var binHash = ComputeBinaryHash(result.MeshBinary);
                    if (binHash != 0 && binHash == m_LastMeshBinaryHash && m_GeneratedMesh != null)
                    {
                        ApplyMesh(m_GeneratedMesh, m_LastMaterialNames);
                    }
                    else
                    {
                        if (!PcgResultParser.TryParseMeshBinary(
                                result.MeshBinary, out var mesh, out var materialNames, out var meshError))
                        {
                            Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                            return false;
                        }
                        ApplyMesh(mesh, materialNames);
                        m_LastMaterialNames = materialNames;
                        m_LastMeshBinaryHash = binHash;
                    }

                    if (result.GeometryBinary != null && result.GeometryBinary.Length > 0)
                    {
                        if (PcgResultParser.TryParseGeometryBinary(
                                result.GeometryBinary, out var polygon, out var geometryError))
                        {
                            m_PolygonPreview = polygon;
                            ApplyPointLineGizmoPreview(polygon);
                        }
                        else
                        {
                            Debug.LogWarning($"[PCG] Failed to parse geometry binary for polygon wire: {geometryError}");
                            m_PolygonPreview = null;
                            ClearPointLineGizmoPreview();
                        }
                    }
#if UNITY_EDITOR
                    else
                    {
                        m_PolygonPreview = null;
                        ClearPointLineGizmoPreview();
                        var exportHint = "";
                        if (!string.IsNullOrEmpty(result.Json) &&
                            result.Json.Contains("\"geometry_export\""))
                        {
                            exportHint = " json.geometry_export present.";
                        }

                        Debug.LogWarning(
                            "[PCG] Mesh cook has no geometry_binary — Scene polygon wire empty. " +
                            $"verts={result.VertexCount} idx={result.IndexCount}.{exportHint} " +
                            "Normal for SubdivideMesh / other mesh-only nodes with no upstream Geometry. " +
                            "CreateBoxMesh / Sweep / Bevel / GroupCreate should export geometry — if not, bug. " +
                            "Check '[PCG] Run uses node preview' for the previewed node.");
                    }
#endif
                    break;
                }

                case PcgResultKind.Splines:
                    if (!PcgResultParser.TryParseSplines(result.Json, out var splines, out var splineError))
                    {
                        Debug.LogError($"[PCG] Failed to parse spline result: {splineError}");
                        return false;
                    }
                    ApplySplines(splines);
                    break;

                case PcgResultKind.Points:
                    if (result.Kind == PcgExecuteKind.Points)
                    {
                        if (!PcgResultParser.TryParsePointBinary(result.PointBinary, out var points, out var binaryError))
                        {
                            Debug.LogError($"[PCG] Failed to parse point binary result: {binaryError}");
                            return false;
                        }
                        var spawnHash = ComputeBinaryHash(result.MeshBinary);
                        if (spawnHash != 0 &&
                            spawnHash == m_LastMeshBinaryHash &&
                            m_OwnedSpawnPrototypeMeshes.Count > 0 &&
                            m_CachedScatterInstancesList.Count == m_OwnedSpawnPrototypeMeshes.Count)
                        {
                            ApplyCachedSpawnPrototypes(points);
                        }
                        else if (!TryApplySpawnPrototypes(result.MeshBinary, points, spawnHash))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (!PcgResultParser.TryParsePoints(result.Json, out var parsed, out var parseError))
                        {
                            Debug.LogError($"[PCG] Failed to parse point result: {parseError}");
                            return false;
                        }
                        if (!TryApplySpawnPrototypes(
                                result.MeshBinary,
                                PcgResultParser.ToScatterPoints(PcgResultParser.ToVector3List(parsed)),
                                ComputeBinaryHash(result.MeshBinary)))
                        {
                            return false;
                        }
                    }
                    break;

                case PcgResultKind.HeightField:
                    Debug.LogError(
                        "[PCG] HeightField Output requires Host Output = Terrain " +
                        "with PcgGraphComponent on the Terrain GameObject.",
                        this);
                    return false;

                default:
                    Debug.LogError("[PCG] Unknown result JSON shape.");
                    return false;
            }

            return true;
        }

#if UNITY_EDITOR
        /// <summary>
        /// Terrain Host + Node Preview of Mesh/Points: keep Terrain, optional polygon wire only.
        /// Stamp volume interaction uses <c>PcgStampOverlaySceneHandles</c>, not Host Output.
        /// </summary>
        private void ApplyTerrainHostOverlayPreview(PcgGraphExecuteResult result, PcgResultKind kind)
        {
            if (kind != PcgResultKind.Mesh)
                return;

            if (result.GeometryBinary != null &&
                result.GeometryBinary.Length > 0 &&
                PcgResultParser.TryParseGeometryBinary(
                    result.GeometryBinary, out var polygon, out _))
            {
                m_PolygonPreview = polygon;
                ApplyPointLineGizmoPreview(polygon);
            }
            else
            {
                ClearPointLineGizmoPreview();
            }
        }
#endif

        private void ApplyPointLineGizmoPreview(PcgPolygonPreviewData preview)
        {
            if (preview == null || !preview.IsPointOrCurveLike())
            {
                ClearPointLineGizmoPreview();
                return;
            }

            var gizmos = GetComponent<PcgPreview>() ?? gameObject.AddComponent<PcgPreview>();
            if (preview.FaceCount <= 0)
            {
                gizmos.SetPoints(preview.Points);
                return;
            }

            var polylines = preview.ExtractPolylines();
            if (polylines.Count > 0)
                gizmos.SetSplines(polylines);
            else
                gizmos.SetPoints(preview.Points);
        }

        private void ClearPointLineGizmoPreview()
        {
            var gizmos = GetComponent<PcgPreview>();
            gizmos?.ClearGizmosOnly();
        }

        private bool ApplyTerrainHostResult(PcgGraphExecuteResult result, PcgResultKind kind)
        {
            ClearGeneratedMesh();
            m_LastMeshBinaryHash = 0;
            m_LastMaterialNames = Array.Empty<string>();

            if (PcgTerrainBindingTable.ResolveSelfTerrain(gameObject) == null)
            {
                Debug.LogError(
                    "[PCG] Host Output Mode = Terrain requires PcgGraphComponent on a GameObject with Terrain.",
                    this);
                return false;
            }

            if (kind != PcgResultKind.HeightField &&
                (result.HeightFieldBinary == null || result.HeightFieldBinary.Length == 0))
            {
                Debug.LogError(
                    "[PCG] Host Output Mode = Terrain expects a HeightField Output " +
                    "(wire HeightField → Output; do not Convert → Mesh).",
                    this);
                return false;
            }

            if (!PcgHeightFieldBinaryParser.TryParse(
                    result.HeightFieldBinary, out var terrainSurface, out var terrainParseError))
            {
                Debug.LogError(
                    $"[PCG] Terrain host mode needs a typed HeightField result: {terrainParseError}",
                    this);
                return false;
            }

            SetLastCookedHeightField(terrainSurface);
            return ApplyTerrainSurface(terrainSurface);
        }

        private void SetLastCookedHeightField(PcgHostTerrainSurface surface)
        {
            m_LastCookedHeightField = surface;
            m_LastCookedHeightFieldGeneration++;
#if UNITY_EDITOR
            UnityEditor.SceneView.RepaintAll();
#endif
        }

        private void ClearLastCookedHeightField()
        {
            if (m_LastCookedHeightField == null && m_LastCookedHeightFieldGeneration == 0)
                return;

            m_LastCookedHeightField = null;
            m_LastCookedHeightFieldGeneration++;
#if UNITY_EDITOR
            UnityEditor.SceneView.RepaintAll();
#endif
        }

        private bool ApplyTerrainSurface(PcgHostTerrainSurface surface)
        {
            var terrain = PcgTerrainBindingTable.ResolveSelfTerrain(gameObject);
            if (terrain == null)
            {
                Debug.LogError(
                    "[PCG] No Terrain on this GameObject. Attach PcgGraphComponent to the Terrain.",
                    this);
                return false;
            }

            var adapter = new PcgUnityTerrainAdapter(terrain, transform);
            if (!adapter.TryExportSurface(surface, out var report, out var error))
            {
                Debug.LogError($"[PCG] Terrain output failed: {error}", this);
                return false;
            }

            if (report.Applied)
            {
                m_TerrainApplyGeneration++;
                // Without Flush, Scene View often keeps the previous heightmesh until
                // the camera moves (LOD / render cache). Always flush after SetHeights.
                terrain.Flush();
#if UNITY_EDITOR
                if (terrain.terrainData != null)
                    UnityEditor.EditorUtility.SetDirty(terrain.terrainData);
                var scene = gameObject.scene;
                if (scene.IsValid())
                    UnityEditor.SceneManagement.EditorSceneManager.MarkSceneDirty(scene);
                UnityEditor.EditorApplication.QueuePlayerLoopUpdate();
#endif
            }

            if (report.Resampled || report.ScaledFootprint || report.ClampedSamples > 0)
            {
                Debug.Log(
                    $"[PCG] Terrain applied " +
                    $"({report.SourceResolutionX}x{report.SourceResolutionZ} -> " +
                    $"{report.TargetResolution}x{report.TargetResolution}, " +
                    $"resampled={report.Resampled}, scaledFootprint={report.ScaledFootprint}, " +
                    $"clamped={report.ClampedSamples}).",
                    this);
            }

            return true;
        }

        private bool IsCookBusy() => m_CookInProgress || m_AsyncCookInProgress;

        private bool ShouldUseAsyncCook()
        {
#if UNITY_EDITOR
            return enableAsyncCookInEditor && !Application.isPlaying;
#else
            return false;
#endif
        }

        private void StartAsyncCook(
            string json,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines,
            IReadOnlyList<PcgHeightFieldUpload> heightfields)
        {
            CancelAsyncCook(null, log: false);
            m_AsyncCookInProgress = true;
            m_LastAsyncCookStatus = "running";
            m_AsyncCookGeneration++;
            var generation = m_AsyncCookGeneration;
            var localSeed = seed;
            m_AsyncCookCts = new CancellationTokenSource();
            var token = m_AsyncCookCts.Token;
            m_AsyncCookTask = Task.Run(() =>
            {
                if (token.IsCancellationRequested)
                    return AsyncCookResult.FromCancelled(generation);

                // ExecuteGraph performs parse + validation. Avoid a second native/HTTP
                // round-trip and a second full JSON parse on every asynchronous preview cook.
                var (execCode, execResult) = PcgCookBackend.ExecuteGraph(
                    json, localSeed, textures, meshes, splines, heightfields);
                if (execCode != PcgResultCode.Ok)
                {
                    return AsyncCookResult.Failed(
                        generation,
                        $"Execution failed ({execCode}): {execResult?.Error}");
                }

                return AsyncCookResult.Succeeded(generation, execResult);
            }, token);
        }

        /// <summary>
        /// Marks an in-flight preview cook as obsolete and asks native execution to
        /// cancel. The worker is deliberately not joined here: preview switching must
        /// stay responsive even while a long-running native node is unwinding.
        /// </summary>
        public void CancelAsyncCookForPreviewSwitch()
        {
            RequestAsyncCookCancellation(null, log: false);
        }

        /// <summary>
        /// Drop the retained HeightField used by Scene mask tint when leaving Node Preview
        /// so the previous preview mask disappears immediately (full-graph recook restores it).
        /// </summary>
        public void ClearHeightFieldOverlayForPreviewSwitch()
        {
            ClearLastCookedHeightField();
        }

        private bool RequestAsyncCookCancellation(string reason, bool log = true)
        {
            if (!m_AsyncCookInProgress || m_AsyncCookTask == null)
                return false;

            if (m_LastAsyncCookStatus == "cancelling")
                return true;

            PcgCookBackend.RequestCancel();
            m_AsyncCookCts?.Cancel();
            // A completed result from the superseded request must never apply.
            m_AsyncCookGeneration++;
            m_LastAsyncCookStatus = "cancelling";

#if UNITY_EDITOR
            if (log && !string.IsNullOrEmpty(reason))
                Debug.Log($"[PCG] Async cook cancellation requested ({reason}).");
#endif
            return true;
        }

        private bool CancelAsyncCook(string reason, bool log = true)
        {
            var task = m_AsyncCookTask;
            if (!m_AsyncCookInProgress && task == null)
                return false;

            // Ask the in-flight Task.Run cook to stop, then WAIT for ExecuteGraph
            // to finish. Dropping the Task reference without Wait races the next cook against
            // g_cook_cache / static cancel flag — Mesh may still apply, GeometryBinary often becomes 0
            // (cyan polygon wire empty on node Preview).
            PcgCookBackend.RequestCancel();
            m_AsyncCookCts?.Cancel();

            m_AsyncCookInProgress = false;
            m_AsyncCookTask = null;
            // Invalidate generation so a late Pump cannot apply a raced result.
            m_AsyncCookGeneration++;
            var cts = m_AsyncCookCts;
            m_AsyncCookCts = null;
            m_LastAsyncCookStatus = "cancelled";

            if (task != null)
            {
                try
                {
                    if (!task.Wait(TimeSpan.FromSeconds(30)))
                    {
#if UNITY_EDITOR
                        Debug.LogWarning("[PCG] Timed out waiting for cancelled async cook to finish.");
#endif
                    }
                }
                catch (AggregateException)
                {
                    // Expected when RequestCancel / CTS cancels the worker.
                }
                catch (Exception ex)
                {
#if UNITY_EDITOR
                    Debug.LogWarning($"[PCG] Wait for cancelled cook failed: {ex.Message}");
#endif
                }
            }

            cts?.Dispose();
            PcgCookBackend.ClearCancel();

#if UNITY_EDITOR
            if (log && !string.IsNullOrEmpty(reason))
                Debug.Log($"[PCG] Async cook cancelled ({reason}).");
#endif
            return true;
        }

        private void PumpAsyncCookCompletion()
        {
            if (!m_AsyncCookInProgress || m_AsyncCookTask == null || !m_AsyncCookTask.IsCompleted)
                return;

            var completedTask = m_AsyncCookTask;
            m_AsyncCookTask = null;
            m_AsyncCookInProgress = false;
            m_AsyncCookCts?.Dispose();
            m_AsyncCookCts = null;

            if (completedTask.IsCanceled)
                return;

            if (completedTask.IsFaulted)
            {
                m_LastAsyncCookStatus = "failed";
                Debug.LogError($"[PCG] Async cook task failed: {completedTask.Exception?.GetBaseException().Message}", this);
                return;
            }

            var asyncResult = completedTask.Result;
            if (asyncResult == null || asyncResult.IsCancelled || asyncResult.Generation != m_AsyncCookGeneration)
            {
                m_LastAsyncCookStatus = "cancelled";
                return;
            }

            if (!string.IsNullOrEmpty(asyncResult.Error))
            {
                m_LastAsyncCookStatus = asyncResult.Error.Contains("Execution cancelled")
                    ? "cancelled"
                    : "failed";
                Debug.LogError($"[PCG] {asyncResult.Error}", this);
                return;
            }

            var result = asyncResult.Result;
            if (result == null)
                return;

            if (result.CookNodesSkipped > 0)
            {
                Debug.Log(
                    $"[PCG] Cook cache: skipped {result.CookNodesSkipped} node(s), executed {result.CookNodesExecuted}.");
            }
            else if (PcgProjectSettings.IsLogEnabled)
            {
                Debug.Log(
                    $"[PCG] Cook cache: skipped 0 node(s), executed {result.CookNodesExecuted} (cold).");
            }

            if (ApplyExecutionResult(result))
            {
                m_LastAsyncCookStatus = "completed";
#if UNITY_EDITOR
                MarkCookResultPersistable(m_GeneratedMesh);
                EditorAfterPreviewCookApplied?.Invoke();
#endif
            }
        }

        private sealed class AsyncCookResult
        {
            public int Generation;
            public PcgGraphExecuteResult Result;
            public string Error;
            public bool IsCancelled;

            public static AsyncCookResult Succeeded(int generation, PcgGraphExecuteResult result)
            {
                return new AsyncCookResult
                {
                    Generation = generation,
                    Result = result
                };
            }

            public static AsyncCookResult Failed(int generation, string error)
            {
                return new AsyncCookResult
                {
                    Generation = generation,
                    Error = error
                };
            }

            public static AsyncCookResult FromCancelled(int generation)
            {
                return new AsyncCookResult
                {
                    Generation = generation,
                    IsCancelled = true
                };
            }
        }

        public void ClearResults()
        {
            InvalidateCookResult();
            ClearGeneratedMesh();
            ClearScatterCpuCache();
            ClearLastCookedHeightField();
            m_PolygonPreview = null;
        }

        // --- Result rendering ---

        private void ApplyPoints(
            List<PcgScatterPoint> points,
            Mesh pointPrototypeMesh = null,
            IReadOnlyList<string> materialNames = null)
        {
            var names = Array.Empty<string>();
            if (materialNames != null)
            {
                names = new string[materialNames.Count];
                for (var i = 0; i < materialNames.Count; i++)
                    names[i] = materialNames[i];
            }

            ApplyPrototypeBatches(
                new List<PcgResultParser.PcgSpawnPrototype>
                {
                    new PcgResultParser.PcgSpawnPrototype
                    {
                        Mesh = pointPrototypeMesh != null ? pointPrototypeMesh : ResolveScatterPointMesh(),
                        MaterialNames = names,
                        PointCount = points?.Count ?? 0
                    }
                },
                points);
        }

        private bool TryApplySpawnPrototypes(
            byte[] meshBinary,
            List<PcgScatterPoint> points,
            ulong spawnHash)
        {
            if (meshBinary == null || meshBinary.Length == 0)
            {
                ApplyPoints(points, null, null);
                return true;
            }

            if (!PcgResultParser.TryParseSpawnMeshBinary(meshBinary, out var prototypes, out var error))
            {
                Debug.LogError($"[PCG] Failed to parse spawn mesh binary: {error}");
                return false;
            }

            m_LastMeshBinaryHash = spawnHash;
            if (prototypes.Count == 1 && prototypes[0].PointCount < 0)
                prototypes[0].PointCount = points?.Count ?? 0;

            var allNames = new List<string>();
            foreach (var proto in prototypes)
            {
                if (proto.MaterialNames == null)
                    continue;
                foreach (var name in proto.MaterialNames)
                {
                    if (!string.IsNullOrEmpty(name) && !allNames.Contains(name))
                        allNames.Add(name);
                }
            }
            m_LastMaterialNames = allNames.ToArray();
            ApplyPrototypeBatches(prototypes, points);
            return true;
        }

        private void ApplyCachedSpawnPrototypes(List<PcgScatterPoint> points)
        {
            var prototypes = new List<PcgResultParser.PcgSpawnPrototype>(m_OwnedSpawnPrototypeMeshes.Count);
            for (var i = 0; i < m_OwnedSpawnPrototypeMeshes.Count; i++)
            {
                prototypes.Add(new PcgResultParser.PcgSpawnPrototype
                {
                    Mesh = m_OwnedSpawnPrototypeMeshes[i],
                    MaterialNames = i < m_CachedScatterMaterialNamesList.Count
                        ? m_CachedScatterMaterialNamesList[i]
                        : Array.Empty<string>(),
                    PointCount = i < m_CachedScatterPointCounts.Count
                        ? m_CachedScatterPointCounts[i]
                        : 0
                });
            }
            ApplyPrototypeBatches(prototypes, points, reuseOwnedMeshes: true);
        }

        private void ApplyPrototypeBatches(
            List<PcgResultParser.PcgSpawnPrototype> prototypes,
            List<PcgScatterPoint> points,
            bool reuseOwnedMeshes = false)
        {
            if (prototypes == null || prototypes.Count == 0 || points == null || points.Count == 0)
            {
                ClearScatterDisplay();
                ClearScatterCpuCache();
                return;
            }

            if (scatterDisplayMode != PcgScatterDisplayMode.GpuInstancing)
            {
                ClearGpuInstancingOnly();
                ClearScatterCpuCache();

                var combines = new List<CombineInstance>();
                var totalVertsEstimate = 0;
                var mergedPointOffset = 0;
                try
                {
                    for (var i = 0; i < prototypes.Count; i++)
                    {
                        var proto = prototypes[i];
                        var count = proto.PointCount;
                        if (count < 0)
                            count = points.Count - mergedPointOffset;
                        if (count <= 0 || mergedPointOffset >= points.Count)
                            continue;
                        if (mergedPointOffset + count > points.Count)
                            count = points.Count - mergedPointOffset;

                        var slice = points.GetRange(mergedPointOffset, count);
                        mergedPointOffset += count;
                        if (proto.Mesh == null)
                            continue;
                        if (!PcgInstanceList.TryBuild(slice, proto.Mesh, scatterPointScale, out var instanceList))
                            continue;

                        totalVertsEstimate += instanceList.Count * proto.Mesh.vertexCount;
                        for (var j = 0; j < instanceList.Count; j++)
                        {
                            combines.Add(new CombineInstance
                            {
                                mesh = proto.Mesh,
                                transform = instanceList.LocalMatrices[j]
                            });
                        }
                    }

                    if (combines.Count == 0)
                    {
                        ClearScatterDisplay();
                        return;
                    }

                    var mesh = new Mesh { name = "PCG Scatter Points Mesh" };
                    if (totalVertsEstimate > 65535)
                        mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
                    mesh.CombineMeshes(combines.ToArray(), true, true, false);
                    mesh.RecalculateBounds();
                    mesh.RecalculateNormals();
                    mesh.RecalculateTangents();
                    ApplyMesh(mesh);
                }
                finally
                {
                    if (!reuseOwnedMeshes)
                        DestroyTemporarySpawnPrototypes(prototypes);
                }
                return;
            }

            ClearMergedMeshOnly();
            ClearGpuInstancers();
            if (!reuseOwnedMeshes)
                SetOwnedSpawnPrototypes(prototypes);

            m_CachedScatterInstancesList.Clear();
            m_CachedScatterMaterialNamesList.Clear();
            m_CachedScatterPointCounts.Clear();
            m_GpuInstancers.Clear();

            var offset = 0;
            var anyActive = false;
            for (var i = 0; i < prototypes.Count; i++)
            {
                var proto = prototypes[i];
                var count = proto.PointCount;
                if (count < 0)
                    count = points.Count - offset;
                if (count <= 0 || offset >= points.Count)
                    continue;
                if (offset + count > points.Count)
                    count = points.Count - offset;

                var slice = points.GetRange(offset, count);
                offset += count;
                if (proto.Mesh == null)
                    continue;
                if (!PcgInstanceList.TryBuild(slice, proto.Mesh, scatterPointScale, out var instanceList))
                    continue;

                var names = proto.MaterialNames ?? Array.Empty<string>();
                var materials = ResolveMaterialBindings(names, proto.Mesh.subMeshCount);
                var instancer = new PcgScatterGpuInstancer();
                instancer.Set(instanceList, materials, gameObject.layer, transform.localToWorldMatrix);
                m_GpuInstancers.Add(instancer);
                m_CachedScatterInstancesList.Add(instanceList);
                m_CachedScatterMaterialNamesList.Add(names);
                m_CachedScatterPointCounts.Add(count);
                if (instancer.IsActive)
                    anyActive = true;
            }

            if (anyActive)
                PcgScatterRenderBridge.Register(this);
            else
                PcgScatterRenderBridge.Unregister(this);
            if (m_MeshRenderer != null)
                m_MeshRenderer.enabled = false;
        }

        private void ApplySplines(List<List<Vector3>> splines)
        {
            ClearGeneratedMesh();
            var preview = GetComponent<PcgPreview>() ?? gameObject.AddComponent<PcgPreview>();
            preview.SetSplines(splines);
            if (splines != null && splines.Count > 0 && PcgProjectSettings.IsLogEnabled)
                Debug.Log($"[PCG] Spline preview updated ({splines.Count} spline(s)).");
        }

        private Mesh m_GeneratedMesh;
        private readonly List<Mesh> m_OwnedSpawnPrototypeMeshes = new();
        private MeshFilter m_MeshFilter;
        private MeshRenderer m_MeshRenderer;
        private readonly List<PcgScatterGpuInstancer> m_GpuInstancers = new();
        private readonly List<PcgInstanceList> m_CachedScatterInstancesList = new();
        private readonly List<string[]> m_CachedScatterMaterialNamesList = new();
        private readonly List<int> m_CachedScatterPointCounts = new();

        private void ClearScatterDisplay()
        {
            ClearGpuInstancingOnly();
            ClearMergedMeshOnly();
        }

        private void ClearGpuInstancers()
        {
            for (var i = 0; i < m_GpuInstancers.Count; i++)
                m_GpuInstancers[i]?.Clear();
            m_GpuInstancers.Clear();
        }

        private void ClearGpuInstancingOnly()
        {
            PcgScatterRenderBridge.Unregister(this);
            ClearGpuInstancers();
        }

        private void ClearScatterCpuCache()
        {
            m_CachedScatterInstancesList.Clear();
            m_CachedScatterMaterialNamesList.Clear();
            m_CachedScatterPointCounts.Clear();
            DestroyOwnedSpawnPrototypes();
        }

        private void TryRestoreGpuInstancingFromCache()
        {
            if (scatterDisplayMode != PcgScatterDisplayMode.GpuInstancing)
                return;
            if (m_CachedScatterInstancesList.Count == 0)
                return;

            ClearGpuInstancers();
            var anyActive = false;
            for (var i = 0; i < m_CachedScatterInstancesList.Count; i++)
            {
                var instances = m_CachedScatterInstancesList[i];
                if (instances == null || instances.Count == 0 || instances.PrototypeMesh == null)
                    continue;
                var names = i < m_CachedScatterMaterialNamesList.Count
                    ? m_CachedScatterMaterialNamesList[i]
                    : Array.Empty<string>();
                var materials = ResolveMaterialBindings(names, instances.PrototypeMesh.subMeshCount);
                var instancer = new PcgScatterGpuInstancer();
                instancer.Set(instances, materials, gameObject.layer, transform.localToWorldMatrix);
                m_GpuInstancers.Add(instancer);
                if (instancer.IsActive)
                    anyActive = true;
            }

            if (anyActive)
            {
                PcgScatterRenderBridge.Register(this);
                if (m_MeshRenderer != null)
                    m_MeshRenderer.enabled = false;
            }
        }

        private void ClearMergedMeshOnly()
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

            if (m_MeshFilter != null)
                m_MeshFilter.sharedMesh = null;
            if (m_MeshRenderer != null)
                m_MeshRenderer.enabled = false;
        }

        private void ClearGeneratedMesh()
        {
            ClearScatterDisplay();
            ClearScatterCpuCache();
        }

        private void ApplyMesh(Mesh mesh, IReadOnlyList<string> materialNames = null)
        {
            ClearGpuInstancingOnly();
            ClearScatterCpuCache();
            EnsureMeshComponents();

#if UNITY_EDITOR
            mesh = AdoptSceneEmbeddedMesh(mesh);
#endif

            if (m_GeneratedMesh != null && m_GeneratedMesh != mesh)
            {
#if UNITY_EDITOR
                DestroyImmediate(m_GeneratedMesh);
#else
                Destroy(m_GeneratedMesh);
#endif
            }

            m_GeneratedMesh = mesh;
            m_MeshFilter.sharedMesh = mesh;
            if (m_MeshRenderer != null)
            {
                m_MeshRenderer.enabled = mesh != null;
                ApplyMaterialBindings(materialNames);
            }
        }

#if UNITY_EDITOR
        /// <summary>
        /// Write cooked geometry into the scene-embedded Mesh (if any) so Save Scene
        /// persists the preview instead of leaving a stale sub-asset reference.
        /// </summary>
        private Mesh AdoptSceneEmbeddedMesh(Mesh cookedMesh)
        {
            if (cookedMesh == null || m_MeshFilter == null)
                return cookedMesh;

            var existing = m_MeshFilter.sharedMesh;
            if (existing == null || existing == cookedMesh ||
                UnityEditor.EditorUtility.IsPersistent(existing))
            {
                return cookedMesh;
            }

            CopyMeshGeometry(cookedMesh, existing);
            if (cookedMesh != existing)
                DestroyImmediate(cookedMesh);
            return existing;
        }

        private static void CopyMeshGeometry(Mesh source, Mesh destination)
        {
            destination.Clear(false);
            destination.indexFormat = source.indexFormat;
            destination.vertices = source.vertices;
            destination.normals = source.normals;
            destination.tangents = source.tangents;
            destination.colors = source.colors;
            destination.uv = source.uv;
            destination.uv2 = source.uv2;
            destination.uv3 = source.uv3;
            destination.uv4 = source.uv4;
            destination.subMeshCount = source.subMeshCount;
            for (var subMesh = 0; subMesh < source.subMeshCount; subMesh++)
                destination.SetTriangles(source.GetTriangles(subMesh), subMesh, false);
            destination.RecalculateBounds();
            if (!string.IsNullOrEmpty(source.name))
                destination.name = source.name;
        }

        private void MarkCookResultPersistable(Mesh mesh)
        {
            UnityEditor.EditorUtility.SetDirty(this);

            if (mesh != null)
                UnityEditor.EditorUtility.SetDirty(mesh);

            if (m_MeshFilter != null)
                UnityEditor.EditorUtility.SetDirty(m_MeshFilter);

            var groupVisualizer = GetComponent<PcgGroupVisualizer>();
            if (groupVisualizer != null)
                UnityEditor.EditorUtility.SetDirty(groupVisualizer);

            var scene = gameObject.scene;
            if (scene.IsValid())
                UnityEditor.SceneManagement.EditorSceneManager.MarkSceneDirty(scene);
        }
#endif

        private void ApplyMaterialBindings(IReadOnlyList<string> materialNames)
        {
            if (m_MeshRenderer == null)
                return;

            var subMeshCount = m_MeshFilter != null && m_MeshFilter.sharedMesh != null
                ? m_MeshFilter.sharedMesh.subMeshCount
                : (materialNames?.Count ?? 0);
            m_MeshRenderer.sharedMaterials = ResolveMaterialBindings(materialNames, subMeshCount);
        }

        /// <summary>
        /// Resolve slot names → Materials for MeshRenderer and GPU instancing.
        /// Truncates or pads to <paramref name="subMeshCount"/>; empty/missing names use fallback.
        /// </summary>
        internal Material[] ResolveMaterialBindings(
            IReadOnlyList<string> materialNames,
            int subMeshCount)
        {
            var fallback = ResolveFallbackMaterial();
            var count = subMeshCount > 0
                ? subMeshCount
                : (materialNames != null && materialNames.Count > 0 ? materialNames.Count : 1);
            if (count <= 0)
                count = 1;

            var resolved = new Material[count];
            for (var slot = 0; slot < count; slot++)
            {
                resolved[slot] = fallback;
                if (materialNames == null || slot >= materialNames.Count)
                    continue;

                var name = materialNames[slot];
                if (string.IsNullOrEmpty(name))
                    continue;

                foreach (var binding in m_MaterialBindings)
                {
                    if (binding != null && binding.material != null && binding.materialName == name)
                    {
                        resolved[slot] = binding.material;
                        break;
                    }
                }
            }

            return resolved;
        }

        /// <summary>
        /// Pure resolver for EditMode tests (no MeshRenderer required).
        /// </summary>
        internal static Material[] ResolveMaterialBindings(
            IReadOnlyList<string> materialNames,
            int subMeshCount,
            IReadOnlyList<PcgMaterialBinding> bindings,
            Material fallback)
        {
            var count = subMeshCount > 0
                ? subMeshCount
                : (materialNames != null && materialNames.Count > 0 ? materialNames.Count : 1);
            if (count <= 0)
                count = 1;

            var resolved = new Material[count];
            for (var slot = 0; slot < count; slot++)
            {
                resolved[slot] = fallback;
                if (materialNames == null || slot >= materialNames.Count)
                    continue;

                var name = materialNames[slot];
                if (string.IsNullOrEmpty(name) || bindings == null)
                    continue;

                foreach (var binding in bindings)
                {
                    if (binding != null && binding.material != null && binding.materialName == name)
                    {
                        resolved[slot] = binding.material;
                        break;
                    }
                }
            }

            return resolved;
        }

        private Material ResolveFallbackMaterial()
        {
            if (meshMaterial != null)
                return meshMaterial;

            EnsureMeshComponents();
            if (m_MeshRenderer != null && m_MeshRenderer.sharedMaterial != null)
                return m_MeshRenderer.sharedMaterial;

            var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
            return shader != null
                ? new Material(shader) { color = new Color(0.55f, 0.75f, 0.95f) }
                : null;
        }

        private void EnsureMeshComponents()
        {
            if (m_MeshFilter == null)
            {
                m_MeshFilter = GetComponent<MeshFilter>();
                if (m_MeshFilter == null)
                    m_MeshFilter = gameObject.AddComponent<MeshFilter>();
            }
            if (m_MeshRenderer == null)
            {
                m_MeshRenderer = GetComponent<MeshRenderer>();
                if (m_MeshRenderer == null)
                    m_MeshRenderer = gameObject.AddComponent<MeshRenderer>();
                if (m_MeshRenderer.sharedMaterial == null)
                {
                    var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
                    m_MeshRenderer.sharedMaterial = meshMaterial != null
                        ? meshMaterial
                        : new Material(shader) { color = new Color(0.55f, 0.75f, 0.95f) };
                }
            }
        }

        private Mesh BuildScatterMesh(PcgInstanceList instanceList)
        {
            if (instanceList == null || instanceList.Count == 0 || instanceList.PrototypeMesh == null)
                return null;

            var sourceMesh = instanceList.PrototypeMesh;
            var combines = new CombineInstance[instanceList.Count];
            for (var i = 0; i < instanceList.Count; i++)
            {
                combines[i] = new CombineInstance
                {
                    mesh = sourceMesh,
                    transform = instanceList.LocalMatrices[i]
                };
            }

            var mesh = new Mesh { name = "PCG Scatter Points Mesh" };
            if (instanceList.Count * sourceMesh.vertexCount > 65535)
                mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
            mesh.CombineMeshes(combines, true, true, false);
            mesh.RecalculateBounds();
            mesh.RecalculateNormals();
            mesh.RecalculateTangents();
            return mesh;
        }

        private static Mesh BuildSpawnPrototypeMesh(
            PcgGraphExecuteResult result,
            out string[] materialNames)
        {
            materialNames = Array.Empty<string>();
            if (result?.MeshBinary == null || result.MeshBinary.Length < PcgNative.MeshBinaryHeaderSize)
                return null;

            if (!PcgResultParser.TryParseMeshBinary(
                    result.MeshBinary, out var mesh, out materialNames, out _))
            {
                materialNames = Array.Empty<string>();
                return null;
            }

            mesh.name = "PCG Spawn Prototype Mesh";
            materialNames ??= Array.Empty<string>();
            return mesh;
        }

        /// <summary>FNV-1a 64-bit hash of a byte array (0 if null/empty).</summary>
        private static ulong ComputeBinaryHash(byte[] data)
        {
            if (data == null || data.Length == 0)
                return 0;
            const ulong fnvOffsetBasis = 14695981039346656037UL;
            const ulong fnvPrime = 1099511628211UL;
            ulong hash = fnvOffsetBasis;
            for (int i = 0; i < data.Length; i++)
            {
                hash ^= data[i];
                hash *= fnvPrime;
            }
            return hash;
        }

        private Mesh ResolveScatterPointMesh()
        {
            if (scatterPointMesh != null)
                return scatterPointMesh;

            var sphere = Resources.GetBuiltinResource<Mesh>("Sphere.fbx");
            if (sphere != null)
                return sphere;

            var cube = Resources.GetBuiltinResource<Mesh>("Cube.fbx");
            return cube;
        }

        private void SetOwnedSpawnPrototypes(List<PcgResultParser.PcgSpawnPrototype> prototypes)
        {
            DestroyOwnedSpawnPrototypes();
            if (prototypes == null)
                return;
            for (var i = 0; i < prototypes.Count; i++)
            {
                var mesh = prototypes[i]?.Mesh;
                if (mesh == null || mesh == scatterPointMesh)
                    continue;
                m_OwnedSpawnPrototypeMeshes.Add(mesh);
            }
        }

        private void DestroyOwnedSpawnPrototypes()
        {
            // Point-only previews (e.g. Keep Short Sites) reuse shared/builtin meshes
            // from ResolveScatterPointMesh(); those must never be destroyed.
            var sharedPointMesh = ResolveScatterPointMesh();
            for (var i = 0; i < m_OwnedSpawnPrototypeMeshes.Count; i++)
            {
                var mesh = m_OwnedSpawnPrototypeMeshes[i];
                if (mesh == null)
                    continue;
                if (mesh == scatterPointMesh || mesh == sharedPointMesh)
                    continue;
#if UNITY_EDITOR
                // Project / builtin assets are tracked for GPU cache but are not owned.
                if (UnityEditor.EditorUtility.IsPersistent(mesh))
                    continue;
                DestroyImmediate(mesh);
#else
                Destroy(mesh);
#endif
            }
            m_OwnedSpawnPrototypeMeshes.Clear();
        }

        private void DestroyTemporarySpawnPrototypes(List<PcgResultParser.PcgSpawnPrototype> prototypes)
        {
            if (prototypes == null)
                return;
            var sharedPointMesh = ResolveScatterPointMesh();
            for (var i = 0; i < prototypes.Count; i++)
            {
                var mesh = prototypes[i]?.Mesh;
                if (mesh == null)
                    continue;
                if (mesh == scatterPointMesh || mesh == sharedPointMesh)
                    continue;
#if UNITY_EDITOR
                if (UnityEditor.EditorUtility.IsPersistent(mesh))
                    continue;
                DestroyImmediate(mesh);
#else
                Destroy(mesh);
#endif
            }
        }
    }
}
