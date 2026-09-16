// Shader-based infinite ground grid (Blender/Unity SceneView style).
// A camera-following plane avoids precision loss at large coordinates.

import * as THREE from 'three';

export interface InfiniteGridOptions {
  cellSize?: number;
  sectionSize?: number;
  cellColor?: THREE.ColorRepresentation;
  sectionColor?: THREE.ColorRepresentation;
  backgroundColor?: THREE.ColorRepresentation;
  axisXColor?: THREE.ColorRepresentation;
  axisZColor?: THREE.ColorRepresentation;
  fadeDistance?: number;
}

const vertexShader = /* glsl */ `
  varying vec3 vWorldPosition;

  void main() {
    vec4 worldPosition = modelMatrix * vec4(position, 1.0);
    vWorldPosition = worldPosition.xyz;
    gl_Position = projectionMatrix * viewMatrix * worldPosition;
  }
`;

const fragmentShader = /* glsl */ `
  uniform float uCellSize;
  uniform float uSectionSize;
  uniform vec3 uCellColor;
  uniform vec3 uSectionColor;
  uniform vec3 uBackgroundColor;
  uniform vec3 uAxisXColor;
  uniform vec3 uAxisZColor;
  uniform float uFadeDistance;
  uniform vec3 uCameraPosition;

  varying vec3 vWorldPosition;

  float gridLine(vec2 coord, float size) {
    vec2 scaled = coord / size;
    vec2 grid = abs(fract(scaled - 0.5) - 0.5) / fwidth(scaled);
    return 1.0 - min(min(grid.x, grid.y), 1.0);
  }

  float axisLine(float p) {
    float w = fwidth(p);
    return 1.0 - smoothstep(w * 0.5, w * 1.5, abs(p));
  }

  void main() {
    vec2 coord = vWorldPosition.xz;
    float dist = length(coord - uCameraPosition.xz);
    float fade = 1.0 - smoothstep(uFadeDistance * 0.35, uFadeDistance, dist);
    if (fade <= 0.0) discard;

    float cell = gridLine(coord, uCellSize);
    float section = gridLine(coord, uSectionSize);
    float line = max(cell, section);
    float axX = axisLine(coord.y);
    float axZ = axisLine(coord.x);
    if (line < 0.01 && axX < 0.01 && axZ < 0.01) discard;

    vec3 lineColor = mix(uCellColor, uSectionColor, section);
    vec3 color = mix(uBackgroundColor, lineColor, line * fade);
    color = mix(color, uAxisXColor, axX * fade);
    color = mix(color, uAxisZColor, axZ * fade);

    gl_FragColor = vec4(color, 1.0);
  }
`;

// ShaderMaterial output goes to the sRGB canvas unconverted, so keep the
// theme colors as raw sRGB values for an exact Blender viewport match.
function rawColor(hex: THREE.ColorRepresentation): THREE.Color {
  return new THREE.Color().setHex(hex as number, THREE.NoColorSpace);
}

export function createInfiniteGrid(options: InfiniteGridOptions = {}): THREE.Mesh {
  const {
    cellSize = 0.5,
    sectionSize = 5,
    // Blender 4.x dark theme viewport colors (userdef_default_theme.c):
    // background #3d3d3d, grid minor #545454@50%, grid major #545454.
    cellColor = 0x494949,
    sectionColor = 0x545454,
    backgroundColor = 0x3d3d3d,
    // Axis lines: theme axis color blended toward grid at 0.46
    // (X #ff3352 -> #a34553, Y/Z plane axis #8bdc00 -> #6d932d).
    axisXColor = 0xa34553,
    axisZColor = 0x6d932d,
    fadeDistance = 120,
  } = options;

  const material = new THREE.ShaderMaterial({
    uniforms: {
      uCellSize: { value: cellSize },
      uSectionSize: { value: sectionSize },
      uCellColor: { value: rawColor(cellColor) },
      uSectionColor: { value: rawColor(sectionColor) },
      uBackgroundColor: { value: rawColor(backgroundColor) },
      uAxisXColor: { value: rawColor(axisXColor) },
      uAxisZColor: { value: rawColor(axisZColor) },
      uFadeDistance: { value: fadeDistance },
      uCameraPosition: { value: new THREE.Vector3() },
    },
    vertexShader,
    fragmentShader,
    transparent: false,
    toneMapped: false,
    depthWrite: false,
    side: THREE.DoubleSide,
  });

  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), material);
  mesh.rotation.x = -Math.PI / 2;
  mesh.frustumCulled = false;
  mesh.renderOrder = -1;

  mesh.onBeforeRender = (_renderer, _scene, camera) => {
    mesh.position.set(camera.position.x, 0, camera.position.z);
    const scale = fadeDistance * 2;
    mesh.scale.set(scale, scale, 1);
    material.uniforms.uCameraPosition.value.copy(camera.position);
  };

  return mesh;
}
