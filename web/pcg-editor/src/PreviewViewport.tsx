// PreviewViewport.tsx — three.js viewport for pcg-server cook results.
// Renders polygon geometry as mesh / edges / points (Unity SceneView parity),
// spline polylines, and optional draggable control-point handles for
// CreateSpline / CreateBezierSpline authoring nodes.
// Positions arrive in Unity's left-handed Y-up convention; they are mapped to
// three.js right-handed space by negating Z and reversing triangle winding.

import { useEffect, useRef, useState, useCallback } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

import { buildEdgeIndices, type ParsedGeometry, type ParsedMesh, type ParsedSplines } from './cookResult';
import type { PreviewData } from './previewCook';
import {
  beginDrag,
  buildAxisGizmo,
  buildControlPointCloud,
  disposeObject3D,
  dragPoint,
  gizmoLengthForCamera,
  gizmoLineRadiusForCamera,
  pickAxisGizmo,
  pickControlPoint,
  rescaleAxisGizmo,
  unityToThree,
  updateAxisGizmoPosition,
  updateControlPointCloud,
  type DragState,
} from './previewSplineGizmo';
import type { Vec3 } from './splineControlPoints';

export interface SplineEditContext {
  nodeId: string;
  controlPoints: Vec3[];
  closed: boolean;
  onControlPointsChange: (points: Vec3[]) => void;
}

interface PreviewViewportProps {
  data: PreviewData | null;
  loading: boolean;
  error: string | null;
  onRefresh: () => void;
  onClose: () => void;
  splineEdit?: SplineEditContext | null;
}

type DisplayMode = 'mesh' | 'edges' | 'points';

const SPLINE_CURVE_COLOR = 0x4de66a;
const CONTROL_LINE_COLOR = 0xffd933;

const MIN_WIDTH = 320;
const MAX_WIDTH = 900;

function flipZArray(src: Float32Array): Float32Array {
  const out = new Float32Array(src.length);
  for (let i = 0; i < src.length; i += 3) {
    out[i] = src[i];
    out[i + 1] = src[i + 1];
    out[i + 2] = -src[i + 2];
  }
  return out;
}

export default function PreviewViewport({
  data,
  loading,
  error,
  onRefresh,
  onClose,
  splineEdit,
}: PreviewViewportProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<{
    renderer: THREE.WebGLRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    content: THREE.Group;
    handles: THREE.Group;
    gizmoLength: number;
    selectedIndex: number;
  } | null>(null);
  const [modes, setModes] = useState<DisplayMode[]>(['mesh']);
  const [selectedIndex, setSelectedIndex] = useState(-1);
  const [width, setWidth] = useState(420);

  const onResizeStart = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      const startX = e.clientX;
      const startWidth = width;
      const onMove = (ev: MouseEvent) => {
        setWidth(Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, startWidth - (ev.clientX - startX))));
      };
      const onUp = () => {
        window.removeEventListener('mousemove', onMove);
        window.removeEventListener('mouseup', onUp);
        document.body.classList.remove('pcg-preview--resizing');
      };
      document.body.classList.add('pcg-preview--resizing');
      window.addEventListener('mousemove', onMove);
      window.addEventListener('mouseup', onUp);
    },
    [width],
  );
  const dragRef = useRef<DragState | null>(null);
  const splineEditRef = useRef(splineEdit);
  const selectedIndexRef = useRef(selectedIndex);
  splineEditRef.current = splineEdit;
  selectedIndexRef.current = selectedIndex;

  const dataRef = useRef(data);
  dataRef.current = data;

  const hasAutoFramedRef = useRef(false);

  const focusPreview = useCallback(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    const edit = splineEditRef.current;
    const sel = selectedIndexRef.current;

    if (edit && sel >= 0 && sel < edit.controlPoints.length) {
      focusCameraOnTarget(ctx.camera, ctx.controls, unityToThree(edit.controlPoints[sel]));
      return;
    }

    const previewData = dataRef.current;
    if (!previewData) return;
    const positions = collectFitPositions(previewData, edit?.controlPoints);
    if (positions.length > 0) fitCamera(ctx.camera, ctx.controls, positions);
  }, []);

  const syncSplineHandles = useCallback((points: readonly Vec3[], selected: number) => {
    const ctx = sceneRef.current;
    if (!ctx) return;

    disposeObject3D(ctx.handles);
    ctx.handles.clear();
    ctx.selectedIndex = selected;

    if (points.length === 0) return;

    const cloud = buildControlPointCloud(points, selected);
    ctx.handles.add(cloud);

    if (selected >= 0 && selected < points.length) {
      const pos = unityToThree(points[selected]);
      const gizmoLen = gizmoLengthForCamera(
        ctx.camera,
        pos,
        ctx.renderer.domElement.clientHeight,
      );
      const lineRadius = gizmoLineRadiusForCamera(
        ctx.camera,
        pos,
        ctx.renderer.domElement.clientHeight,
      );
      ctx.gizmoLength = gizmoLen;
      const gizmo = buildAxisGizmo(pos, gizmoLen, lineRadius);
      rescaleAxisGizmo(gizmo, gizmoLen);
      ctx.handles.add(gizmo);
    }
  }, []);

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

    scene.add(new THREE.GridHelper(10, 20, 0x3a3a3a, 0x2a2a2a));
    scene.add(new THREE.AxesHelper(0.75));

    const content = new THREE.Group();
    scene.add(content);
    const handles = new THREE.Group();
    scene.add(handles);

    sceneRef.current = {
      renderer,
      scene,
      camera,
      controls,
      content,
      handles,
      gizmoLength: 0.5,
      selectedIndex: -1,
    };

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
      const ctx = sceneRef.current;
      if (ctx) {
        const gizmo = ctx.handles.children.find((c) => c.userData.kind === 'axis-gizmo') as
          | THREE.Group
          | undefined;
        const edit = splineEditRef.current;
        const sel = selectedIndexRef.current;
        if (gizmo && edit && sel >= 0 && sel < edit.controlPoints.length) {
          const pos = unityToThree(edit.controlPoints[sel]);
          const len = gizmoLengthForCamera(
            ctx.camera,
            pos,
            ctx.renderer.domElement.clientHeight,
          );
          ctx.gizmoLength = len;
          rescaleAxisGizmo(gizmo, len);
        }
      }
      renderer.render(scene, camera);
    };
    tick();

    const updateControlPolyline = (group: THREE.Group, points: readonly Vec3[], closed: boolean) => {
      const line = group.children.find((c) => c.userData.kind === 'control-line') as THREE.Line | undefined;
      if (!line) return;
      const count = closed ? points.length + 1 : points.length;
      const positions = new Float32Array(count * 3);
      for (let i = 0; i < points.length; i++) {
        const v = unityToThree(points[i]);
        positions[i * 3] = v.x;
        positions[i * 3 + 1] = v.y;
        positions[i * 3 + 2] = v.z;
      }
      if (closed && points.length > 0) {
        const v = unityToThree(points[0]);
        const o = points.length * 3;
        positions[o] = v.x;
        positions[o + 1] = v.y;
        positions[o + 2] = v.z;
      }
      line.geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
      line.geometry.attributes.position.needsUpdate = true;
      line.computeLineDistances();
    };

    const refreshGizmoVisuals = (points: readonly Vec3[], selected: number) => {
      const ctx = sceneRef.current;
      if (!ctx) return;
      const cloud = ctx.handles.children.find((c) => c.userData.kind === 'control-points') as
        | THREE.Points
        | undefined;
      if (cloud) updateControlPointCloud(cloud, points, selected);
      const gizmo = ctx.handles.children.find((c) => c.userData.kind === 'axis-gizmo') as
        | THREE.Group
        | undefined;
      if (gizmo && selected >= 0 && selected < points.length) {
        updateAxisGizmoPosition(gizmo, unityToThree(points[selected]));
      }
    };

    const onPointerDown = (event: PointerEvent) => {
      const edit = splineEditRef.current;
      const ctx = sceneRef.current;
      if (!edit || !ctx || event.button !== 0) return;

      const rect = ctx.renderer.domElement.getBoundingClientRect();
      const selected = selectedIndexRef.current;

      if (selected >= 0 && selected < edit.controlPoints.length) {
        const gizmoPos = unityToThree(edit.controlPoints[selected]);
        const axisPick = pickAxisGizmo(
          ctx.camera,
          rect,
          event.clientX,
          event.clientY,
          gizmoPos,
          ctx.gizmoLength,
        );
        if (axisPick) {
          event.preventDefault();
          event.stopPropagation();
          dragRef.current = beginDrag(
            selected,
            edit.controlPoints,
            axisPick.mode,
            axisPick.axis,
            axisPick.screenAxis,
            axisPick.worldPerPixel,
            ctx.camera,
            event.clientX,
            event.clientY,
            rect,
            axisPick.planeNormal,
          );
          ctx.controls.enabled = false;
          ctx.renderer.domElement.setPointerCapture(event.pointerId);
          return;
        }
      }

      const pointIndex = pickControlPoint(
        ctx.camera,
        rect,
        event.clientX,
        event.clientY,
        edit.controlPoints,
      );
      if (pointIndex < 0) return;

      event.preventDefault();
      event.stopPropagation();
      setSelectedIndex(pointIndex);
      selectedIndexRef.current = pointIndex;
      syncSplineHandles(edit.controlPoints, pointIndex);

      dragRef.current = beginDrag(
        pointIndex,
        edit.controlPoints,
        'free',
        new THREE.Vector3(),
        new THREE.Vector2(),
        0,
        ctx.camera,
        event.clientX,
        event.clientY,
        rect,
      );
      ctx.controls.enabled = false;
      ctx.renderer.domElement.setPointerCapture(event.pointerId);
    };

    const onPointerMove = (event: PointerEvent) => {
      const drag = dragRef.current;
      const ctx = sceneRef.current;
      const edit = splineEditRef.current;
      if (!drag || !ctx || !edit) return;

      const rect = ctx.renderer.domElement.getBoundingClientRect();
      drag.livePoints[drag.index] = dragPoint(drag, ctx.camera, event.clientX, event.clientY, rect);
      refreshGizmoVisuals(drag.livePoints, drag.index);
      updateControlPolyline(ctx.content, drag.livePoints, edit.closed);
    };

    const onPointerUp = (event: PointerEvent) => {
      const drag = dragRef.current;
      const ctx = sceneRef.current;
      const edit = splineEditRef.current;
      if (!drag || !ctx) return;

      ctx.controls.enabled = true;
      try {
        ctx.renderer.domElement.releasePointerCapture(event.pointerId);
      } catch {
        // ignore
      }
      edit?.onControlPointsChange(drag.livePoints);
      dragRef.current = null;
    };

    const canvas = renderer.domElement;
    const focusContainer = () => container.focus({ preventScroll: true });
    canvas.addEventListener('pointerdown', focusContainer);
    canvas.addEventListener('pointerdown', onPointerDown);
    canvas.addEventListener('pointermove', onPointerMove);
    canvas.addEventListener('pointerup', onPointerUp);
    canvas.addEventListener('pointercancel', onPointerUp);

    return () => {
      cancelAnimationFrame(raf);
      observer.disconnect();
      canvas.removeEventListener('pointerdown', focusContainer);
      canvas.removeEventListener('pointerdown', onPointerDown);
      canvas.removeEventListener('pointermove', onPointerMove);
      canvas.removeEventListener('pointerup', onPointerUp);
      canvas.removeEventListener('pointercancel', onPointerUp);
      controls.dispose();
      disposeGroup(content);
      disposeObject3D(handles);
      handles.clear();
      renderer.dispose();
      renderer.domElement.remove();
      sceneRef.current = null;
    };
  }, [syncSplineHandles, focusPreview]);

  // F — frame selection (or full preview) when the preview panel has focus.
  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key !== 'f' && e.key !== 'F') return;
      const target = e.target as HTMLElement;
      if (target.tagName === 'INPUT' || target.tagName === 'SELECT' || target.tagName === 'TEXTAREA') return;
      if (!container.contains(target) && document.activeElement !== container) return;
      e.preventDefault();
      e.stopPropagation();
      focusPreview();
    };
    container.addEventListener('keydown', onKeyDown);
    return () => container.removeEventListener('keydown', onKeyDown);
  }, [focusPreview]);

  // ── Content rebuild on new cook data ─────────────────
  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    disposeGroup(ctx.content);
    ctx.content.clear();
    if (!data) {
      hasAutoFramedRef.current = false;
      return;
    }

    const geometry = data.geometry;
    const mesh = data.mesh;
    const cloudPositions = geometry ? geometry.positions : mesh ? mesh.positions : data.scatterPoints;
    const hasSolid = cloudPositions != null && cloudPositions.length > 0;

    if (mesh) ctx.content.add(buildMeshObject(mesh, flipZArray));
    if (geometry && geometry.faceCount > 0) {
      const edgeObj = buildEdgeObject(geometry, flipZArray(geometry.positions));
      if (edgeObj) ctx.content.add(edgeObj);
    }
    if (hasSolid) ctx.content.add(buildPointsObject(flipZArray(cloudPositions!)));
    if (data.splines) ctx.content.add(...buildSplineObjects(data.splines));
    if (splineEdit && splineEdit.controlPoints.length > 0) {
      ctx.content.add(buildControlPolyline(splineEdit.controlPoints, splineEdit.closed));
    }

    const fitPositions = collectFitPositions(data, splineEdit?.controlPoints);
    if (fitPositions.length > 0 && !hasAutoFramedRef.current) {
      fitCamera(ctx.camera, ctx.controls, fitPositions);
      hasAutoFramedRef.current = true;
    }

    applyModes(ctx.content, modes, data.splines != null);
  }, [data, splineEdit]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx || !data) return;
    if (!splineEdit || splineEdit.controlPoints.length === 0) {
      disposeObject3D(ctx.handles);
      ctx.handles.clear();
      setSelectedIndex(-1);
      return;
    }
    const sel = selectedIndex >= 0 && selectedIndex < splineEdit.controlPoints.length ? selectedIndex : -1;
    syncSplineHandles(splineEdit.controlPoints, sel);
  }, [splineEdit, data, selectedIndex, syncSplineHandles]);

  useEffect(() => {
    if (!splineEdit) setSelectedIndex(-1);
  }, [splineEdit?.nodeId]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (sceneRef.current) applyModes(sceneRef.current.content, modes, data?.splines != null);
  }, [modes, data?.splines]);

  const toggleMode = (mode: DisplayMode) => {
    setModes((prev) => (prev.includes(mode) ? prev.filter((m) => m !== mode) : [...prev, mode]));
  };

  const geometry = data?.geometry ?? null;
  const splineCount = data?.splines?.splines.length ?? 0;
  const stats = geometry
    ? `${geometry.pointCount} pts · ${geometry.faceCount} faces · ${geometry.triangles.length / 3} tris`
    : data?.scatterPoints
      ? `${data.scatterPoints.length / 3} scatter pts`
      : splineCount > 0
        ? `${splineCount} spline${splineCount === 1 ? '' : 's'}`
        : 'no geometry';

  return (
    <div className="pcg-preview" style={{ width }}>
      <div
        className="pcg-preview__resize-handle"
        onMouseDown={onResizeStart}
        title="Drag to resize"
      />
      <div className="pcg-preview__header">
        {splineEdit && (
          <span
            className="pcg-preview__spline-hint"
            title="Click a control point, then drag center (free) or RGB axes (X/Y/Z)"
          >
            {selectedIndex >= 0 ? 'Drag axis or center' : 'Click control point'}
          </span>
        )}
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
      <div
        ref={containerRef}
        className="pcg-preview__canvas"
        tabIndex={0}
        title="Click to focus · F to frame selection"
        onPointerDown={() => containerRef.current?.focus({ preventScroll: true })}
      />
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

function applyModes(group: THREE.Group, modes: DisplayMode[], hasSplines: boolean) {
  const hasMesh = group.children.some((c) => c.userData.kind === 'mesh');
  for (const child of group.children) {
    const kind = child.userData.kind as string;
    if (kind === 'mesh') child.visible = modes.includes('mesh');
    else if (kind === 'edges') child.visible = modes.includes('edges');
    else if (kind === 'points') child.visible = modes.includes('points') || !hasMesh;
    else if (kind === 'spline' || kind === 'control-line') child.visible = true;
  }
  void hasSplines;
}

function buildMeshObject(mesh: ParsedMesh, flipZ: (src: Float32Array) => Float32Array): THREE.Mesh {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(flipZ(mesh.positions), 3));
  if (mesh.normals) geo.setAttribute('normal', new THREE.BufferAttribute(flipZ(mesh.normals), 3));
  if (mesh.colors && mesh.colors.length === mesh.vertexCount * 4) {
    geo.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 4));
  }
  if (mesh.uvs && mesh.uvs.length === mesh.vertexCount * 2) {
    geo.setAttribute('uv', new THREE.BufferAttribute(mesh.uvs, 2));
  }
  const indices = new Uint32Array(mesh.indices.length);
  for (let i = 0; i < mesh.indices.length; i += 3) {
    indices[i] = mesh.indices[i];
    indices[i + 1] = mesh.indices[i + 2];
    indices[i + 2] = mesh.indices[i + 1];
  }
  geo.setIndex(new THREE.BufferAttribute(indices, 1));
  if (!mesh.normals) geo.computeVertexNormals();

  const meshObj = new THREE.Mesh(
    geo,
    new THREE.MeshStandardMaterial({
      color: mesh.colors ? 0xffffff : 0x9aa4ae,
      vertexColors: mesh.colors != null,
      roughness: 0.85,
      metalness: 0.05,
      side: THREE.DoubleSide,
    }),
  );
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

function buildSplineObjects(splines: ParsedSplines): THREE.Object3D[] {
  const objects: THREE.Object3D[] = [];
  for (const spline of splines.splines) {
    const positions = flipZArray(spline.points);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    const line = new THREE.Line(geo, new THREE.LineBasicMaterial({ color: SPLINE_CURVE_COLOR }));
    line.userData.kind = 'spline';
    objects.push(line);
  }
  return objects;
}

function buildControlPolyline(points: readonly Vec3[], closed: boolean): THREE.Line {
  const positions = new Float32Array((closed ? points.length + 1 : points.length) * 3);
  for (let i = 0; i < points.length; i++) {
    const v = unityToThree(points[i]);
    positions[i * 3] = v.x;
    positions[i * 3 + 1] = v.y;
    positions[i * 3 + 2] = v.z;
  }
  if (closed && points.length > 0) {
    const v = unityToThree(points[0]);
    const o = points.length * 3;
    positions[o] = v.x;
    positions[o + 1] = v.y;
    positions[o + 2] = v.z;
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  const line = new THREE.Line(
    geo,
    new THREE.LineDashedMaterial({ color: CONTROL_LINE_COLOR, dashSize: 0.4, gapSize: 0.2 }),
  );
  line.computeLineDistances();
  line.userData.kind = 'control-line';
  return line;
}

function collectFitPositions(data: PreviewData, controlPoints?: readonly Vec3[]): Float32Array {
  const chunks: number[] = [];
  const push = (arr: Float32Array) => {
    for (let i = 0; i < arr.length; i++) chunks.push(arr[i]);
  };
  if (data.geometry) push(flipZArray(data.geometry.positions));
  else if (data.mesh) push(flipZArray(data.mesh.positions));
  else if (data.scatterPoints) push(flipZArray(data.scatterPoints));
  if (data.splines) for (const s of data.splines.splines) push(flipZArray(s.points));
  if (controlPoints) {
    for (const p of controlPoints) {
      const v = unityToThree(p);
      chunks.push(v.x, v.y, v.z);
    }
  }
  return new Float32Array(chunks);
}

function focusCameraOnTarget(
  camera: THREE.PerspectiveCamera,
  controls: OrbitControls,
  target: THREE.Vector3,
  radius = 0.015,
) {
  controls.target.copy(target);
  const offset = camera.position.clone().sub(controls.target);
  if (offset.lengthSq() < 1e-8) offset.set(0.7, 0.6, 0.7);
  offset.normalize().multiplyScalar(Math.max(radius * 8, 0.08));
  camera.position.copy(target).add(offset);
  camera.near = Math.max(offset.length() / 500, 0.0001);
  camera.far = Math.max(offset.length() * 200, 10);
  camera.updateProjectionMatrix();
  controls.update();
}

function fitCamera(
  camera: THREE.PerspectiveCamera,
  controls: OrbitControls,
  positions: Float32Array,
  margin = 1.35,
) {
  if (positions.length < 3) return;

  const box = new THREE.Box3();
  const point = new THREE.Vector3();
  for (let i = 0; i < positions.length; i += 3) {
    point.set(positions[i], positions[i + 1], positions[i + 2]);
    box.expandByPoint(point);
  }
  if (box.isEmpty()) return;

  const center = new THREE.Vector3();
  const sphere = new THREE.Sphere();
  box.getCenter(center);
  box.getBoundingSphere(sphere);
  const radius = Math.max(sphere.radius, 0.001);

  const fovRad = (camera.fov * Math.PI) / 180;
  const aspect = Math.max(camera.aspect, 0.01);
  const halfFovTan = Math.tan(fovRad * 0.5);

  // Perspective fit: ensure the AABB bounding sphere fits in both vertical and
  // horizontal FOV, then add margin for the angled view direction.
  const fitVert = radius / halfFovTan;
  const fitHorz = radius / (halfFovTan * aspect);
  const distance = Math.max(fitVert, fitHorz) * margin;

  const viewDir = new THREE.Vector3(0.7, 0.6, 0.7).normalize();
  camera.position.copy(center).addScaledVector(viewDir, distance);
  controls.target.copy(center);

  camera.near = Math.max(distance / 1000, 0.0001);
  camera.far = Math.max(distance * 100, radius * 20, 10);
  camera.updateProjectionMatrix();
  controls.update();
}

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    if (
      child instanceof THREE.Mesh ||
      child instanceof THREE.Points ||
      child instanceof THREE.LineSegments ||
      child instanceof THREE.Line
    ) {
      child.geometry.dispose();
      const material = child.material as THREE.Material | THREE.Material[];
      if (Array.isArray(material)) material.forEach((m) => m.dispose());
      else material.dispose();
    }
  }
}
