// ReviewPage.tsx — Clean review page that loads a .pcg graph, cooks it via
// pcg-server, and renders only the PreviewViewport (no editor UI).
//
// URL: /review?graph=<relative-path-to-pcg-file>
// The path is resolved relative to the workspace root (parent of web/).
//
// Sets window.__pcgReady = true when the first render is complete so
// Playwright can capture a screenshot.

import { useCallback, useEffect, useState } from 'react';
import PreviewViewport from './PreviewViewport';
import {
  cookGraphPreview,
  prepareGraphForPreviewCook,
  type PreviewData,
} from './previewCook';
import type { GraphJson } from './graphSchema';

interface ReviewState {
  loading: boolean;
  error: string | null;
  data: PreviewData | null;
}

export default function ReviewPage() {
  const [state, setState] = useState<ReviewState>({
    loading: true,
    error: null,
    data: null,
  });

  const graphPath = new URLSearchParams(window.location.search).get('graph');

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

      const graph = (await res.json()) as GraphJson;
      const prepared = prepareGraphForPreviewCook(graph);
      const response = await cookGraphPreview(prepared, 42);

      if (response.ok && response.data) {
        setState({ loading: false, error: null, data: response.data });
      } else {
        setState({ loading: false, error: response.error ?? 'Cook failed', data: null });
      }
    } catch (err) {
      setState({ loading: false, error: String(err), data: null });
    }
  }, [graphPath]);

  useEffect(() => {
    cook();
  }, [cook]);

  // Signal readiness for Playwright screenshot capture
  useEffect(() => {
    if (!state.loading) {
      // Small delay to ensure Three.js has rendered a frame
      const timer = setTimeout(() => {
        (window as unknown as Record<string, unknown>).__pcgReady = true;
      }, 200);
      return () => clearTimeout(timer);
    }
  }, [state.loading]);

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
          </span>
        )}
      </div>
      <div style={{ flex: 1, minHeight: 0 }}>
        <PreviewViewport
          data={state.data}
          loading={state.loading}
          error={state.error}
          onRefresh={cook}
        />
      </div>
    </div>
  );
}
