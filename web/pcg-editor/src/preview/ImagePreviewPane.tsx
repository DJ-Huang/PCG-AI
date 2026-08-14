import { useCallback, useRef, useState } from 'react';
import type { PreviewImage } from '../previewCook';

interface ImagePreviewPaneProps {
  image: PreviewImage;
}

interface ViewTransform {
  scale: number;
  x: number;
  y: number;
}

const INITIAL_TRANSFORM: ViewTransform = { scale: 1, x: 0, y: 0 };

/** 2D image view for Texture-output previews: wheel zoom around cursor, drag pan, double-click reset. */
export default function ImagePreviewPane({ image }: ImagePreviewPaneProps) {
  const [transform, setTransform] = useState<ViewTransform>(INITIAL_TRANSFORM);
  const [naturalSize, setNaturalSize] = useState<{ width: number; height: number } | null>(null);
  const dragRef = useRef<{ pointerId: number; startX: number; startY: number; base: ViewTransform } | null>(null);

  const onWheel = useCallback((event: React.WheelEvent<HTMLDivElement>) => {
    event.preventDefault();
    const rect = event.currentTarget.getBoundingClientRect();
    const cursorX = event.clientX - rect.left - rect.width / 2;
    const cursorY = event.clientY - rect.top - rect.height / 2;
    setTransform((prev) => {
      const nextScale = Math.min(64, Math.max(0.05, prev.scale * (event.deltaY < 0 ? 1.1 : 1 / 1.1)));
      const ratio = nextScale / prev.scale;
      return {
        scale: nextScale,
        x: cursorX - (cursorX - prev.x) * ratio,
        y: cursorY - (cursorY - prev.y) * ratio,
      };
    });
  }, []);

  const onPointerDown = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
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

  if (!image.url) {
    return (
      <div className="pcg-preview__image-pane pcg-preview__image-pane--empty">
        <span>Node "{image.nodeId}" has no image source — assign a texture on the node first.</span>
      </div>
    );
  }

  return (
    <div
      className="pcg-preview__image-pane"
      onWheel={onWheel}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onDoubleClick={() => setTransform(INITIAL_TRANSFORM)}
      title="Scroll zoom · drag pan · double-click reset"
    >
      <img
        className="pcg-preview__image"
        src={image.url}
        alt={image.storage}
        draggable={false}
        onLoad={(e) => setNaturalSize({
          width: e.currentTarget.naturalWidth,
          height: e.currentTarget.naturalHeight,
        })}
        style={{
          transform: `translate(calc(-50% + ${transform.x}px), calc(-50% + ${transform.y}px)) scale(${transform.scale})`,
        }}
      />
      <div className="pcg-preview__image-info">
        <span className="pcg-preview__image-name">{image.storage}</span>
        {naturalSize && <span>{naturalSize.width}×{naturalSize.height}</span>}
        {(image.repeatX !== 1 || image.repeatY !== 1) && (
          <span>repeat {image.repeatX}×{image.repeatY}</span>
        )}
        <span>{Math.round(transform.scale * 100)}%</span>
      </div>
    </div>
  );
}
