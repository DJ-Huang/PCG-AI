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
import PreviewViewport, { type PreviewViewportHandle } from './PreviewViewport';
import {
  cookGraphPreview,
  prepareGraphForPreviewCook,
  type PreviewData,
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

interface ReviewState {
  loading: boolean;
  error: string | null;
  data: PreviewData | null;
}

interface PcgReviewApi {
  ready: boolean;
  camera: ReviewCameraPose | null;
  setCamera: (view: ReviewCameraView) => ReviewCameraPose | null;
  capture: () => ReturnType<PreviewViewportHandle['captureFrame']>;
}

function readReviewQuery() {
  const params = new URLSearchParams(window.location.search);
  return {
    graphPath: params.get('graph'),
    camera: parseReviewCameraView(params.get('camera')),
    frontAxis: parseFrontAxis(params.get('frontAxis')),
    sideView: parseSideView(params.get('sideView')),
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
  const viewportRef = useRef<PreviewViewportHandle>(null);
  const query = useMemo(() => readReviewQuery(), []);
  const [reviewView, setReviewView] = useState<ReviewCameraView>(
    query.camera ?? 'three-quarter',
  );

  const graphPath = query.graphPath;

  const cook = useCallback(async () => {
    if (!graphPath) {
      setState({ loading: false, error: 'Missing ?graph= query parameter', data: null });
      return;
    }

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
      const resolvedValues = resolvePreviewParameterValues(nextGraph.parameters ?? [], parameterValues);
      setGraph(nextGraph);
      setParameterValues((current) => (
        JSON.stringify(current) === JSON.stringify(resolvedValues) ? current : resolvedValues
      ));
      const prepared = prepareGraphForPreviewCook(
        applyPreviewParameterOverrides(nextGraph, resolvedValues),
      );
      const response = await cookGraphPreview(prepared, 42);

      if (response.ok && response.data) {
        setState({ loading: false, error: null, data: response.data });
      } else {
        setState({ loading: false, error: response.error ?? 'Cook failed', data: null });
      }
    } catch (err) {
      setState({ loading: false, error: String(err), data: null });
    }
  }, [graphPath, parameterValues]);

  useEffect(() => {
    const timer = setTimeout(() => void cook(), 600);
    return () => clearTimeout(timer);
  }, [cook]);

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
      capture: () => viewportRef.current?.captureFrame() ?? null,
    };
    const host = window as unknown as {
      __pcgReady?: boolean;
      __pcgReview?: PcgReviewApi;
    };
    host.__pcgReview = api;
    host.__pcgReady = true;
  }, [query.frontAxis, query.sideView]);

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
            nodes={state.data.cook.nodesExecuted}
            {' '}
            {state.data.cook.graphExecuteMs.toFixed(0)}ms
            {' '}
            cam={reviewView}
          </span>
        )}
      </div>
      <div style={{ flex: 1, minHeight: 0 }}>
        <PreviewViewport
          ref={viewportRef}
          data={state.data}
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
