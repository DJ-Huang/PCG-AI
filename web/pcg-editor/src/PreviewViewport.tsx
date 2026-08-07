// PreviewViewport.tsx — three.js viewport for pcg-server cook results.
// Renders polygon geometry as mesh / edges / points (Unity SceneView parity),
// or a scatter point cloud when the graph outputs points only.
// Positions arrive in Unity's left-handed Y-up convention; they are mapped to
// three.js right-handed space by negating Z and reversing triangle winding.

import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

import { buildEdgeIndices, type ParsedGeometry, type ParsedMesh } from './cookResult';
import type { PreviewData } from './previewCook';

interface PreviewViewportProps {
  data: PreviewData | null;
  loading: boolean;
  error: string | null;
  onRefresh: () => void;
  onClose: () => void;
}

type DisplayMode = 'mesh' | 'edges' | 'points';

export default function PreviewViewport({ data, loading, error, onRefresh, onClose }: PreviewViewportProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<{
    renderer: THREE.WebGLRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    content: THREE.Group;
  } | null>(null);
  const [modes, setModes] = useState<DisplayMode[]>(['mesh']);

  // ── Scene lifecycle ──────────────────────────────────
  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setClearColor(0x1e1e1e);
    container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(50, 1, 0.01, 5000);
    camera.position.set(3, 2.5, 4);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;

    scene.add(new THREE.HemisphereLight(0xffffff, 0x333a44, 1.1));
    const dir = new THREE.DirectionalLight(0xffffff, 1.4);
    dir.position.set(4, 8, 5);
    scene.add(dir);

    const grid = new THREE.GridHelper(10, 20, 0x3a3a3a, 0x2a2a2a);
    scene.add(grid);
    scene.add(new THREE.AxesHelper(0.75));

    const content = new THREE.Group();
    scene.add(content);

    sceneRef.current = { renderer, scene, camera, controls, content };

    const resize = () => {
      const w = container.clientWidth;
      const h = container.clientHeight;
      if (w === 0 || h === 0) return;
      renderer.setSize(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    };
    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(container);

    let raf = 0;
    const tick = () => {
      raf = requestAnimationFrame(tick);
      controls.update();
      renderer.render(scene, camera);
    };
    tick();

    return () => {
      cancelAnimationFrame(raf);
      observer.disconnect();
      controls.dispose();
      disposeGroup(content);
      renderer.dispose();
      renderer.domElement.remove();
      sceneRef.current = null;
    };
  }, []);

  // ── Content rebuild on new cook data ─────────────────
  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    disposeGroup(ctx.content);
    ctx.content.clear();
    if (!data) return;

    const geometry = data.geometry;
    const mesh = data.mesh;
    const cloudPositions = geometry ? geometry.positions : mesh ? mesh.positions : data.scatterPoints;
    if (!cloudPositions || cloudPositions.length === 0) return;

    // Unity (LH, Y-up) → three.js (RH, Y-up): negate Z.
    const flipZ = (src: Float32Array): Float32Array => {
      const out = new Float32Array(src.length);
      for (let i = 0; i < src.length; i += 3) {
        out[i] = src[i];
        out[i + 1] = src[i + 1];
        out[i + 2] = -src[i + 2];
      }
      return out;
    };

    if (mesh) {
      ctx.content.add(buildMeshObject(mesh, flipZ));
    }
    if (geometry && geometry.faceCount > 0) {
      const edgeObj = buildEdgeObject(geometry, flipZ(geometry.positions));
      if (edgeObj) ctx.content.add(edgeObj);
    }
    ctx.content.add(buildPointsObject(flipZ(cloudPositions)));

    fitCamera(ctx.camera, ctx.controls, flipZ(cloudPositions));
    applyModes(ctx.content, modes);
  }, [data]); // eslint-disable-line react-hooks/exhaustive-deps

  // ── Mode toggles ─────────────────────────────────────
  useEffect(() => {
    if (sceneRef.current) applyModes(sceneRef.current.content, modes);
  }, [modes]);

  const toggleMode = (mode: DisplayMode) => {
    setModes((prev) =>
      prev.includes(mode)
        ? prev.filter((m) => m !== mode)
        : [...prev, mode],
    );
  };

  const geometry = data?.geometry ?? null;
  const stats = geometry
    ? `${geometry.pointCount} pts · ${geometry.faceCount} faces · ${geometry.triangles.length / 3} tris`
    : data?.scatterPoints
      ? `${data.scatterPoints.length / 3} scatter pts`
      : 'no geometry';

  return (
    <div className="pcg-preview">
      <div className="pcg-preview__header">
        <span>Preview</span>
        <label className="pcg-preview__mode">
          <input type="checkbox" checked={modes.includes('mesh')} onChange={() => toggleMode('mesh')} />
          Mesh
        </label>
        <label className="pcg-preview__mode">
          <input type="checkbox" checked={modes.includes('edges')} onChange={() => toggleMode('edges')} />
          Edges
        </label>
        <label className="pcg-preview__mode">
          <input type="checkbox" checked={modes.includes('points')} onChange={() => toggleMode('points')} />
          Points
        </label>
        <button type="button" className="pcg-preview__btn" onClick={onRefresh} disabled={loading}>
          {loading ? 'Cooking…' : 'Re-cook'}
        </button>
        <button type="button" className="pcg-preview__btn pcg-preview__close" onClick={onClose} title="Close preview">
          ×
        </button>
      </div>
      <div ref={containerRef} className="pcg-preview__canvas" />
      <div className="pcg-preview__footer">
        <span className="pcg-preview__stats">{stats}</span>
        {data && (
          <span className="pcg-preview__perf">
            {data.cook.graphExecuteMs.toFixed(1)} ms · {data.cook.nodesExecuted} nodes
          </span>
        )}
      </div>
      {error && <div className="pcg-preview__error">{error}</div>}
    </div>
  );
}

function applyModes(group: THREE.Group, modes: DisplayMode[]) {
  const hasMesh = group.children.some((c) => c.userData.kind === 'mesh');
  for (const child of group.children) {
    if (child.userData.kind === 'mesh') child.visible = modes.includes('mesh');
    else if (child.userData.kind === 'edges') child.visible = modes.includes('edges');
    else if (child.userData.kind === 'points') {
      // Scatter-only results have no mesh — always show points then.
      child.visible = modes.includes('points') || !hasMesh;
    }
  }
}

function buildMeshObject(mesh: ParsedMesh, flipZ: (src: Float32Array) => Float32Array): THREE.Mesh {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(flipZ(mesh.positions), 3));
  if (mesh.normals) {
    geo.setAttribute('normal', new THREE.BufferAttribute(flipZ(mesh.normals), 3));
  }
  if (mesh.colors && mesh.colors.length === mesh.vertexCount * 4) {
    geo.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 4));
  }
  if (mesh.uvs && mesh.uvs.length === mesh.vertexCount * 2) {
    geo.setAttribute('uv', new THREE.BufferAttribute(mesh.uvs, 2));
  }
  // Z was negated — reverse winding to keep front faces correct.
  const indices = new Uint32Array(mesh.indices.length);
  for (let i = 0; i < mesh.indices.length; i += 3) {
    indices[i] = mesh.indices[i];
    indices[i + 1] = mesh.indices[i + 2];
    indices[i + 2] = mesh.indices[i + 1];
  }
  geo.setIndex(new THREE.BufferAttribute(indices, 1));
  if (!mesh.normals) {
    geo.computeVertexNormals();
  }

  const material = new THREE.MeshStandardMaterial({
    color: mesh.colors ? 0xffffff : 0x9aa4ae,
    vertexColors: mesh.colors != null,
    roughness: 0.85,
    metalness: 0.05,
    side: THREE.DoubleSide,
  });
  const meshObj = new THREE.Mesh(geo, material);
  meshObj.userData.kind = 'mesh';
  return meshObj;
}

function buildEdgeObject(geometry: ParsedGeometry, positions: Float32Array): THREE.LineSegments | null {
  const edges = buildEdgeIndices(geometry);
  if (edges.length === 0) return null;
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geo.setIndex(new THREE.BufferAttribute(edges, 1));
  const lines = new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color: 0x54c8bc }));
  lines.userData.kind = 'edges';
  return lines;
}

function buildPointsObject(positions: Float32Array): THREE.Points {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  const points = new THREE.Points(
    geo,
    new THREE.PointsMaterial({ color: 0xffc857, size: 3, sizeAttenuation: false }),
  );
  points.userData.kind = 'points';
  return points;
}

function fitCamera(camera: THREE.PerspectiveCamera, controls: OrbitControls, positions: Float32Array) {
  const bounds = new THREE.BufferGeometry();
  bounds.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  bounds.computeBoundingSphere();
  const sphere = bounds.boundingSphere;
  bounds.dispose();
  if (!sphere) return;
  const radius = Math.max(sphere.radius, 0.001);
  controls.target.copy(sphere.center);
  const distance = radius * 2.2;
  camera.position.set(
    sphere.center.x + distance * 0.7,
    sphere.center.y + distance * 0.6,
    sphere.center.z + distance * 0.7,
  );
  camera.near = Math.max(distance / 1000, 0.001);
  camera.far = distance * 100;
  camera.updateProjectionMatrix();
  controls.update();
}

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    if (child instanceof THREE.Mesh || child instanceof THREE.Points || child instanceof THREE.LineSegments) {
      child.geometry.dispose();
      const material = child.material as THREE.Material | THREE.Material[];
      if (Array.isArray(material)) material.forEach((m) => m.dispose());
      else material.dispose();
    }
  }
}
