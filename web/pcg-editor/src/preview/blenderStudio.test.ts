import { describe, expect, it } from 'vitest';

import { createBlenderStudioMaterial } from './blenderStudio';

describe('Blender studio material', () => {
  it('applies Three.js skinning to both positions and normals', () => {
    const material = createBlenderStudioMaterial(false);

    expect(material.vertexShader).toContain('#include <skinning_pars_vertex>');
    expect(material.vertexShader).toContain('#include <skinning_vertex>');
    expect(material.vertexShader).toContain('#include <skinnormal_vertex>');
    expect(material.vertexShader).toContain('vec4(transformed, 1.0)');
    expect(material.vertexShader).toContain('normalize(transformedNormal)');

    material.dispose();
  });
});
