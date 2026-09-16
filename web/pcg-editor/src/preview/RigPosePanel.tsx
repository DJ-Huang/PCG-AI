import type { ActionBoneInfo, ActionComponentInfo } from '../actionRuntime';

interface RigPosePanelProps {
  bones: readonly ActionBoneInfo[];
  components: readonly ActionComponentInfo[];
  splitComponents: boolean;
  selectedBone: string;
  selectedComponent: string;
  explodeAmount: number;
  onSelectBone(id: string): void;
  onSelectComponent(id: string): void;
  onToggleComponent(id: string, visible: boolean): void;
  onExplodeChange(amount: number): void;
  onResetPose(): void;
  onClose(): void;
}

function componentDepth(component: ActionComponentInfo, all: readonly ActionComponentInfo[]): number {
  const byId = new Map(all.map((candidate) => [candidate.id, candidate]));
  const seen = new Set<string>();
  let depth = 0;
  let parent = component.parent;
  while (parent && !seen.has(parent)) {
    seen.add(parent);
    depth++;
    parent = byId.get(parent)?.parent ?? null;
  }
  return depth;
}

export default function RigPosePanel({
  bones,
  components,
  splitComponents,
  selectedBone,
  selectedComponent,
  explodeAmount,
  onSelectBone,
  onSelectComponent,
  onToggleComponent,
  onExplodeChange,
  onResetPose,
  onClose,
}: RigPosePanelProps) {
  const component = components.find((candidate) => candidate.id === selectedComponent) ?? components[0];
  return (
    <section className="pcg-rig-pose" aria-label="Rig pose mode">
      <div className="pcg-rig-pose__header">
        <span className="pcg-rig-pose__mode">Pose</span>
        <span className="pcg-rig-pose__hint">Select a bone, then rotate in local space</span>
        <button type="button" className="pcg-rig-pose__close" aria-label="Close pose mode" onClick={onClose}>×</button>
      </div>

      <div className="pcg-rig-pose__row">
        <label className="pcg-rig-pose__field pcg-rig-pose__field--bone">
          <span>Bone</span>
          <select aria-label="Pose bone" value={selectedBone} onChange={(event) => onSelectBone(event.target.value)}>
            {bones.map((bone) => <option key={bone.id} value={bone.id}>{bone.id}</option>)}
          </select>
        </label>
        <button type="button" className="pcg-rig-pose__button" onClick={onResetPose}>Reset Pose</button>
      </div>

      {component ? (
        <div className="pcg-rig-pose__row pcg-rig-pose__row--parts">
          <label className="pcg-rig-pose__field pcg-rig-pose__field--part">
            <span>Parts · {components.length}</span>
            <select
              aria-label="Rig component"
              value={component.id}
              onChange={(event) => onSelectComponent(event.target.value)}
            >
              {components.map((candidate) => (
                <option key={candidate.id} value={candidate.id}>
                  {'\u00a0'.repeat(componentDepth(candidate, components) * 2)}{candidate.id} · {candidate.role}
                </option>
              ))}
            </select>
          </label>
          <button
            type="button"
            className={`pcg-rig-pose__button${component.visible ? ' is-active' : ''}`}
            aria-label={`${component.visible ? 'Hide' : 'Show'} component ${component.id}`}
            aria-pressed={component.visible}
            onClick={() => onToggleComponent(component.id, !component.visible)}
          >
            {component.visible ? 'Visible' : 'Hidden'}
          </button>
          <label className="pcg-rig-pose__explode">
            <span>{splitComponents ? 'Explode' : 'Continuous shell'}</span>
            <input
              type="range"
              aria-label="Component explode"
              min={0}
              max={1}
              step={0.01}
              value={explodeAmount}
              disabled={!splitComponents}
              onChange={(event) => onExplodeChange(Number(event.target.value))}
            />
          </label>
        </div>
      ) : (
        <div className="pcg-rig-pose__continuous">Preserved GLB · continuous shell</div>
      )}
    </section>
  );
}
