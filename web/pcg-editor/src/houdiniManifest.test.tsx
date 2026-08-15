import { render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import type { Node } from '@xyflow/react';

import Inspector from './Inspector';
import { defaultDataFor, getAllNodeTypes, getNodeTypeDefs } from './nodeManifest';

function renderInspector(type: string) {
  const selectedNode: Node = {
    id: 'subject',
    type,
    position: { x: 0, y: 0 },
    data: defaultDataFor(type),
  };
  return render(
    <Inspector
      selectedNode={selectedNode}
      parameters={[]}
      nodes={[selectedNode]}
      edges={[]}
      onUpdateNodeData={vi.fn()}
      onPromoteParameter={vi.fn()}
      onBindParameter={vi.fn()}
    />,
  );
}

describe('Houdini SOP manifest coverage', () => {
  it('ships every generated native SOP definition', () => {
    const nodes = getAllNodeTypes();
    expect(nodes).toHaveLength(305);
    expect(nodes.filter((node) => node.houdiniInternalNames?.length)).toHaveLength(172);
    for (const type of ['Sphere', 'Torus', 'AttributeCreate', 'UVFlatten', 'Curve']) {
      const definition = getNodeTypeDefs(type);
      expect(definition?.houdiniInternalNames?.length).toBeGreaterThan(0);
      expect(Object.keys(definition?.properties ?? {}).length).toBeGreaterThan(0);
    }
  });

  it('renders sticky parameters and Houdini folders in manifest order', () => {
    const { container } = renderInspector('UVFlatten');
    const firstFoldout = screen.getByText('Existing UVs').closest('details');
    expect(firstFoldout).not.toBeNull();
    expect(within(firstFoldout!).getByText('Preserve Seams')).toBeInTheDocument();
    expect(screen.getByText('Flattening constraints')).toBeInTheDocument();
    expect(screen.getByText('Layout constraints')).toBeInTheDocument();

    const labels = Array.from(container.querySelectorAll('.pcg-inspector__prop-label'))
      .map((element) => element.textContent);
    expect(labels.slice(0, 3)).toEqual(['Group', 'UV Attribute', 'Flattening Method']);
  });

  it('keeps unsectioned Houdini nodes in official parameter order', () => {
    const { container } = renderInspector('Sphere');
    const labels = Array.from(container.querySelectorAll('.pcg-inspector__prop-label'))
      .map((element) => element.textContent);
    expect(labels.slice(0, 6)).toEqual([
      'Primitive Type',
      'Connectivity',
      'Radius',
      'Center',
      'Rotation',
      'Uniform Scale',
    ]);
  });
});
