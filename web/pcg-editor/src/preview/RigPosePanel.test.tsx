import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import RigPosePanel from './RigPosePanel';

describe('RigPosePanel', () => {
  it('exposes bone posing and real component controls', () => {
    const onSelectBone = vi.fn();
    const onToggleComponent = vi.fn();
    const onExplodeChange = vi.fn();
    const onResetPose = vi.fn();
    render(
      <RigPosePanel
        bones={[
          { id: 'body', name: 'body', parent: null, tailOffset: [0, 1, 0], radius: 0.4 },
          { id: 'head', name: 'head', parent: 'body', tailOffset: [0, 0.5, 0], radius: 0.3 },
        ]}
        components={[
          {
            id: 'body', parent: null, bone: 'body', role: 'structural', skin: 'smooth',
            detachable: false, visible: true, triangleCount: 120,
          },
          {
            id: 'head', parent: 'body', bone: 'head', role: 'structural', skin: 'smooth',
            detachable: true, visible: true, triangleCount: 80,
          },
        ]}
        splitComponents
        selectedBone="body"
        selectedComponent="head"
        explodeAmount={0}
        onSelectBone={onSelectBone}
        onSelectComponent={vi.fn()}
        onToggleComponent={onToggleComponent}
        onExplodeChange={onExplodeChange}
        onResetPose={onResetPose}
        onClose={vi.fn()}
      />,
    );

    expect(screen.getByText('Parts · 2')).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Pose bone'), { target: { value: 'head' } });
    fireEvent.click(screen.getByRole('button', { name: 'Hide component head' }));
    fireEvent.change(screen.getByLabelText('Component explode'), { target: { value: '0.35' } });
    fireEvent.click(screen.getByRole('button', { name: 'Reset Pose' }));

    expect(onSelectBone).toHaveBeenCalledWith('head');
    expect(onToggleComponent).toHaveBeenCalledWith('head', false);
    expect(onExplodeChange).toHaveBeenCalledWith(0.35);
    expect(onResetPose).toHaveBeenCalledOnce();
  });
});
