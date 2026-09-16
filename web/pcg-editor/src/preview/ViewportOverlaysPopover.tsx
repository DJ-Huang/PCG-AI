interface ViewportOverlaysPopoverProps {
  wireframeOverlay: boolean;
  onWireframeOverlayChange: (enabled: boolean) => void;
}

export default function ViewportOverlaysPopover({
  wireframeOverlay,
  onWireframeOverlayChange,
}: ViewportOverlaysPopoverProps) {
  return (
    <div
      className="pcg-preview__shading-popover pcg-preview__shading-popover--align-left"
      role="dialog"
      aria-label="Viewport Overlays"
    >
      <div className="pcg-preview__shading-popover-title">Viewport Overlays</div>

      <div className="pcg-preview__shading-section-label">Geometry</div>
      <label className="pcg-preview__overlay-row">
        <input
          type="checkbox"
          checked={wireframeOverlay}
          onChange={(e) => onWireframeOverlayChange(e.target.checked)}
        />
        <span>Wireframe</span>
      </label>
    </div>
  );
}
