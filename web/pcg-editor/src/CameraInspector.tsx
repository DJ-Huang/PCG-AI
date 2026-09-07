import { findShotCamera, findShotMotionCurve, SHOT_OUTPUT_ID, type ShotDocument } from './shot';
import {
  addMotionCurvePoint,
  applyShotCameraPreset,
  removeMotionCurvePoint,
  renameShotCamera,
  renameShotMotionCurve,
  setMotionCurveClosed,
  setMotionCurvePoint,
  upsertShotCamera,
} from './cameraGraph';
import { findCameraPathCurve } from './cameraPath';
import {
  CAMERA_PRESET_CATEGORY_LABELS,
  CAMERA_PRESET_CATEGORIES,
  CAMERA_PRESETS,
  inferCameraPresetId,
} from './cameraPresets';
import { CAMERA_LIMITS, mergeCameraCommand } from './physicalCamera';

interface CameraInspectorProps {
  shot: ShotDocument;
  onShotChange: (shot: ShotDocument) => void;
}

export default function CameraInspector({ shot, onShotChange }: CameraInspectorProps) {
  const selectedId = shot.selectedNodeId || shot.activeCameraId;
  const curve = findShotMotionCurve(shot, selectedId);
  if (curve) {
    return (
      <aside className="pcg-inspector">
        <div className="pcg-inspector__node-type">Motion Curve</div>
        <div className="pcg-inspector__title">{curve.name}</div>
        <div className="pcg-inspector__props">
          <label className="pcg-inspector__field">
            <span>Name</span>
            <input
              value={curve.name}
              onChange={(event) => onShotChange(renameShotMotionCurve(shot, curve.id, event.target.value))}
            />
          </label>
          <div className="pcg-inspector__stat">Id · {curve.id}</div>
          <div className="pcg-inspector__stat">
            Cameras 画布只接线。选中这条曲线后，在左侧 Preview 按 F 对准橙色点，拖点改形状，或在下面改坐标。
          </div>
          <div className="pcg-inspector__stat">
            接到 Camera 的 in 之后，Play 会让相机沿这条路径走。
          </div>
          <label className="pcg-inspector__toggle-row">
            <input
              type="checkbox"
              checked={curve.closed === true}
              onChange={(event) => onShotChange(setMotionCurveClosed(shot, curve.id, event.target.checked))}
            />
            Closed loop
          </label>
          {curve.controlPoints.map((point, pointIndex) => (
            <div key={`${curve.id}-${pointIndex}`} className="pcg-inspector__prop">
              <div className="pcg-inspector__prop-header">
                <span className="pcg-inspector__prop-label">P{pointIndex + 1}</span>
                <button
                  type="button"
                  className="pcg-inspector__promote"
                  disabled={curve.controlPoints.length <= 2}
                  onClick={() => onShotChange(removeMotionCurvePoint(shot, curve.id, pointIndex))}
                >
                  Remove
                </button>
              </div>
              <div className="pcg-inspector__vector3">
                {(['X', 'Y', 'Z'] as const).map((label, axis) => (
                  <label key={label} className="pcg-inspector__vector3-axis">
                    <span>{label}</span>
                    <input
                      type="number"
                      step="0.1"
                      value={point[axis]}
                      onChange={(event) => {
                        const next = Number(event.target.value);
                        const updated: [number, number, number] = [...point];
                        updated[axis] = Number.isFinite(next) ? next : 0;
                        onShotChange(setMotionCurvePoint(shot, curve.id, pointIndex, updated));
                      }}
                    />
                  </label>
                ))}
              </div>
            </div>
          ))}
          <button
            type="button"
            className="pcg-inspector__promote"
            onClick={() => onShotChange(addMotionCurvePoint(shot, curve.id))}
          >
            Add point
          </button>
        </div>
      </aside>
    );
  }

  if (selectedId === SHOT_OUTPUT_ID) {
    return (
      <aside className="pcg-inspector">
        <div className="pcg-inspector__node-type">Shot Output</div>
        <div className="pcg-inspector__title">Look-through / export</div>
      </aside>
    );
  }

  const camera = findShotCamera(shot, selectedId) ?? findShotCamera(shot, shot.activeCameraId);
  if (!camera) {
    return (
      <aside className="pcg-inspector">
        <div className="pcg-inspector__title">Select a camera station</div>
      </aside>
    );
  }

  const presetId = camera.presetId || inferCameraPresetId(camera.camera);
  const path = findCameraPathCurve(shot, camera.id);

  const patchCamera = (patch: Parameters<typeof mergeCameraCommand>[1]) => {
    const nextCamera = mergeCameraCommand(camera.camera, patch);
    onShotChange(upsertShotCamera(shot, {
      id: camera.id,
      camera: nextCamera,
      presetId: inferCameraPresetId(nextCamera),
    }));
  };

  return (
    <aside className="pcg-inspector">
      <div className="pcg-inspector__node-type">Camera</div>
      <div className="pcg-inspector__title">{camera.name}</div>
      <div className="pcg-inspector__props">
        <label className="pcg-inspector__field">
          <span>Name</span>
          <input
            value={camera.name}
            onChange={(event) => onShotChange(renameShotCamera(shot, camera.id, event.target.value))}
          />
        </label>
        <label className="pcg-inspector__field">
          <span>Body</span>
          <select
            value={presetId}
            onChange={(event) => onShotChange(applyShotCameraPreset(shot, camera.id, event.target.value))}
          >
            {CAMERA_PRESET_CATEGORIES.map((category) => (
              <optgroup key={category} label={CAMERA_PRESET_CATEGORY_LABELS[category]}>
                {CAMERA_PRESETS.filter((preset) => preset.category === category).map((preset) => (
                  <option key={preset.id} value={preset.id}>{preset.name}</option>
                ))}
              </optgroup>
            ))}
          </select>
        </label>
        <div className="pcg-inspector__stat">Id · {camera.id}</div>
        <div className="pcg-inspector__stat">
          Path · {path ? path.name : 'none — wire a Motion Curve into this camera'}
        </div>
        <div className="pcg-inspector__stat">
          {camera.cameraKeyframes.length} keyframe{camera.cameraKeyframes.length === 1 ? '' : 's'} · key in Preview
        </div>
        <label className="pcg-inspector__field">
          <span>Sensor width (mm)</span>
          <input
            type="number"
            min={CAMERA_LIMITS.sensorWidthMm.min}
            max={CAMERA_LIMITS.sensorWidthMm.max}
            step={0.01}
            value={camera.camera.sensorWidthMm}
            onChange={(event) => patchCamera({ sensorWidthMm: Number(event.target.value) })}
          />
        </label>
        <label className="pcg-inspector__field">
          <span>Sensor height (mm)</span>
          <input
            type="number"
            min={CAMERA_LIMITS.sensorHeightMm.min}
            max={CAMERA_LIMITS.sensorHeightMm.max}
            step={0.01}
            value={camera.camera.sensorHeightMm}
            onChange={(event) => patchCamera({ sensorHeightMm: Number(event.target.value) })}
          />
        </label>
        <label className="pcg-inspector__field">
          <span>Focal length (mm)</span>
          <input
            type="number"
            min={CAMERA_LIMITS.focalLengthMm.min}
            max={CAMERA_LIMITS.focalLengthMm.max}
            step={1}
            value={camera.camera.focalLengthMm}
            onChange={(event) => patchCamera({ focalLengthMm: Number(event.target.value) })}
          />
        </label>
        <div className="pcg-inspector__stat">
          {Math.round(camera.camera.focalLengthMm)}mm · {camera.camera.projection}
        </div>
      </div>
    </aside>
  );
}
