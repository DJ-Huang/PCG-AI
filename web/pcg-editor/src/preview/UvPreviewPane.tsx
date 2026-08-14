import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { ParsedMesh } from '../cookResult';

interface UvPreviewPaneProps {
  mesh: ParsedMesh | null;
}

interface ViewTransform {
  /** Screen px per UV unit. */
  scale: number;
  /** Screen x of u = 0. */
  x: number;
  /** Screen y of v = 0 (v grows upward, screen y downward). */
  y: number;
}

interface UvBounds {
  minU: number;
  minV: number;
  maxU: number;
  maxV: number;
}

const FIT_PADDING = 24;

function fitTransform(size: { width: number; height: number }, bounds: UvBounds): ViewTransform {
  const w = Math.max(1, size.width - FIT_PADDING * 2);
  const h = Math.max(1, size.height - FIT_PADDING * 2);
  const du = Math.max(bounds.maxU - bounds.minU, 1e-6);
  const dv = Math.max(bounds.maxV - bounds.minV, 1e-6);
  const scale = Math.min(w / du, h / dv);
  return {
    scale,
    x: size.width / 2 - ((bounds.minU + bounds.maxU) / 2) * scale,
    y: size.height / 2 + ((bounds.minV + bounds.maxV) / 2) * scale,
  };
}

/** 2D UV layout view for mesh previews: wheel zoom around cursor, drag pan, double-click fit. */
export default function UvPreviewPane({ mesh }: UvPreviewPaneProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState<{ width: number; height: number } | null>(null);
  const [transform, setTransform] = useState<ViewTransform | null>(null);
  const dragRef = useRef<{ pointerId: number; startX: number; startY: number; base: ViewTransform } | null>(null);

  const uvs = mesh?.uvs ?? null;

  const bounds = useMemo<UvBounds | null>(() => {
    if (!uvs || uvs.length < 2) return null;
    let minU = Infinity;
    let minV = Infinity;
    let maxU = -Infinity;
    let maxV = -Infinity;
    for (let i = 0; i + 1 < uvs.length; i += 2) {
      const u = uvs[i];
      const v = uvs[i + 1];
      if (u < minU) minU = u;
      if (u > maxU) maxU = u;
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }
    return { minU, minV, maxU, maxV };
  }, [uvs]);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const observer = new ResizeObserver((entries) => {
      const rect = entries[0]?.contentRect;
      if (rect) setSize({ width: rect.width, height: rect.height });
    });
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  // Fit once on first display; re-cooks and resizes keep the current pan/zoom
  // (double-click resets to null → refit).
  useEffect(() => {
    if (transform || !size || !bounds) return;
    setTransform(fitTransform(size, bounds));
  }, [transform, size, bounds]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !size || !transform) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.round(size.width * dpr));
    canvas.height = Math.max(1, Math.round(size.height * dpr));
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size.width, size.height);

    const toX = (u: number) => transform.x + u * transform.scale;
    const toY = (v: number) => transform.y - v * transform.scale;

    ctx.lineWidth = 1;
    ctx.strokeStyle = '#2c2d30';
    ctx.beginPath();
    const firstU = Math.floor((0 - transform.x) / transform.scale);
    const lastU = Math.ceil((size.width - transform.x) / transform.scale);
    for (let u = firstU; u <= lastU; u++) {
      const sx = Math.round(toX(u)) + 0.5;
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, size.height);
    }
    const firstV = Math.floor((transform.y - size.height) / transform.scale);
    const lastV = Math.ceil(transform.y / transform.scale);
    for (let v = firstV; v <= lastV; v++) {
      const sy = Math.round(toY(v)) + 0.5;
      ctx.moveTo(0, sy);
      ctx.lineTo(size.width, sy);
    }
    ctx.stroke();

    ctx.strokeStyle = '#4a4d54';
    ctx.strokeRect(toX(0), toY(1), transform.scale, transform.scale);

    if (uvs && mesh?.indices) {
      const vertexCount = uvs.length / 2;
      const indices = mesh.indices;
      ctx.strokeStyle = '#7fb4ff';
      ctx.beginPath();
      for (let i = 0; i + 2 < indices.length; i += 3) {
        for (let e = 0; e < 3; e++) {
          const a = indices[i + e];
          const b = indices[i + ((e + 1) % 3)];
          if (a >= vertexCount || b >= vertexCount) continue;
          ctx.moveTo(toX(uvs[a * 2]), toY(uvs[a * 2 + 1]));
          ctx.lineTo(toX(uvs[b * 2]), toY(uvs[b * 2 + 1]));
        }
      }
      ctx.stroke();
    }
  }, [size, transform, uvs, mesh?.indices]);

  const onWheel = useCallback((event: React.WheelEvent<HTMLDivElement>) => {
    event.preventDefault();
    const rect = event.currentTarget.getBoundingClientRect();
    const cursorX = event.clientX - rect.left;
    const cursorY = event.clientY - rect.top;
    setTransform((prev) => {
      if (!prev) return prev;
      const nextScale = Math.min(1e9, Math.max(1e-3, prev.scale * (event.deltaY < 0 ? 1.1 : 1 / 1.1)));
      const ratio = nextScale / prev.scale;
      return {
        scale: nextScale,
        x: cursorX - (cursorX - prev.x) * ratio,
        y: cursorY - (cursorY - prev.y) * ratio,
      };
    });
  }, []);

  const onPointerDown = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    if (!transform) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    dragRef.current = {
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      base: transform,
    };
  }, [transform]);

  const onPointerMove = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) return;
    setTransform({
      scale: drag.base.scale,
      x: drag.base.x + event.clientX - drag.startX,
      y: drag.base.y + event.clientY - drag.startY,
    });
  }, []);

  const onPointerUp = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    if (dragRef.current?.pointerId === event.pointerId) dragRef.current = null;
  }, []);

  if (!mesh) {
    return (
      <div className="pcg-preview__image-pane pcg-preview__image-pane--empty">
        <span>Cook produced no mesh — nothing to show UVs for.</span>
      </div>
    );
  }
  if (!uvs || !bounds) {
    return (
      <div className="pcg-preview__image-pane pcg-preview__image-pane--empty">
        <span>Mesh "{mesh.vertexCount} verts" has no UV coordinates — add a UV node to the graph.</span>
      </div>
    );
  }

  const fitScale = size ? fitTransform(size, bounds).scale : null;

  return (
    <div
      ref={containerRef}
      className="pcg-preview__uv-pane"
      onWheel={onWheel}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onDoubleClick={() => setTransform(null)}
      title="Scroll zoom · drag pan · double-click fit"
    >
      <canvas ref={canvasRef} className="pcg-preview__uv-canvas" />
      <div className="pcg-preview__image-info">
        <span>{mesh.vertexCount} uv verts · {mesh.indexCount / 3} tris</span>
        <span>
          u {bounds.minU.toFixed(2)}–{bounds.maxU.toFixed(2)} · v {bounds.minV.toFixed(2)}–{bounds.maxV.toFixed(2)}
        </span>
        {transform && fitScale && <span>{Math.max(1, Math.round((transform.scale / fitScale) * 100))}%</span>}
      </div>
    </div>
  );
}
