using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
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
        [SerializeField] private bool enableAsyncCookInEditor = true;

        [SerializeField]
        private List<PcgParameterOverride> m_ParameterOverrides = new();

        [SerializeField]
        private List<PcgMeshBinding> m_MeshBindings = new();

        private float m_NextEditModeCookTime;
        private bool m_PreviewCookPending;
        private bool m_CookInProgress;
        private bool m_AsyncCookInProgress;
        private bool m_ForceFullQuality;
        private CancellationTokenSource m_AsyncCookCts;
        private Task<AsyncCookResult> m_AsyncCookTask;
        private int m_AsyncCookGeneration;
        private string m_LastAsyncCookStatus = "idle";
        private string m_LastCookKey;
        private bool m_HasAppliedCookResult;
        private ulong m_LastMeshBinaryHash;

#if UNITY_EDITOR
        private static readonly HashSet<PcgGraphComponent> s_EditModePreviewCooks = new();

        /// <summary>Editor-only preview mesh bindings (Graph Editor / Inspector).</summary>
        public static System.Func<PcgGraphComponent, IReadOnlyList<PcgPreviewMeshBinding>> EditorResolvePreviewMeshBindings;
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
        public PcgCookMode CookMode => cookMode;
        public PcgScatterDisplayMode ScatterDisplayMode => scatterDisplayMode;

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
        public static System.Func<PcgGraphComponent, PcgPreviewQuality, string> EditorBuildExecutionJson;

        /// <summary>Invoked after a preview cook applies results (SceneView repaint).</summary>
        public static System.Action EditorAfterPreviewCookApplied;
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
            if (!Application.isPlaying && SupportsEditModePreview())
                RequestPreviewCook(immediate: true);
        }

        private void OnDisable()
        {
#if UNITY_EDITOR
            s_EditModePreviewCooks.Remove(this);
#endif
            PcgScatterRenderBridge.Unregister(this);
            CancelAsyncCook(null, log: false);
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

            m_GpuInstancer.Draw(camera);
        }

#if UNITY_EDITOR
        /// <summary>Called by <c>PcgEditModeCookScheduler</c> in the Editor assembly.</summary>
        public static void TickAllEditModePreviewCooks()
        {
            foreach (var component in s_EditModePreviewCooks.ToArray())
            {
                if (component != null)
                {
                    component.PumpAsyncCookCompletion();
                    component.TickEditModePreviewCook();
                }
            }
        }

        public static bool CancelAllEditModeAsyncCooks()
        {
            var cancelledAny = false;
            foreach (var component in s_EditModePreviewCooks.ToArray())
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
                if (!m_AsyncCookInProgress)
                    EditorAfterPreviewCookApplied?.Invoke();
            }
            else if (!IsCookBusy())
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

            if (m_AsyncCookInProgress)
            {
                if (ShouldUseAsyncCook(forceFullQuality))
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

            var cookKey = PcgGraphCookCache.BuildKey(json, seed, quality);
            if (TryReuseCachedCook(cookKey))
                return true;

            var textures = PcgTextureResolver.CollectFromGraphJson(json);
            if (!PcgTextureGraphUtil.TryValidateTextureRequirements(json, textures, out var textureError))
            {
                Debug.LogError($"[PCG] {textureError}");
                return false;
            }

            var previewBindings = EditorResolvePreviewMeshBindings?.Invoke(this);
            var meshes = PcgMeshResolver.CollectFromGraphJson(
                json, gameObject, m_MeshBindings, previewBindings);
            if (!PcgMeshGraphUtil.TryValidateMeshRequirements(json, meshes, out _))
                return false;

            if (ShouldUseAsyncCook(m_ForceFullQuality))
            {
                StartAsyncCook(json, textures, meshes);
                return true;
            }

            var result = PcgGraphLoader.Execute(json, seed, textures, meshes, quality);
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
                return true;
            }

            return PcgGraphCookCache.TryGet(cookKey, out var cached) && CommitCookResult(cookKey, cached);
        }

        private bool CommitCookResult(string cookKey, PcgGraphExecuteResult result)
        {
            if (!ApplyExecutionResult(result))
                return false;

            m_LastCookKey = cookKey;
            m_HasAppliedCookResult = true;
            return true;
        }

        private void InvalidateCookResult()
        {
            m_LastCookKey = null;
            m_HasAppliedCookResult = false;
            m_LastMeshBinaryHash = 0;
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
                {
                    var binHash = ComputeBinaryHash(result.MeshBinary);
                    if (binHash != 0 && binHash == m_LastMeshBinaryHash && m_GeneratedMesh != null)
                    {
                        ApplyMesh(m_GeneratedMesh);
                    }
                    else
                    {
                        if (!PcgResultParser.TryParseMeshBinary(result.MeshBinary, out var mesh, out var meshError))
                        {
                            Debug.LogError($"[PCG] Failed to parse mesh result: {meshError}");
                            return false;
                        }
                        ApplyMesh(mesh);
                        m_LastMeshBinaryHash = binHash;
                    }
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
                        Mesh spawnPrototype;
                        if (spawnHash != 0 && spawnHash == m_LastMeshBinaryHash && m_OwnedSpawnPrototypeMesh != null)
                            spawnPrototype = m_OwnedSpawnPrototypeMesh;
                        else
                        {
                            spawnPrototype = BuildSpawnPrototypeMesh(result);
                            m_LastMeshBinaryHash = spawnHash;
                        }
                        ApplyPoints(points, spawnPrototype);
                    }
                    else
                    {
                        if (!PcgResultParser.TryParsePoints(result.Json, out var parsed, out var parseError))
                        {
                            Debug.LogError($"[PCG] Failed to parse point result: {parseError}");
                            return false;
                        }
                        ApplyPoints(PcgResultParser.ToScatterPoints(PcgResultParser.ToVector3List(parsed)), BuildSpawnPrototypeMesh(result));
                    }
                    break;

                default:
                    Debug.LogError("[PCG] Unknown result JSON shape.");
                    return false;
            }

            return true;
        }

        private bool IsCookBusy() => m_CookInProgress || m_AsyncCookInProgress;

        private bool ShouldUseAsyncCook(bool forceFullQuality)
        {
#if UNITY_EDITOR
            return enableAsyncCookInEditor && !Application.isPlaying && !forceFullQuality;
#else
            return false;
#endif
        }

        private void StartAsyncCook(
            string json,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes)
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

                var (validateCode, validateError) = PcgNative.ValidateGraph(json);
                if (validateCode != PcgResultCode.Ok)
                {
                    return AsyncCookResult.Failed(
                        generation,
                        $"Validation failed ({validateCode}): {validateError}");
                }

                if (token.IsCancellationRequested)
                    return AsyncCookResult.FromCancelled(generation);

                var (execCode, execResult) = PcgNative.ExecuteGraph(json, localSeed, textures, meshes);
                if (execCode != PcgResultCode.Ok)
                {
                    return AsyncCookResult.Failed(
                        generation,
                        $"Execution failed ({execCode}): {execResult?.Error}");
                }

                return AsyncCookResult.Succeeded(generation, execResult);
            }, token);
        }

        private bool CancelAsyncCook(string reason, bool log = true)
        {
            if (!m_AsyncCookInProgress)
                return false;

            PcgNative.RequestCancel();
            m_AsyncCookCts?.Cancel();
            m_AsyncCookInProgress = false;
            m_AsyncCookTask = null;
            m_AsyncCookCts?.Dispose();
            m_AsyncCookCts = null;
            m_LastAsyncCookStatus = "cancelled";

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
                return;

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
        }

        // --- Result rendering ---

        private void ApplyPoints(List<PcgScatterPoint> points, Mesh pointPrototypeMesh = null)
        {
            var prototypeMesh = pointPrototypeMesh != null ? pointPrototypeMesh : ResolveScatterPointMesh();
            if (!PcgInstanceList.TryBuild(points, prototypeMesh, scatterPointScale, out var instanceList))
            {
                ClearScatterDisplay();
                return;
            }

            if (scatterDisplayMode == PcgScatterDisplayMode.GpuInstancing)
            {
                ClearMergedMeshOnly();
                SetOwnedSpawnPrototype(pointPrototypeMesh);
                var material = ResolveMeshMaterial();
                m_GpuInstancer.Set(instanceList, material, gameObject.layer, transform.localToWorldMatrix);
                PcgScatterRenderBridge.Register(this);
                if (m_MeshRenderer != null)
                    m_MeshRenderer.enabled = false;
                return;
            }

            ClearGpuInstancingOnly();
            var scatterMesh = BuildScatterMesh(instanceList);
            ApplyMesh(scatterMesh);
        }

        private void ApplySplines(List<List<Vector3>> splines)
        {
            ClearGeneratedMesh();
            if (splines != null && splines.Count > 0 && PcgProjectSettings.IsLogEnabled)
                Debug.Log($"[PCG] Spline result has {splines.Count} spline(s); spline mesh rendering is not implemented yet.");
        }

        private Mesh m_GeneratedMesh;
        private Mesh m_OwnedSpawnPrototypeMesh;
        private MeshFilter m_MeshFilter;
        private MeshRenderer m_MeshRenderer;
        private readonly PcgScatterGpuInstancer m_GpuInstancer = new();

        private void ClearScatterDisplay()
        {
            ClearGpuInstancingOnly();
            ClearMergedMeshOnly();
        }

        private void ClearGpuInstancingOnly()
        {
            PcgScatterRenderBridge.Unregister(this);
            m_GpuInstancer.Clear();
            DestroyOwnedSpawnPrototype();
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
        }

        private void ApplyMesh(Mesh mesh)
        {
            ClearGpuInstancingOnly();

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

        private static Mesh BuildSpawnPrototypeMesh(PcgGraphExecuteResult result)
        {
            if (result?.MeshBinary == null || result.MeshBinary.Length < PcgNative.MeshBinaryHeaderSize)
                return null;

            if (!PcgResultParser.TryParseMeshBinary(result.MeshBinary, out var mesh, out _))
                return null;

            mesh.name = "PCG Spawn Prototype Mesh";
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

        private Material ResolveMeshMaterial()
        {
            EnsureMeshComponents();
            if (m_MeshRenderer == null)
                return null;

            var material = m_MeshRenderer.sharedMaterial;
            if (material != null)
                material.enableInstancing = true;
            return material;
        }

        private void SetOwnedSpawnPrototype(Mesh spawnPrototypeMesh)
        {
            if (spawnPrototypeMesh == null || spawnPrototypeMesh == scatterPointMesh)
            {
                DestroyOwnedSpawnPrototype();
                return;
            }

            if (m_OwnedSpawnPrototypeMesh == spawnPrototypeMesh)
                return;

            DestroyOwnedSpawnPrototype();
            m_OwnedSpawnPrototypeMesh = spawnPrototypeMesh;
        }

        private void DestroyOwnedSpawnPrototype()
        {
            if (m_OwnedSpawnPrototypeMesh == null)
                return;

#if UNITY_EDITOR
            DestroyImmediate(m_OwnedSpawnPrototypeMesh);
#else
            Destroy(m_OwnedSpawnPrototypeMesh);
#endif
            m_OwnedSpawnPrototypeMesh = null;
        }
    }
}
