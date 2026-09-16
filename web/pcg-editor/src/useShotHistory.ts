import { useCallback, useEffect, useRef, useState } from 'react';
import { syncShotCameras, type ShotDocument } from './shot';

function content(shot: ShotDocument): string {
  const { activeCameraId: _active, selectedNodeId: _selected, camera: _camera, cameraKeyframes: _keys, ...document } = syncShotCameras(shot);
  return JSON.stringify(document);
}

/** Selection is view state. A pointer gesture creates at most one undo entry. */
export function useShotHistory(initial: ShotDocument) {
  const [shot, setShot] = useState(initial);
  const current = useRef(shot);
  const history = useRef<{ past: ShotDocument[]; future: ShotDocument[] }>({ past: [], future: [] });
  const gesture = useRef({ active: false, saved: false });
  useEffect(() => {
    const begin = () => { gesture.current = { active: true, saved: false }; };
    const end = () => { gesture.current.active = false; };
    window.addEventListener('pointerdown', begin, true);
    window.addEventListener('pointerup', end);
    window.addEventListener('pointercancel', end);
    window.addEventListener('blur', end);
    return () => {
      window.removeEventListener('pointerdown', begin, true);
      window.removeEventListener('pointerup', end);
      window.removeEventListener('pointercancel', end);
      window.removeEventListener('blur', end);
    };
  }, []);
  const apply = useCallback((next: ShotDocument) => {
    const synced = syncShotCameras(next);
    if (content(current.current) !== content(synced)) {
      if (!gesture.current.active || !gesture.current.saved) {
        history.current.past = [...history.current.past.slice(-99), current.current];
        gesture.current.saved = true;
      }
      history.current.future = [];
    }
    current.current = synced;
    setShot(synced);
    return synced;
  }, []);
  const travel = useCallback((direction: 'past' | 'future') => {
    const stack = history.current[direction];
    const next = stack.pop();
    if (!next) return;
    history.current[direction === 'past' ? 'future' : 'past'].push(current.current);
    current.current = next;
    setShot(next);
  }, []);
  const reset = useCallback((next: ShotDocument) => {
    history.current = { past: [], future: [] };
    current.current = syncShotCameras(next);
    setShot(current.current);
  }, []);
  return { shot, apply, reset, undo: () => travel('past'), redo: () => travel('future'),
    canUndo: history.current.past.length > 0, canRedo: history.current.future.length > 0 };
}
