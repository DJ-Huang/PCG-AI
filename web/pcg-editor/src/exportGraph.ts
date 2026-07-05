// exportGraph.ts — Convert React Flow nodes/edges/parameters to Graph JSON v1.

import type { Node, Edge } from '@xyflow/react';
import type { GraphJson, GraphNode, GraphEdge, NodeData, GraphParameter } from './graphSchema';

/**
 * Converts React Flow internal nodes/edges to Graph JSON v1 (with parameters).
 */
export function exportGraph(nodes: Node[], edges: Edge[], parameters: GraphParameter[] = []): GraphJson {
  return {
    version: '1.0',
    nodes: nodes.map(toGraphNode),
    edges: edges.map(toGraphEdge),
    parameters,
  };
}

function toGraphNode(node: Node): GraphNode {
  return {
    id: node.id,
    type: node.type ?? 'Unknown',
    position: { x: node.position.x, y: node.position.y },
    data: node.data as unknown as NodeData,
  };
}

function toGraphEdge(edge: Edge): GraphEdge {
  return {
    id: edge.id,
    source: edge.source,
    target: edge.target,
    sourceHandle: edge.sourceHandle ?? undefined,
    targetHandle: edge.targetHandle ?? undefined,
  };
}

/**
 * Triggers a browser download of the graph as a .json file.
 */
export function downloadGraph(graph: GraphJson, filename = 'graph.pcg'): void {
  const blob = new Blob([JSON.stringify(graph, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

/**
 * Writes graph JSON to schema/editor-export.pcg via Vite dev server.
 * Requires `npm run dev` — not available in production build.
 */
export async function exportToSchema(graph: GraphJson): Promise<{ ok: boolean; error?: string }> {
  try {
    const res = await fetch('/api/export-graph', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(graph, null, 2),
    });
    const data = (await res.json()) as { ok?: boolean; error?: string };
    if (!res.ok) {
      return { ok: false, error: data.error ?? res.statusText };
    }
    return { ok: true };
  } catch (err) {
    return { ok: false, error: String(err) };
  }
}

/**
 * Saves graph JSON directly to a file on disk via Vite dev server.
 * Requires `npm run dev` — not available in production build.
 */
export async function saveGraphToFile(
  graph: GraphJson,
  filePath: string,
): Promise<{ ok: boolean; error?: string }> {
  try {
    const res = await fetch('/api/save-graph', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ filePath, graphData: graph }, null, 2),
    });
    const data = (await res.json()) as { ok?: boolean; error?: string };
    if (!res.ok) {
      return { ok: false, error: data.error ?? res.statusText };
    }
    return { ok: true };
  } catch (err) {
    return { ok: false, error: String(err) };
  }
}

/**
 * Reveals a file in Finder via Vite dev server.
 * Requires `npm run dev` — not available in production build.
 */
export async function revealInFinder(filePath: string): Promise<{ ok: boolean; error?: string }> {
  try {
    const res = await fetch('/api/reveal-in-finder', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ filePath }, null, 2),
    });
    const data = (await res.json()) as { ok?: boolean; error?: string };
    if (!res.ok) {
      return { ok: false, error: data.error ?? res.statusText };
    }
    return { ok: true };
  } catch (err) {
    return { ok: false, error: String(err) };
  }
}
