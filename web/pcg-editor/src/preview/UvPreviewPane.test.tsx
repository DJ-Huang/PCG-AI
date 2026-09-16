import { render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import UvPreviewPane from './UvPreviewPane';
import type { ParsedMesh } from '../cookResult';

class ResizeObserverStub {
  observe() {}
  unobserve() {}
  disconnect() {}
}
vi.stubGlobal('ResizeObserver', ResizeObserverStub);

function makeMesh(overrides: Partial<ParsedMesh> = {}): ParsedMesh {
  return {
    positions: new Float32Array(9),
    indices: new Uint32Array([0, 1, 2]),
    normals: null,
    colors: null,
    uvs: new Float32Array([0, 0, 1, 0, 0, 1]),
    materialSlots: [],
    triangleMaterials: null,
    vertexCount: 3,
    indexCount: 3,
    ...overrides,
  };
}

describe('UvPreviewPane', () => {
  it('shows an empty state when the cook produced no mesh', () => {
    render(<UvPreviewPane mesh={null} />);
    expect(screen.getByText(/no mesh/)).toBeInTheDocument();
  });

  it('shows an empty state when the mesh has no UVs', () => {
    render(<UvPreviewPane mesh={makeMesh({ uvs: null })} />);
    expect(screen.getByText(/no UV coordinates/)).toBeInTheDocument();
  });

  it('renders the uv pane with stats when UVs are present', () => {
    const { container } = render(<UvPreviewPane mesh={makeMesh()} />);
    expect(container.querySelector('.pcg-preview__uv-pane')).not.toBeNull();
    expect(container.querySelector('canvas')).not.toBeNull();
    expect(screen.getByText(/3 uv verts · 1 tris/)).toBeInTheDocument();
    expect(screen.getByText(/u 0\.00–1\.00 · v 0\.00–1\.00/)).toBeInTheDocument();
  });
});
