import type { GraphJson } from './graphSchema';
import { previewParameterDrift } from './previewParameters';

export interface EvaluationEvidence {
  version: 1;
  graph: GraphJson;
  sourceGraph: GraphJson;
  seed: number;
  cook: Record<string, unknown>;
  fullResolution: boolean;
}

/** A successful transport or non-empty mesh does not establish execution quality. */
export function requireTrustworthyCook(json: string, code: number): Record<string, unknown> {
  const result: unknown = JSON.parse(json);
  if (!result || typeof result !== 'object' || Array.isArray(result)) {
    throw new Error('Cook diagnostics are missing; rebuild pcg-server before final review');
  }
  const cook = result as Record<string, unknown>;
  if (code !== 0 || cook.diagnostics_version !== 1 ||
      cook.cook_outcome !== 'success' || cook.execution_acceptable !== true ||
      cook.fallback_used !== false || !Array.isArray(cook.node_diagnostics)) {
    throw new Error('Final review/export blocked: failed, degraded, or unverified cook');
  }
  for (const value of cook.node_diagnostics) {
    if (!value || typeof value !== 'object') throw new Error('Invalid node diagnostic');
    const entry = value as Record<string, unknown>;
    if (entry.fallback_used !== false || !['success', 'noop', 'empty'].includes(String(entry.outcome))) {
      throw new Error(`Final review/export blocked at ${String(entry.node_id)}: ${String(entry.reason)}`);
    }
  }
  return cook;
}

export function requireSynchronizedParameters(graph: GraphJson): void {
  const drift = previewParameterDrift(graph);
  if (drift.length) {
    throw new Error(`Resolve parameter defaults before final review/export (Save defaults or repair the graph): ${drift.join('; ')}`);
  }
}
