import { findShotCamera, findShotMotionCurve, SHOT_OUTPUT_ID, type ShotDocument } from './shot';
import {
  addMotionCurvePoint,
  connectShotCameras,
  applyShotCameraPreset,
  removeMotionCurvePoint,
  renameShotCamera,
  renameShotMotionCurve,
  setMotionCurveClosed,
  setMotionCurvePoint,
} from './cameraGraph';
import { findCameraPathCurve, sampleShotCameraWorld } from './cameraPath';
import { findCameraTransform, setCameraTransform } from './cameraTransform';
import { editCameraAtTime } from './cameraEditing';
import { editableCameraKeyframes, upsertCameraKeyframe } from './cameraTrack';
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
  time: number;
  autoKey: boolean;
  selectedCurvePoint: number;
  onSelectCurvePoint: (index: number) => void;
}

export default function CameraInspector({ shot, onShotChange, time, autoKey, selectedCurvePoint, onSelectCurvePoint }: CameraInspectorProps) {
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
            在 Scene 视图点击路径点，拖动 XYZ 控制轴；F 对准路径。下方 Animation 编辑时间和镜头属性。
          </div>
          <div className="pcg-inspector__stat">
            接到 Camera 的 in 之后，Play 会让相机沿这条路径走。
          </div>
          <label className="pcg-inspector__field"><span>Connect to camera</span>
            <select aria-label="Motion Curve camera" value={shot.cameraEdges.find((edge) => edge.source === curve.id)?.target ?? ''}
              onChange={(event) => onShotChange(connectShotCameras(shot, curve.id, event.target.value))}>
              <option value="">Choose camera…</option>
              {shot.cameras.map((camera) => <option key={camera.id} value={camera.id}>{camera.name}</option>)}
            </select>
          </label>
          <label className="pcg-inspector__field"><span>Look direction</span>
            <select aria-label="Motion Curve look direction" value={curve.lookMode ?? 'tangent'} onChange={(event) => onShotChange({ ...shot,
              motionCurves: shot.motionCurves.map((entry) => entry.id === curve.id ? { ...entry, lookMode: event.target.value as 'tangent' | 'target' } : entry) })}>
              <option value="tangent">Follow path</option><option value="target">Look at target</option>
            </select>
          </label>
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
                <button aria-label={`Select path point ${pointIndex + 1}`} aria-pressed={selectedCurvePoint === pointIndex} onClick={() => onSelectCurvePoint(pointIndex)}>P{pointIndex + 1}</button>
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
        <div className="pcg-inspector__props">
          <label className="pcg-inspector__field"><span>Shot name</span><input aria-label="Shot name" value={shot.name} onChange={(event) => onShotChange({ ...shot, name: event.target.value })} /></label>
          <label className="pcg-inspector__field"><span>Delivery format</span>
            <select aria-label="Shot delivery format" value={`${shot.width}x${shot.height}`} onChange={(event) => {
              const [width, height] = event.target.value.split('x').map(Number); onShotChange({ ...shot, width, height });
            }}>
              <option value={`${shot.width}x${shot.height}`}>Current · {shot.width} × {shot.height}</option>
              <option value="1920x1080">HD · 16:9</option><option value="3840x2160">UHD · 16:9</option>
              <option value="1920x804">Cinema · 2.39:1</option><option value="1080x1920">Portrait · 9:16</option>
              <option value="1080x1080">Square · 1:1</option>
            </select>
          </label>
          <p>Camera view 按输出画幅显示构图。Guides 显示三分线和 90% 安全框；导出不含辅助线。</p>
        </div>
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
  const sampled = sampleShotCameraWorld(shot, camera.id, time);
  const rig = findCameraTransform(shot, camera.id);
  const patchCamera = (patch: Parameters<typeof mergeCameraCommand>[1]) => onShotChange(editCameraAtTime(shot, patch, time, autoKey));
  const numericFields = [
    ['focalLengthMm', 'Focal length (mm)'], ['focusDistance', 'Focus distance (m)'], ['apertureFstop', 'Aperture (f-stop)'],
    ['exposure', 'Exposure'], ['sensorWidthMm', 'Sensor width (mm)'], ['sensorHeightMm', 'Sensor height (mm)'],
    ['shiftX', 'Lens shift X'], ['shiftY', 'Lens shift Y'], ['near', 'Near clip'], ['far', 'Far clip'],
    ['orthographicScale', 'Orthographic scale'], ['apertureBlades', 'Aperture blades'],
    ['apertureRotationDeg', 'Aperture rotation'], ['apertureRatio', 'Aperture ratio'],
  ] as const;

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
          {editableCameraKeyframes(shot).length} keyframe{editableCameraKeyframes(shot).length === 1 ? '' : 's'} · key in Preview
        </div>
        <div className="pcg-inspector__stat">Frame {Math.round(time * shot.fps)} · {autoKey ? 'Auto key on' : 'Rest pose / selected key'}</div>
        {(['position', 'target'] as const).map((field) => <div key={field} className="pcg-inspector__prop">
          <div className="pcg-inspector__prop-header">{field === 'position' ? 'Position (world)' : 'Look at (world)'}</div>
          <div className="pcg-inspector__vector3">{['X', 'Y', 'Z'].map((axis, index) => <label key={axis} className="pcg-inspector__vector3-axis">
            <span>{axis}</span><input type="number" step="0.1" aria-label={`Camera ${field} ${axis}`} value={Number(sampled[field][index].toFixed(4))}
              onChange={(event) => { const value: [number, number, number] = [...sampled[field]]; value[index] = Number(event.target.value); patchCamera({ [field]: value }); }} />
          </label>)}</div>
        </div>)}
        {rig && <div className="pcg-inspector__prop">
          <div className="pcg-inspector__prop-header">Transform · rig offset</div>
          {(['translation', 'rotationEulerDeg'] as const).map((field) => <div key={field}>
            <span>{field === 'translation' ? 'Translation (m)' : 'Rotation (degrees)'}</span>
            <div className="pcg-inspector__vector3">{['X', 'Y', 'Z'].map((axis, index) => <label key={axis} className="pcg-inspector__vector3-axis">
              <span>{axis}</span><input type="number" step="0.1" aria-label={`Transform ${field} ${axis}`} value={Number(rig[field][index].toFixed(4))}
                onChange={(event) => { const value: [number, number, number] = [...rig[field]]; value[index] = Number(event.target.value); onShotChange(setCameraTransform(shot, camera.id, { [field]: value })); }} />
            </label>)}</div>
          </div>)}
        </div>}
        {numericFields.map(([field, label]) => <label key={field} className="pcg-inspector__field picg-camera-property">
          <span>{label}</span>
          <input type="number" step={field === 'apertureBlades' ? 1 : 0.1} aria-label={label} value={Number(sampled[field].toFixed(4))}
            min={field === 'shiftX' || field === 'shiftY' ? CAMERA_LIMITS.shift.min : field === 'apertureRotationDeg' ? -180 : 0}
            onChange={(event) => patchCamera({ [field]: Number(event.target.value) })} />
          <button title={`Key ${label} at frame ${Math.round(time * shot.fps)}`} aria-label={`Key ${label}`} onClick={(event) => {
            event.preventDefault(); onShotChange(upsertCameraKeyframe(shot, { timeSeconds: time, interpolation: 'ease-in-out', value: { [field]: sampled[field] } }));
          }}>◇</button>
        </label>)}
        <label className="pcg-inspector__field"><span>Projection</span>
          <select aria-label="Camera projection" value={sampled.projection} onChange={(event) => patchCamera({ projection: event.target.value as 'perspective' | 'orthographic' })}>
            <option value="perspective">Perspective</option><option value="orthographic">Orthographic</option>
          </select>
        </label>
        <label className="pcg-inspector__toggle-row"><input type="checkbox" checked={sampled.dofEnabled} onChange={(event) => patchCamera({ dofEnabled: event.target.checked })} />Depth of field</label>
        <button onClick={() => patchCamera({ focusOnTarget: true })}>Focus on target</button>
      </div>
    </aside>
  );
}
