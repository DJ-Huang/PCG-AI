// ReviewPage.tsx — Clean review page that loads a .pcg graph, cooks it via
// pcg-server, and renders only the PreviewViewport (no editor UI).
//
// URL: /review?graph=<relative-path-to-pcg-file>
// Optional camera: &camera=front|side|top|three-quarter&frontAxis=+z&sideView=right
// The path is resolved relative to the workspace root (parent of web/).
//
// Sets window.__pcgReady = true when the first framed render is complete so
// Playwright can capture a screenshot. window.__pcgReview.setCamera switches
// deterministic views without reloading.

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import PreviewViewport, {
  type PreviewViewportHandle,
  type ShadingMode,
} from './PreviewViewport';
import {
  cancelCook,
  cookGraphPreviewBudgeted,
  newPreviewJobId,
  type PreviewData,
  type PreviewQualityMode,
} from './previewCook';
import type { GraphJson } from './graphSchema';
import { saveGraphToFile } from './exportGraph';
import {
  applyPreviewParameterOverrides,
  resolvePreviewParameterValues,
  savePreviewParameterDefaults,
  type PreviewParameterValue,
  type PreviewParameterValues,
} from './previewParameters';
import {
  parseFrontAxis,
  parseReviewCameraView,
  parseSideView,
  type ReviewCameraPose,
  type ReviewCameraView,
} from './reviewCamera';
import { exportPreviewMeshGlb } from './previewGlbExport';

interface ReviewState {
  loading: boolean;
  error: string | null;
  data: PreviewData | null;
}

interface PcgReviewApi {
  ready: boolean;
  camera: ReviewCameraPose | null;
  setCamera: (view: ReviewCameraView) => ReviewCameraPose | null;
  capture: (options?: Parameters<PreviewViewportHandle['captureFrame']>[0]) => (
    ReturnType<PreviewViewportHandle['captureFrame']>
  );
  downloadGlb: () => Promise<string | null>;
  animations: string[];
  components: string[];
  playAnimation: (name: string) => boolean;
  pauseAnimation: () => void;
  resumeAnimation: () => boolean;
  stopAnimation: () => void;
  seekAnimation: (name: string, timeSeconds: number) => boolean;
  setAnimationSpeed: (speed: number) => void;
  setAnimationLoop: (loop: boolean) => void;
  getAnimationPlaybackState: () => ReturnType<PreviewViewportHandle['getAnimationPlaybackState']>;
  setExplode: (amount: number) => void;
  inspectActionRuntime: () => ReturnType<PreviewViewportHandle['inspectActionRuntime']>;
}

function readReviewQuery() {
  const params = new URLSearchParams(window.location.search);
  const requestedShading = params.get('shading');
  const requestedQuality = params.get('quality');
  return {
    graphPath: params.get('graph'),
    camera: parseReviewCameraView(params.get('camera')),
    frontAxis: parseFrontAxis(params.get('frontAxis')),
    sideView: parseSideView(params.get('sideView')),
    shadingMode: (requestedShading === 'solid' ? 'solid' : 'material') as ShadingMode,
    actionRuntimeEnabled: params.get('action') !== 'off',
    quality: (
      requestedQuality === 'full' || requestedQuality === 'medium' || requestedQuality === 'low'
        ? requestedQuality
        : 'adaptive'
    ) as PreviewQualityMode,
  };
}

export default function ReviewPage() {
  const [state, setState] = useState<ReviewState>({
    loading: true,
    error: null,
    data: null,
  });
  const [graph, setGraph] = useState<GraphJson | null>(null);
  const [parameterValues, setParameterValues] = useState<PreviewParameterValues>({});
  const parameterValuesRef = useRef<PreviewParameterValues>({});
  const initialParametersResolvedRef = useRef(false);
  const suppressNextParameterCookRef = useRef(false);
  const viewportRef = useRef<PreviewViewportHandle>(null);
  const query = useMemo(() => readReviewQuery(), []);
  const [quality, setQuality] = useState<PreviewQualityMode>(query.quality);
  const qualityRef = useRef<PreviewQualityMode>(query.quality);
  const cookAbortRef = useRef<AbortController | null>(null);
  const cookJobIdRef = useRef('');
  const [reviewView, setReviewView] = useState<ReviewCameraView>(
    query.camera ?? 'three-quarter',
  );
  const [explode, setExplode] = useState(0);
  const [exporting, setExporting] = useState(false);

  const graphPath = query.graphPath;

  const cook = useCallback(async () => {
    if (!graphPath) {
      setState({ loading: false, error: 'Missing ?graph= query parameter', data: null });
      return;
    }

    cookAbortRef.current?.abort();
    if (cookJobIdRef.current) void cancelCook(cookJobIdRef.current);
    const abort = new AbortController();
    const jobId = newPreviewJobId();
    cookAbortRef.current = abort;
    cookJobIdRef.current = jobId;
    setState({ loading: true, error: null, data: null });

    try {
      const res = await fetch(`/api/load-graph?path=${encodeURIComponent(graphPath)}`);
      if (!res.ok) {
        const text = await res.text();
        let msg = text;
        try {
          const parsed = JSON.parse(text);
          if (parsed.error) msg = parsed.error;
        } catch { /* keep raw text */ }
        setState({ loading: false, error: `Failed to load graph: ${msg}`, data: null });
        return;
      }

      const nextGraph = (await res.json()) as GraphJson;
      const resolvedValues = resolvePreviewParameterValues(
        nextGraph.parameters ?? [],
        parameterValuesRef.current,
      );
      setGraph(nextGraph);
      initialParametersResolvedRef.current = true;
      setParameterValues((current) => {
        if (JSON.stringify(current) === JSON.stringify(resolvedValues)) return current;
        suppressNextParameterCookRef.current = true;
        parameterValuesRef.current = resolvedValues;
        return resolvedValues;
      });
      const response = await cookGraphPreviewBudgeted(
        applyPreviewParameterOverrides(nextGraph, resolvedValues),
        42,
        abort.signal,
        jobId,
        { quality: qualityRef.current },
      );
      if (cookAbortRef.current !== abort) return;

      if (response.ok && response.data) {
        setState({ loading: false, error: null, data: response.data });
      } else {
        setState({ loading: false, error: response.error ?? 'Cook failed', data: null });
      }
    } catch (err) {
      if (cookAbortRef.current === abort) {
        setState({ loading: false, error: String(err), data: null });
      }
    }
  }, [graphPath]);

  useEffect(() => {
    const timer = setTimeout(() => void cook(), 600);
    return () => {
      clearTimeout(timer);
      cookAbortRef.current?.abort();
      if (cookJobIdRef.current) void cancelCook(cookJobIdRef.current);
    };
  }, [cook]);

  useEffect(() => {
    qualityRef.current = quality;
    if (!initialParametersResolvedRef.current) return undefined;
    const timer = setTimeout(() => void cook(), 250);
    return () => clearTimeout(timer);
  }, [cook, quality]);

  useEffect(() => {
    parameterValuesRef.current = parameterValues;
    if (!initialParametersResolvedRef.current) return undefined;
    if (suppressNextParameterCookRef.current) {
      suppressNextParameterCookRef.current = false;
      return undefined;
    }
    const timer = setTimeout(() => void cook(), 600);
    return () => clearTimeout(timer);
  }, [cook, parameterValues]);

  const parameters = useMemo(() => graph?.parameters ?? [], [graph]);
  const parameterNodeIds = useMemo(
    () => new Set((graph?.nodes ?? []).map((node) => node.id)),
    [graph],
  );

  const updateParameterValue = useCallback((parameterId: string, value: PreviewParameterValue) => {
    setParameterValues((current) => ({ ...current, [parameterId]: value }));
  }, []);

  const resetParameters = useCallback(() => {
    setParameterValues(resolvePreviewParameterValues(parameters, undefined));
  }, [parameters]);

  const saveParameterDefaults = useCallback(async () => {
    if (!graph || !graphPath) return;
    const saved = savePreviewParameterDefaults(graph.nodes, parameters, parameterValues);
    const nextGraph: GraphJson = {
      ...graph,
      nodes: saved.nodes,
      parameters: saved.parameters,
    };
    const result = await saveGraphToFile(nextGraph, graphPath);
    if (!result.ok) {
      setState((current) => ({ ...current, error: `Save defaults failed: ${result.error ?? 'unknown error'}` }));
      return;
    }
    setGraph(nextGraph);
  }, [graph, graphPath, parameters, parameterValues]);

  const downloadGlb = useCallback(async (): Promise<string | null> => {
    if (!state.data?.mesh || !graphPath || !graph || exporting) return null;
    setExporting(true);
    const slug = graphPath.split('/').pop()?.replace(/\.pcg$|\.json$/, '') || 'pcg-asset';
    const values = Object.fromEntries(
      parameters.map((parameter) => [parameter.name, parameterValues[parameter.id] ?? parameter.default]),
    );
    try {
      const preserved = viewportRef.current?.getPreservedGltfBytes() ?? null;
      let exportData = state.data;
      if (!preserved) {
        const fullResponse = await cookGraphPreviewBudgeted(
          applyPreviewParameterOverrides(graph, parameterValues),
          42,
          undefined,
          newPreviewJobId(),
          { quality: 'full', enforceBudget: false },
        );
        if (!fullResponse.ok || !fullResponse.data?.mesh) {
          throw new Error(fullResponse.error ?? 'Full-quality export cook produced no mesh');
        }
        exportData = fullResponse.data;
      }
      const bytes = preserved ?? exportPreviewMeshGlb(
        exportData.mesh!,
        exportData.materials,
        {
          name: slug,
          graphPath,
          frontAxis: query.frontAxis,
          parameters: values,
          actionRuntime: exportData.actionRuntime,
        },
      );
      const filename = `${slug}.glb`;
      const url = URL.createObjectURL(new Blob([bytes], { type: 'model/gltf-binary' }));
      const link = document.createElement('a');
      link.href = url;
      link.download = filename;
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 0);
      return filename;
    } catch (error) {
      setState((current) => ({
        ...current,
        error: `Export failed: ${error instanceof Error ? error.message : String(error)}`,
      }));
      return null;
    } finally {
      setExporting(false);
    }
  }, [exporting, graph, graphPath, parameterValues, parameters, query.frontAxis, state.data]);

  const markReady = useCallback((pose: ReviewCameraPose | null) => {
    const api: PcgReviewApi = {
      ready: true,
      camera: pose,
      setCamera: (view) => {
        setReviewView(view);
        const next = viewportRef.current?.setReviewCamera(view, {
          frontAxis: query.frontAxis,
          sideView: query.sideView,
        }) ?? null;
        api.camera = next;
        return next;
      },
      capture: (options) => viewportRef.current?.captureFrame(options) ?? null,
      downloadGlb,
      get animations() { return viewportRef.current?.listAnimations() ?? []; },
      get components() { return viewportRef.current?.listComponents() ?? []; },
      playAnimation: (name) => viewportRef.current?.playAnimation(name) ?? false,
      pauseAnimation: () => viewportRef.current?.pauseAnimation(),
      resumeAnimation: () => viewportRef.current?.resumeAnimation() ?? false,
      stopAnimation: () => viewportRef.current?.stopAnimation(),
      seekAnimation: (name, timeSeconds) => (
        viewportRef.current?.seekAnimation(name, timeSeconds) ?? false
      ),
      setAnimationSpeed: (speed) => viewportRef.current?.setAnimationSpeed(speed),
      setAnimationLoop: (loop) => viewportRef.current?.setAnimationLoop(loop),
      getAnimationPlaybackState: () => viewportRef.current?.getAnimationPlaybackState() ?? null,
      setExplode: (amount) => viewportRef.current?.setExplode(amount),
      inspectActionRuntime: () => viewportRef.current?.inspectActionRuntime() ?? null,
    };
    const host = window as unknown as {
      __pcgReady?: boolean;
      __pcgReview?: PcgReviewApi;
    };
    host.__pcgReview = api;
    host.__pcgReady = true;
  }, [downloadGlb, query.frontAxis, query.sideView]);

  const reviewData = useMemo<PreviewData | null>(() => {
    if (!state.data || query.actionRuntimeEnabled) return state.data;
    return { ...state.data, actionRuntime: null };
  }, [query.actionRuntimeEnabled, state.data]);
  const hasDetachableComponents = reviewData?.actionRuntime?.rig.components
    .some((component) => component.detachable) ?? false;

  useEffect(() => {
    if (state.loading || state.error) return undefined;
    const timer = setTimeout(() => {
      const pose = viewportRef.current?.setReviewCamera(reviewView, {
        frontAxis: query.frontAxis,
        sideView: query.sideView,
      }) ?? null;
      markReady(pose);
    }, 220);
    return () => clearTimeout(timer);
  }, [state.loading, state.error, state.data, reviewView, query.frontAxis, query.sideView, markReady]);

  const slug = graphPath
    ? graphPath.split('/').pop()?.replace(/\.pcg$|\.json$/, '') ?? 'review'
    : 'review';

  return (
    <div style={{ width: '100vw', height: '100vh', display: 'flex', flexDirection: 'column', background: '#1e1e1e' }}>
      <div style={{ padding: '8px 12px', color: '#aaa', fontFamily: 'monospace', fontSize: '12px', flexShrink: 0 }}>
        PCG Review — {slug}
        {state.data?.cook && (
          <span style={{ marginLeft: '12px', color: '#666' }}>
            verts={state.data.mesh?.vertexCount ?? state.data.geometry?.pointCount ?? 0}
            {' '}
            tris={state.data.previewQuality?.triangleCount ?? Math.floor((state.data.mesh?.indexCount ?? 0) / 3)}
            {' '}
            nodes={state.data.cook.nodesExecuted}
            {' '}
            {state.data.cook.graphExecuteMs.toFixed(0)}ms
            {' '}
            cam={reviewView}
            {state.data.previewQuality && state.data.previewQuality.sdfNodeCount > 0 && (
              <>
                {' '}
                quality={state.data.previewQuality.requestedQuality}
                {' '}
                scale=×{state.data.previewQuality.effectiveScale.toFixed(2)}
                {state.data.previewQuality.triangleBudget !== null && (
                  <>
                    {' '}
                    budget={state.data.previewQuality.triangleBudget}
                  </>
                )}
              </>
            )}
          </span>
        )}
        <button
          type="button"
          onClick={() => void downloadGlb()}
          disabled={!state.data?.mesh || exporting}
          style={{ marginLeft: '12px' }}
        >
          {exporting ? 'Exporting Full…' : 'Export Full GLB'}
        </button>
        <label style={{ marginLeft: '12px' }}>
          Preview{' '}
          <select
            aria-label="Preview quality"
            value={quality}
            disabled={state.loading}
            onChange={(event) => setQuality(event.target.value as PreviewQualityMode)}
          >
            <option value="adaptive">Adaptive</option>
            <option value="full">Full</option>
            <option value="medium">Medium ×2</option>
            <option value="low">Low ×3</option>
          </select>
        </label>
        {hasDetachableComponents && (
          <span style={{ marginLeft: '12px', display: 'inline-flex', gap: '6px', alignItems: 'center' }}>
            <label>
              Explode
              <input
                aria-label="Component explode"
                type="range"
                min={0}
                max={0.5}
                step={0.005}
                value={explode}
                onChange={(event) => {
                  const amount = Number(event.target.value);
                  setExplode(amount);
                  viewportRef.current?.setExplode(amount);
                }}
              />
            </label>
          </span>
        )}
      </div>
      <div style={{ flex: 1, minHeight: 0 }}>
        <PreviewViewport
          ref={viewportRef}
          data={reviewData}
          loading={state.loading}
          error={state.error}
          onRefresh={() => void cook()}
          parameters={parameters}
          parameterNodeIds={parameterNodeIds}
          parameterValues={parameterValues}
          onParameterValueChange={updateParameterValue}
          onResetParameters={resetParameters}
          onSaveParameterDefaults={() => void saveParameterDefaults()}
          reviewMode
          initialShadingMode={query.shadingMode}
          initialAnimationModeOpen
          reviewCamera={{
            view: reviewView,
            frontAxis: query.frontAxis,
            sideView: query.sideView,
          }}
          onReviewCameraApplied={markReady}
        />
      </div>
    </div>
  );
}
