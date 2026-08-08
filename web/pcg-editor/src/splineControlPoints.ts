// splineControlPoints.ts — Parse/serialize CreateSpline controlPoints JSON.
// Mirrors Unity PcgSplineControlPoints.cs.

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

const SPLINE_AUTHORING_TYPES = new Set(['CreateSpline', 'CreateBezierSpline']);

export function isSplineAuthoringNode(nodeType: string | undefined): boolean {
  return nodeType != null && SPLINE_AUTHORING_TYPES.has(nodeType);
}

export function parseControlPoints(raw: unknown): Vec3[] {
  if (raw == null) return [];
  const text = typeof raw === 'string' ? raw : JSON.stringify(raw);
  if (!text.trim()) return [];
  try {
    const list = JSON.parse(text) as unknown;
    if (!Array.isArray(list)) return [];
    const points: Vec3[] = [];
    for (const item of list) {
      if (!item || typeof item !== 'object') continue;
      const rec = item as Record<string, unknown>;
      points.push({
        x: Number(rec.x) || 0,
        y: Number(rec.y) || 0,
        z: Number(rec.z) || 0,
      });
    }
    return points;
  } catch {
    return [];
  }
}

export function serializeControlPoints(points: readonly Vec3[]): string {
  if (points.length === 0) return '[]';
  return (
    '[' +
    points
      .map((p) => `{"x":${p.x},"y":${p.y},"z":${p.z}}`)
      .join(',') +
    ']'
  );
}

export function getEffectiveControlPoints(data: Record<string, unknown> | undefined): Vec3[] {
  if (!data) return [];
  const explicit = parseControlPoints(data.controlPoints);
  if (explicit.length > 0) return explicit;
  return [
    {
      x: Number(data.startX) || 0,
      y: Number(data.startY) || 0,
      z: Number(data.startZ) || 0,
    },
    {
      x: Number(data.endX) || 0,
      y: Number(data.endY) || 0,
      z: Number(data.endZ) || 0,
    },
  ];
}
