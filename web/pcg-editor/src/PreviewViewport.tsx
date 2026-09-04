// PreviewViewport.tsx — three.js viewport for pcg-server cook results.
// Renders polygon geometry as mesh / edges / points (Unity SceneView parity),
// spline polylines, and optional draggable control-point handles for
// CreateSpline / CreateBezierSpline authoring nodes.
// Positions arrive in Unity's left-handed Y-up convention; they are mapped to
// three.js right-handed space by negating Z and reversing triangle winding.

import { forwardRef, useEffect, useImperativeHandle, useRef, useState, useCallback } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { EXRLoader } from 'three/addons/loaders/EXRLoader.js';
import { HDRLoader } from 'three/addons/loaders/HDRLoader.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { BokehPass } from 'three/addons/postprocessing/BokehPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';

import { buildEdgeIndices, type ParsedGeometry, type ParsedMesh, type ParsedSplines, type SourceMapping } from './cookResult';
import type { GraphParameter } from './graphSchema';
import type { PreviewData } from './previewCook';
import type { PreviewParameterValue, PreviewParameterValues } from './previewParameters';
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
import SolidShadingPopover, {
  DEFAULT_MATCAP_ID,
  type SolidLighting,
} from './preview/SolidShadingPopover';
import ViewportOverlaysPopover from './preview/ViewportOverlaysPopover';
import MaterialEnvironmentPopover from './preview/MaterialEnvironmentPopover';
import {
  DEFAULT_BUILTIN_ENVIRONMENT,
  EXTERNAL_ENVIRONMENT_ID,
  getBuiltinEnvironment,
} from './preview/builtinEnvironments';
import { createInfiniteGrid } from './preview/infiniteGrid';
import { createBlenderStudioMaterial } from './preview/blenderStudio';
import {
  createStandardPbrMaterial,
  createPbrDebugMaterial,
  disposePbrTextureCache,
  normalizePbrMaterial,
  type PbrDebugView,
  type PbrMaterialLibrary,
} from './preview/pbrMaterials';
import {
  disposeMatcapLibrary,
  loadMatcapTexture,
  preloadMatcap,
  type MatcapId,
} from './preview/matcapLibrary';
import type { Vec3 } from './splineControlPoints';
import PreviewParametersPopover from './preview/PreviewParametersPopover';
import ImagePreviewPane from './preview/ImagePreviewPane';
import UvPreviewPane from './preview/UvPreviewPane';
import {
  applyReviewCameraPose,
  computeReviewCameraPose,
  type FrontAxis,
  type ReviewCameraPose,
  type ReviewCameraView,
  type SideView,
} from './reviewCamera';
import {
  apertureToBokehUniform,
  defaultPhysicalCamera,
  focalLengthToFov,
  mergeCameraCommand,
  syncStateFromLiveCamera,
  type CameraCommand,
  type PhysicalCameraState,
} from './physicalCamera';
import CameraPopover from './preview/CameraPopover';
import AnimationTransport from './preview/AnimationTransport';
import { computeMeshBandProfile, type MeshBandProfile } from './meshBandProfile';
import {
  buildActionRigObject,
  type ActionAnimationClipInfo,
  type ActionPlaybackState,
  type ActionRuntimeController,
  type ActionRuntimeDiagnostics,
} from './actionRuntime';
import { loadPreservedGltfRig } from './preservedGltfRuntime';

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
  parameters?: GraphParameter[];
  parameterNodeIds?: ReadonlySet<string>;
  parameterValues?: PreviewParameterValues;
  onParameterValueChange?: (parameterId: string, value: PreviewParameterValue) => void;
  onResetParameters?: () => void;
  onSaveParameterDefaults?: () => void;
  splineEdit?: SplineEditContext | null;
  /** Click on a preview mesh component → attributed graph node id (may be a
   *  subgraph path like "n5/n12"). Only fires for cooks with source mapping. */
  onPickNode?: (nodeId: string) => void;
  /** Graph-selected node id; triangles attributed to it via source mapping get
   *  an edge highlight overlay. A root-scope subgraph instance id also matches
   *  its flattened "instance/inner" entries. */
  selectedNodeId?: string | null;
  /** Fill the parent and hide editor chrome. Used by the /review route. */
  reviewMode?: boolean;
  /** Initial viewport shading. The clean review route uses material evidence by default. */
  initialShadingMode?: ShadingMode;
  /** Open the viewport's animation workspace as soon as an animated result is available. */
  initialAnimationModeOpen?: boolean;
  /** Deterministic camera applied after each cook on the review page. */
  reviewCamera?: {
    view: ReviewCameraView;
    frontAxis?: FrontAxis;
    sideView?: SideView;
  } | null;
  onReviewCameraApplied?: (pose: ReviewCameraPose) => void;
}

export interface CaptureOptions {
  /** Output pixels; independent of the on-screen viewport size. */
  width?: number;
  height?: number;
  /** Alpha background (PNG). Overrides the environment background. */
  transparent?: boolean;
  /** Camera override applied to the session camera before rendering. */
  camera?: CameraCommand;
  /** Override the session DOF switch for this capture. */
  dof?: boolean;
  /** Temporary depth-transparent solid pass for layout/section review captures. */
  xray?: boolean;
  /** Temporary shading override; the interactive viewport mode is restored afterwards. */
  shadingMode?: ShadingMode;
  /** Deterministic analysis output. Beauty preserves PBR; all other passes hide editor helpers. */
  renderPass?: CaptureRenderPass;
}

const CAPTURE_RENDER_PASSES = [
  'beauty',
  'alpha-silhouette',
  'semantic-id',
  'depth',
  'normal',
  'roughness-material-id',
] as const;

export type CaptureRenderPass = (typeof CAPTURE_RENDER_PASSES)[number];

export interface CaptureDiagnosticLegend {
  depthRange?: [number, number];
  semanticIds?: Array<{ id: number; sourceNode: string; color: string }>;
  materialIds?: Array<{
    id: number;
    slot: string;
    color: string;
    roughness: number;
    metallic: number;
  }>;
}

export interface PreviewCapture {
  pngBase64: string;
  metadata: {
    camera: {
      position: [number, number, number];
      target: [number, number, number];
      up: [number, number, number];
      fov: number;
      near: number;
      far: number;
      projection: 'perspective' | 'orthographic';
      view?: ReviewCameraView;
    };
    physicalCamera: PhysicalCameraState;
    viewport: { width: number; height: number; pixelRatio: number };
    shadingMode: ShadingMode;
    solidLighting: SolidLighting;
    matcapId: MatcapId;
    wireframeOverlay: boolean;
    xrayEnabled: boolean;
    reviewPose?: ReviewCameraPose | null;
    /** Ground-aligned, height-normalized mesh proportions for reference/candidate comparison. */
    meshBandProfile?: MeshBandProfile | null;
    renderPass?: CaptureRenderPass;
    diagnosticLegend?: CaptureDiagnosticLegend | null;
  };
}

export interface PreviewViewportHandle {
  captureFrame(options?: CaptureOptions): PreviewCapture | null;
  setReviewCamera(view: ReviewCameraView, options?: {
    frontAxis?: FrontAxis;
    sideView?: SideView;
  }): ReviewCameraPose | null;
  /** Merge a camera command into the session camera and apply it. */
  applyCameraCommand(command: CameraCommand): PhysicalCameraState | null;
  getCameraState(): PhysicalCameraState | null;
  listAnimations(): string[];
  listAnimationClips(): ActionAnimationClipInfo[];
  listComponents(): string[];
  playAnimation(name: string): boolean;
  pauseAnimation(): void;
  resumeAnimation(): boolean;
  stopAnimation(): void;
  seekAnimation(name: string, timeSeconds: number): boolean;
  setAnimationSpeed(speed: number): void;
  setAnimationLoop(loop: boolean): void;
  getAnimationPlaybackState(): ActionPlaybackState | null;
  setExplode(amount: number): void;
  inspectActionRuntime(): ActionRuntimeDiagnostics | null;
  /** Exact source GLB bytes when PreserveGltfRig is active. */
  getPreservedGltfBytes(): ArrayBuffer | null;
}

export type ShadingMode = 'solid' | 'material' | 'rendered';

const XRAY_OPACITY = 0.35;

type ViewAxis = 'x' | 'y' | 'z';

const AXIS_VIEW_DIRECTIONS: Record<ViewAxis, THREE.Vector3> = {
  x: new THREE.Vector3(1, 0, 0),
  y: new THREE.Vector3(0, 1, 0),
  z: new THREE.Vector3(0, 0, 1),
};

const AXIS_VIEW_UPS: Record<ViewAxis, THREE.Vector3> = {
  x: new THREE.Vector3(0, 1, 0),
  y: new THREE.Vector3(0, 0, 1),
  z: new THREE.Vector3(0, 1, 0),
};

interface AxisNavigationPoint {
  x: number;
  y: number;
  scale: number;
  zIndex: number;
}

type AxisNavigation = Record<ViewAxis, AxisNavigationPoint>;

const AXIS_NAVIGATION_CENTER = 42;
const AXIS_NAVIGATION_RADIUS = 24;

function getAxisNavigation(camera: THREE.PerspectiveCamera): AxisNavigation {
  camera.updateMatrixWorld();
  const inverseCameraRotation = camera.quaternion.clone().invert();
  const pointFor = (axis: ViewAxis): AxisNavigationPoint => {
    const direction = AXIS_VIEW_DIRECTIONS[axis].clone().applyQuaternion(inverseCameraRotation);
    return {
      x: AXIS_NAVIGATION_CENTER + direction.x * AXIS_NAVIGATION_RADIUS,
      y: AXIS_NAVIGATION_CENTER - direction.y * AXIS_NAVIGATION_RADIUS,
      scale: 0.74 + (direction.z + 1) * 0.18,
      zIndex: Math.round((direction.z + 1) * 100),
    };
  };
  return { x: pointFor('x'), y: pointFor('y'), z: pointFor('z') };
}

const SPLINE_CURVE_COLOR = 0x4de66a;
const CONTROL_LINE_COLOR = 0xffd933;
/** Blender default theme wire color (userdef_default_theme.c, dark theme). */
const EDGE_OVERLAY_COLOR = 0x1a1a1a;
/** Screen-space px; Blender overlay uses ~1px core + AA expansion in pack_line_data. */
const EDGE_OVERLAY_LINE_WIDTH = 2.5;
/** Houdini viewport selection orange. */
const SELECTION_HIGHLIGHT_COLOR = 0xff9d2e;
const SELECTION_HIGHLIGHT_LINE_WIDTH = 3;

const MIN_WIDTH = 320;
const MAX_WIDTH = 2400;

const defaultWidth = () =>
  Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, Math.round((window.innerWidth * 2) / 3)));
const EMPTY_PARAMETERS: GraphParameter[] = [];
const EMPTY_PARAMETER_NODE_IDS = new Set<string>();
const EMPTY_PARAMETER_VALUES: PreviewParameterValues = {};
const NOOP_PARAMETER_CHANGE = () => {};
const EMPTY_ACTION_PLAYBACK: ActionPlaybackState = {
  clipName: null,
  currentTime: 0,
  duration: 0,
  playing: false,
  paused: false,
  loop: false,
  loopSource: 'unmeasured',
  speed: 1,
};

function samePlaybackState(a: ActionPlaybackState, b: ActionPlaybackState): boolean {
  return a.clipName === b.clipName &&
    Math.abs(a.currentTime - b.currentTime) < 1e-4 &&
    Math.abs(a.duration - b.duration) < 1e-4 &&
    a.playing === b.playing &&
    a.paused === b.paused &&
    a.loop === b.loop &&
    a.loopSource === b.loopSource &&
    Math.abs(a.speed - b.speed) < 1e-4;
}

function flipZArray(src: Float32Array): Float32Array {
  const out = new Float32Array(src.length);
  for (let i = 0; i < src.length; i += 3) {
    out[i] = src[i];
    out[i + 1] = src[i + 1];
    out[i + 2] = -src[i + 2];
  }
  return out;
}

const PreviewViewport = forwardRef<PreviewViewportHandle, PreviewViewportProps>(function PreviewViewport({
  data,
  loading,
  error,
  onRefresh,
  parameters = EMPTY_PARAMETERS,
  parameterNodeIds = EMPTY_PARAMETER_NODE_IDS,
  parameterValues = EMPTY_PARAMETER_VALUES,
  onParameterValueChange = NOOP_PARAMETER_CHANGE,
  onResetParameters = NOOP_PARAMETER_CHANGE,
  onSaveParameterDefaults = NOOP_PARAMETER_CHANGE,
  splineEdit,
  onPickNode,
  selectedNodeId = null,
  reviewMode = false,
  initialShadingMode = 'solid',
  initialAnimationModeOpen = false,
  reviewCamera = null,
  onReviewCameraApplied,
}: PreviewViewportProps, ref) {
  const containerRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<{
    renderer: THREE.WebGLRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
    perspective: THREE.PerspectiveCamera;
    orthographic: THREE.OrthographicCamera;
    controls: OrbitControls;
    content: THREE.Group;
    handles: THREE.Group;
    helpers: THREE.Group;
    gizmoLength: number;
    selectedIndex: number;
    envMap: THREE.Texture | null;
    backgroundMap: THREE.Texture | null;
    keyLight: THREE.DirectionalLight;
    matcapTexture: THREE.Texture | null;
    pmremGenerator: THREE.PMREMGenerator | null;
    reviewPose: ReviewCameraPose | null;
  } | null>(null);
  const [shadingMode, setShadingMode] = useState<ShadingMode>(initialShadingMode);
  const [solidLighting, setSolidLighting] = useState<SolidLighting>('studio');
  const [matcapId, setMatcapId] = useState<MatcapId>(DEFAULT_MATCAP_ID);
  const [wireframeOverlay, setWireframeOverlay] = useState(false);
  const [xrayEnabled, setXrayEnabled] = useState(false);
  const [overlayPopoverOpen, setOverlayPopoverOpen] = useState(false);
  const [solidPopoverOpen, setSolidPopoverOpen] = useState(false);
  const [parametersPopoverOpen, setParametersPopoverOpen] = useState(false);
  const [environmentId, setEnvironmentId] = useState(DEFAULT_BUILTIN_ENVIRONMENT.id);
  const environmentLoadVersionRef = useRef(0);
  const invalidateEnvironmentLoads = useCallback(() => {
    environmentLoadVersionRef.current++;
  }, []);
  const [environmentRotation, setEnvironmentRotation] = useState(0);
  const environmentRotationRef = useRef(environmentRotation);
  environmentRotationRef.current = environmentRotation;
  const [environmentIntensity, setEnvironmentIntensity] = useState(1);
  const [exposure, setExposure] = useState(1);
  const [keyLightIntensity, setKeyLightIntensity] = useState(1.15);
  const [backgroundVisible, setBackgroundVisible] = useState(false);
  const [pbrDebugView, setPbrDebugView] = useState<PbrDebugView>('lit');
  const [axisNavigation, setAxisNavigation] = useState<AxisNavigation>(() => ({
    x: { x: 63, y: 42, scale: 0.92, zIndex: 50 },
    y: { x: 42, y: 20, scale: 0.92, zIndex: 50 },
    z: { x: 28, y: 56, scale: 0.86, zIndex: 35 },
  }));
  const backgroundVisibleRef = useRef(backgroundVisible);
  backgroundVisibleRef.current = backgroundVisible;
  const shadingRef = useRef({ shadingMode, solidLighting, wireframeOverlay, xrayEnabled, matcapId, pbrDebugView });
  shadingRef.current = { shadingMode, solidLighting, wireframeOverlay, xrayEnabled, matcapId, pbrDebugView };
  const overlayPopoverRef = useRef<HTMLDivElement>(null);
  const solidPopoverRef = useRef<HTMLDivElement>(null);
  const cameraPopoverRef = useRef<HTMLDivElement>(null);
  const [cameraPopoverOpen, setCameraPopoverOpen] = useState(false);
  // Session-scoped physical camera. Never persisted into the .pcg document.
  const physicalCameraRef = useRef<PhysicalCameraState>(defaultPhysicalCamera());
  const [cameraUiState, setCameraUiState] = useState<PhysicalCameraState>(defaultPhysicalCamera());
  const composerRef = useRef<{
    composer: EffectComposer;
    renderPass: RenderPass;
    bokehPass: BokehPass;
    outputPass: OutputPass;
  } | null>(null);
  const [selectedIndex, setSelectedIndex] = useState(-1);
  const [width, setWidth] = useState(defaultWidth);
  const [sceneMs, setSceneMs] = useState(0);
  const [animationModeOpen, setAnimationModeOpen] = useState(initialAnimationModeOpen);
  const [animationClips, setAnimationClips] = useState<ActionAnimationClipInfo[]>([]);
  const [selectedAnimation, setSelectedAnimation] = useState('');
  const [animationPlayback, setAnimationPlayback] = useState<ActionPlaybackState>(EMPTY_ACTION_PLAYBACK);
  const sceneTimedDataRef = useRef<PreviewData | null>(null);
  const pickDownRef = useRef<{ x: number; y: number; consumed: boolean } | null>(null);
  const onPickNodeRef = useRef(onPickNode);
  onPickNodeRef.current = onPickNode;
  const sourceMappingRef = useRef<SourceMapping | null>(null);
  sourceMappingRef.current = data?.sourceMapping ?? null;

  const updateAnimationPlayback = useCallback((next: ActionPlaybackState) => {
    setAnimationPlayback((current) => samePlaybackState(current, next) ? current : next);
  }, []);

  const syncAnimationRuntime = useCallback((controller: ActionRuntimeController | null) => {
    const clips = controller?.clips ?? [];
    setAnimationClips(clips);
    setSelectedAnimation((selected) => {
      if (clips.some((clip) => clip.name === selected)) return selected;
      return controller?.currentAnimation ?? clips[0]?.name ?? '';
    });
    updateAnimationPlayback(controller?.getPlaybackState() ?? EMPTY_ACTION_PLAYBACK);
  }, [updateAnimationPlayback]);

  const onResizeStart = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      const startX = e.clientX;
      const startWidth = width;
      const onMove = (ev: MouseEvent) => {
        setWidth(Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, startWidth + (ev.clientX - startX))));
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
  const reviewCameraRef = useRef(reviewCamera);
  reviewCameraRef.current = reviewCamera;
  const onReviewCameraAppliedRef = useRef(onReviewCameraApplied);
  onReviewCameraAppliedRef.current = onReviewCameraApplied;
  const reviewModeRef = useRef(reviewMode);
  reviewModeRef.current = reviewMode;

  const hasAutoFramedRef = useRef(false);

  const applyActiveReviewCamera = useCallback((view?: ReviewCameraView, options?: {
    frontAxis?: FrontAxis;
    sideView?: SideView;
  }): ReviewCameraPose | null => {
    const ctx = sceneRef.current;
    const previewData = dataRef.current;
    if (!ctx || !previewData) return null;
    const requested = view
      ? {
          view,
          frontAxis: options?.frontAxis ?? reviewCameraRef.current?.frontAxis,
          sideView: options?.sideView ?? reviewCameraRef.current?.sideView,
        }
      : reviewCameraRef.current;
    if (!requested) return null;
    const aspect = Math.max(
      ctx.renderer.domElement.clientWidth / Math.max(ctx.renderer.domElement.clientHeight, 1),
      0.01,
    );
    const positions = collectFitPositions(previewData, splineEditRef.current?.controlPoints);
    const pose = computeReviewCameraPose(positions, requested, aspect);
    if (!pose) return null;
    const nextCamera = pose.projection === 'orthographic' ? ctx.orthographic : ctx.perspective;
    applyReviewCameraPose(nextCamera, pose, aspect);
    ctx.camera = nextCamera;
    ctx.controls.object = nextCamera;
    ctx.controls.target.set(pose.target[0], pose.target[1], pose.target[2]);
    ctx.controls.update();
    ctx.reviewPose = pose;
    ctx.helpers.visible = !reviewModeRef.current;
    if (nextCamera instanceof THREE.PerspectiveCamera) {
      setAxisNavigation(getAxisNavigation(nextCamera));
    }
    onReviewCameraAppliedRef.current?.(pose);
    return pose;
  }, []);

  const ensureComposer = useCallback((ctx: NonNullable<typeof sceneRef.current>) => {
    if (!composerRef.current) {
      const composer = new EffectComposer(ctx.renderer);
      const renderPass = new RenderPass(ctx.scene, ctx.camera);
      const bokehPass = new BokehPass(ctx.scene, ctx.camera, {
        focus: 1, aperture: 0, maxblur: 0.01,
      });
      const outputPass = new OutputPass();
      composer.addPass(renderPass);
      composer.addPass(bokehPass);
      composer.addPass(outputPass);
      composerRef.current = { composer, renderPass, bokehPass, outputPass };
    }
    return composerRef.current;
  }, []);

  const renderWithDof = useCallback((
    ctx: NonNullable<typeof sceneRef.current>,
    state: PhysicalCameraState,
  ) => {
    const pipeline = ensureComposer(ctx);
    const camera = ctx.camera;
    pipeline.renderPass.camera = camera;
    pipeline.bokehPass.camera = camera;
    const aspect = camera instanceof THREE.PerspectiveCamera
      ? camera.aspect
      : Math.max(ctx.renderer.domElement.width / Math.max(ctx.renderer.domElement.height, 1), 0.01);
    const uniforms = pipeline.bokehPass.uniforms as Record<string, { value: number }>;
    uniforms.focus.value = state.focusDistance;
    uniforms.aperture.value = apertureToBokehUniform(
      state.focalLengthMm,
      state.apertureFstop,
    );
    uniforms.aspect.value = aspect;
    pipeline.composer.render();
  }, [ensureComposer]);

  const applyCameraStateToScene = useCallback((
    ctx: NonNullable<typeof sceneRef.current>,
    state: PhysicalCameraState,
  ) => {
    const nextCamera = state.projection === 'orthographic' ? ctx.orthographic : ctx.perspective;
    nextCamera.up.set(state.up[0], state.up[1], state.up[2]);
    nextCamera.position.set(state.position[0], state.position[1], state.position[2]);
    nextCamera.near = state.near;
    nextCamera.far = state.far;
    const aspect = Math.max(
      ctx.renderer.domElement.clientWidth / Math.max(ctx.renderer.domElement.clientHeight, 1),
      0.01,
    );
    if (nextCamera instanceof THREE.PerspectiveCamera) {
      nextCamera.fov = focalLengthToFov(state.focalLengthMm, state.sensorHeightMm);
      nextCamera.aspect = aspect;
    } else {
      // Explicit semantic framing wins; otherwise retain the legacy lens-derived size.
      const distance = Math.max(
        nextCamera.position.distanceTo(new THREE.Vector3(...state.target)),
        0.001,
      );
      const fov = THREE.MathUtils.degToRad(focalLengthToFov(state.focalLengthMm, state.sensorHeightMm));
      const halfH = state.orthographicFrustumHeight
        ? state.orthographicFrustumHeight / 2
        : distance * Math.tan(fov / 2);
      nextCamera.top = halfH;
      nextCamera.bottom = -halfH;
      nextCamera.left = -halfH * aspect;
      nextCamera.right = halfH * aspect;
      nextCamera.userData.frustumHeight = halfH * 2;
    }
    nextCamera.updateProjectionMatrix();
    ctx.camera = nextCamera;
    ctx.controls.object = nextCamera;
    ctx.controls.target.set(state.target[0], state.target[1], state.target[2]);
    ctx.controls.update();
    ctx.renderer.toneMappingExposure = state.exposure;
    ctx.reviewPose = null;
    if (nextCamera instanceof THREE.PerspectiveCamera) {
      setAxisNavigation(getAxisNavigation(nextCamera));
    }
  }, []);

  const applyCameraCommand = useCallback((command: CameraCommand): PhysicalCameraState | null => {
    const ctx = sceneRef.current;
    if (!ctx) return null;
    const synced = syncStateFromLiveCamera(physicalCameraRef.current, ctx.camera, ctx.controls.target);
    const next = mergeCameraCommand(synced, command);
    physicalCameraRef.current = next;
    setCameraUiState(next);
    setExposure(next.exposure);
    applyCameraStateToScene(ctx, next);
    return next;
  }, [applyCameraStateToScene]);

  const getCameraState = useCallback((): PhysicalCameraState | null => {
    const ctx = sceneRef.current;
    if (!ctx) return null;
    const synced = syncStateFromLiveCamera(physicalCameraRef.current, ctx.camera, ctx.controls.target);
    synced.exposure = ctx.renderer.toneMappingExposure;
    physicalCameraRef.current = synced;
    return synced;
  }, []);

  const captureFrameImpl = useCallback((options?: CaptureOptions): PreviewCapture | null => {
    const ctx = sceneRef.current;
    if (!ctx || ctx.renderer.domElement.width === 0 || ctx.renderer.domElement.height === 0) {
      return null;
    }
      if (options?.camera) applyCameraCommand(options.camera);

      const dofActive = (options?.dof ?? physicalCameraRef.current.dofEnabled) &&
        ctx.camera instanceof THREE.PerspectiveCamera;
      const forcedXray = options?.xray === true;
      const currentShading = shadingRef.current;
      const captureShadingMode = options?.shadingMode ?? currentShading.shadingMode;
      const renderPass = options?.renderPass;
      const captureXray = captureShadingMode === 'solid'
        ? (forcedXray || currentShading.xrayEnabled)
        : false;
      const hasTemporaryShading = (renderPass !== undefined && renderPass !== 'beauty') ||
        captureShadingMode !== currentShading.shadingMode ||
        captureXray !== currentShading.xrayEnabled;
      const clampSize = (value: number | undefined) =>
        value === undefined ? null : Math.min(8192, Math.max(16, Math.round(value)));
      const captureWidth = clampSize(options?.width);
      const captureHeight = clampSize(options?.height);
      const needsOffscreen = captureWidth !== null || captureHeight !== null;

      const canvas = ctx.renderer.domElement;
      const savedPixelRatio = ctx.renderer.getPixelRatio();
      const savedWidth = canvas.width;
      const savedHeight = canvas.height;
      const savedBackground = ctx.scene.background;
      const savedClearColor = new THREE.Color();
      ctx.renderer.getClearColor(savedClearColor);
      const savedClearAlpha = ctx.renderer.getClearAlpha();
      const savedHelpersVisible = ctx.helpers.visible;
      const savedHandlesVisible = ctx.handles.visible;
      const savedContentVisibility = new Map<THREE.Object3D, boolean>();
      ctx.content.traverse((child) => savedContentVisibility.set(child, child.visible));
      const savedGroups = collectPreviewMeshes(ctx.content)
        .map((mesh) => ({
          geometry: mesh.geometry,
          groups: mesh.geometry.groups.map((group) => ({ ...group })),
        }));
      let diagnosticLegend: CaptureDiagnosticLegend | null = null;

      const outWidth = captureWidth ?? Math.round(canvas.clientWidth * savedPixelRatio);
      const outHeight = captureHeight ?? Math.round(canvas.clientHeight * savedPixelRatio);

      try {
        if (renderPass && renderPass !== 'beauty') {
          diagnosticLegend = applyDiagnosticRenderPass(
            ctx.content,
            renderPass,
            dataRef.current?.materials ?? {},
            dataRef.current?.sourceMapping ?? null,
            ctx.camera,
          );
        } else if (hasTemporaryShading) {
          applyShading(ctx.content, captureShadingMode, currentShading.solidLighting, currentShading.wireframeOverlay, captureXray, ctx.matcapTexture, dataRef.current?.materials ?? {}, currentShading.pbrDebugView);
        }
        if (renderPass) {
          ctx.helpers.visible = false;
          ctx.handles.visible = false;
          ctx.content.traverse((child) => {
            if (child === ctx.content) return;
            const kind = child.userData.kind as string | undefined;
            child.visible = kind === 'mesh' || kind === 'component' || kind === 'action-root' ||
              child instanceof THREE.Group || child instanceof THREE.Bone;
          });
        }
        if (needsOffscreen) {
          ctx.renderer.setPixelRatio(1);
          ctx.renderer.setSize(outWidth, outHeight, false);
          const aspect = Math.max(outWidth / Math.max(outHeight, 1), 0.01);
          for (const cam of [ctx.perspective, ctx.orthographic]) {
            if (cam instanceof THREE.PerspectiveCamera) {
              cam.aspect = aspect;
            } else {
              const frustumHeight = Number(cam.userData.frustumHeight) || (cam.top - cam.bottom);
              const halfH = frustumHeight / 2;
              cam.left = -halfH * aspect;
              cam.right = halfH * aspect;
              cam.top = halfH;
              cam.bottom = -halfH;
            }
            cam.updateProjectionMatrix();
          }
          syncEdgeOverlayResolution(ctx.content, outWidth, outHeight);
          composerRef.current?.composer.setSize(outWidth, outHeight);
        }
        const transparent = options?.transparent === true || renderPass === 'alpha-silhouette';
        if (transparent) {
          ctx.scene.background = null;
          ctx.renderer.setClearColor(0x000000, 0);
        } else if (renderPass && renderPass !== 'beauty') {
          ctx.scene.background = null;
          ctx.renderer.setClearColor(0x000000, 1);
        }
        ctx.controls.update();
        if (dofActive) renderWithDof(ctx, physicalCameraRef.current);
        else ctx.renderer.render(ctx.scene, ctx.camera);
        const dataUrl = canvas.toDataURL('image/png');
        const state = getCameraState() ?? physicalCameraRef.current;
        const { solidLighting: lighting, matcapId: matcap, wireframeOverlay: wireframe } = currentShading;
        const fov = ctx.camera instanceof THREE.PerspectiveCamera ? ctx.camera.fov : 0;
        return {
          pngBase64: dataUrl.slice(dataUrl.indexOf(',') + 1),
          metadata: {
            camera: {
              position: ctx.camera.position.toArray(),
              target: ctx.controls.target.toArray(),
              up: ctx.camera.up.toArray(),
              fov,
              near: ctx.camera.near,
              far: ctx.camera.far,
              projection: ctx.camera instanceof THREE.OrthographicCamera ? 'orthographic' : 'perspective',
              view: ctx.reviewPose?.view,
            },
            physicalCamera: state,
            viewport: {
              width: canvas.width,
              height: canvas.height,
              pixelRatio: ctx.renderer.getPixelRatio(),
            },
            shadingMode: captureShadingMode,
            solidLighting: lighting,
            matcapId: matcap,
            wireframeOverlay: wireframe,
            xrayEnabled: captureXray,
            reviewPose: ctx.reviewPose,
            meshBandProfile: dataRef.current?.mesh
              ? computeMeshBandProfile(dataRef.current.mesh)
              : null,
            renderPass,
            diagnosticLegend,
          },
        };
      } finally {
        for (const saved of savedGroups) {
          saved.geometry.clearGroups();
          for (const group of saved.groups) {
            saved.geometry.addGroup(group.start, group.count, group.materialIndex);
          }
        }
        if (hasTemporaryShading) {
          applyShading(ctx.content, currentShading.shadingMode, currentShading.solidLighting, currentShading.wireframeOverlay, currentShading.xrayEnabled, ctx.matcapTexture, dataRef.current?.materials ?? {}, currentShading.pbrDebugView);
        }
        ctx.helpers.visible = savedHelpersVisible;
        ctx.handles.visible = savedHandlesVisible;
        ctx.content.traverse((child) => {
          child.visible = savedContentVisibility.get(child) ?? child.visible;
        });
        ctx.scene.background = savedBackground;
        ctx.renderer.setClearColor(savedClearColor, savedClearAlpha);
        if (needsOffscreen) {
          ctx.renderer.setPixelRatio(savedPixelRatio);
          ctx.renderer.setSize(savedWidth / savedPixelRatio, savedHeight / savedPixelRatio, false);
          const aspect = Math.max(
            canvas.clientWidth / Math.max(canvas.clientHeight, 1),
            0.01,
          );
          for (const cam of [ctx.perspective, ctx.orthographic]) {
            if (cam instanceof THREE.PerspectiveCamera) {
              cam.aspect = aspect;
            } else {
              const frustumHeight = Number(cam.userData.frustumHeight) || (cam.top - cam.bottom);
              const halfH = frustumHeight / 2;
              cam.left = -halfH * aspect;
              cam.right = halfH * aspect;
              cam.top = halfH;
              cam.bottom = -halfH;
            }
            cam.updateProjectionMatrix();
          }
          syncEdgeOverlayResolution(
            ctx.content,
            Math.round(canvas.clientWidth),
            Math.round(canvas.clientHeight),
          );
          composerRef.current?.composer.setSize(canvas.clientWidth, canvas.clientHeight);
        }
      }
  }, [applyCameraCommand, getCameraState, renderWithDof]);

  const snapshotPng = useCallback((options: CaptureOptions): string | null => {
    const capture = captureFrameImpl(options);
    return capture ? `data:image/png;base64,${capture.pngBase64}` : null;
  }, [captureFrameImpl]);

  const actionController = useCallback((): ActionRuntimeController | null => (
    (sceneRef.current?.content.userData.actionController as ActionRuntimeController | undefined) ?? null
  ), []);

  useImperativeHandle(ref, () => ({
    captureFrame: captureFrameImpl,
    setReviewCamera: (nextView, options) => applyActiveReviewCamera(nextView, options),
    applyCameraCommand: (command) => applyCameraCommand(command),
    getCameraState: () => getCameraState(),
    listAnimations: () => [...(actionController()?.animationNames ?? [])],
    listAnimationClips: () => [...(actionController()?.clips ?? [])],
    listComponents: () => [...(actionController()?.componentNames ?? [])],
    playAnimation: (name) => actionController()?.play(name) ?? false,
    pauseAnimation: () => actionController()?.pause(),
    resumeAnimation: () => actionController()?.resume() ?? false,
    stopAnimation: () => actionController()?.stop(),
    seekAnimation: (name, timeSeconds) => actionController()?.seek(name, timeSeconds) ?? false,
    setAnimationSpeed: (speed) => actionController()?.setPlaybackSpeed(speed),
    setAnimationLoop: (loop) => actionController()?.setLoop(loop),
    getAnimationPlaybackState: () => actionController()?.getPlaybackState() ?? null,
    setExplode: (amount) => actionController()?.setExplode(amount),
    inspectActionRuntime: () => actionController()?.inspect() ?? null,
    getPreservedGltfBytes: () => (
      (sceneRef.current?.content.userData.preservedGltfBytes as ArrayBuffer | undefined) ?? null
    ),
  }), [
    captureFrameImpl,
    applyActiveReviewCamera,
    applyCameraCommand,
    getCameraState,
    actionController,
  ]);

  useEffect(() => {
    preloadMatcap(DEFAULT_MATCAP_ID);
  }, []);

  const focusPreview = useCallback(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    const edit = splineEditRef.current;
    const sel = selectedIndexRef.current;

    if (edit && sel >= 0 && sel < edit.controlPoints.length) {
      ctx.camera = ctx.perspective;
      ctx.controls.object = ctx.perspective;
      focusCameraOnTarget(ctx.perspective, ctx.controls, unityToThree(edit.controlPoints[sel]));
      return;
    }

    const previewData = dataRef.current;
    if (!previewData) return;
    const positions = collectFitPositions(previewData, edit?.controlPoints);
    if (positions.length > 0) {
      ctx.camera = ctx.perspective;
      ctx.controls.object = ctx.perspective;
      fitCamera(ctx.perspective, ctx.controls, positions);
    }
  }, []);

  const setAxisView = useCallback((axis: ViewAxis) => {
    const ctx = sceneRef.current;
    if (!ctx) return;

    ctx.camera = ctx.perspective;
    ctx.controls.object = ctx.perspective;
    const camera = ctx.perspective;
    const { controls } = ctx;
    const distance = Math.max(camera.position.distanceTo(controls.target), 0.08);
    camera.up.copy(AXIS_VIEW_UPS[axis]);
    camera.position.copy(controls.target).addScaledVector(AXIS_VIEW_DIRECTIONS[axis], distance);
    camera.lookAt(controls.target);
    camera.updateProjectionMatrix();
    controls.update();
    setAxisNavigation(getAxisNavigation(camera));
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
        ctx.perspective,
        pos,
        ctx.renderer.domElement.clientHeight,
      );
      const lineRadius = gizmoLineRadiusForCamera(
        ctx.perspective,
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

    const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true, alpha: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setClearColor(0x3d3d3d, 1);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.0;
    container.appendChild(renderer.domElement);

    const pmremGenerator = new THREE.PMREMGenerator(renderer);
    pmremGenerator.compileEquirectangularShader();
    const envMap = pmremGenerator.fromScene(new RoomEnvironment(), 0.04).texture;

    const scene = new THREE.Scene();
    scene.environment = envMap;
    scene.environmentIntensity = 1;
    const perspective = new THREE.PerspectiveCamera(50, 1, 0.01, 5000);
    perspective.position.set(3, 2.5, 4);
    const orthographic = new THREE.OrthographicCamera(-1, 1, 1, -1, 0.01, 5000);
    orthographic.position.set(3, 2.5, 4);

    const controls = new OrbitControls(perspective, renderer.domElement);
    controls.enableDamping = true;
    // Blender-style: MMB orbit, Shift+MMB pan, scroll zoom (OrbitControls maps shift+rotate → pan).
    controls.mouseButtons = {
      LEFT: THREE.MOUSE.ROTATE,
      MIDDLE: THREE.MOUSE.ROTATE,
      RIGHT: THREE.MOUSE.PAN,
    };
    const syncAxisNavigation = () => {
      const active = sceneRef.current?.camera ?? perspective;
      if (active instanceof THREE.PerspectiveCamera) {
        setAxisNavigation(getAxisNavigation(active));
      }
    };
    controls.addEventListener('change', syncAxisNavigation);
    syncAxisNavigation();

    scene.add(new THREE.HemisphereLight(0xffffff, 0x333a44, 0.2));
    const dir = new THREE.DirectionalLight(0xffffff, 1.15);
    dir.position.set(4, 8, 5);
    scene.add(dir);

    const helpers = new THREE.Group();
    helpers.add(createInfiniteGrid());
    helpers.add(new THREE.AxesHelper(0.75));
    helpers.visible = !reviewModeRef.current;
    scene.add(helpers);

    const content = new THREE.Group();
    scene.add(content);
    const handles = new THREE.Group();
    scene.add(handles);

    sceneRef.current = {
      renderer,
      scene,
      camera: perspective,
      perspective,
      orthographic,
      controls,
      content,
      handles,
      helpers,
      gizmoLength: 0.5,
      selectedIndex: -1,
      envMap,
      backgroundMap: null,
      keyLight: dir,
      matcapTexture: null,
      pmremGenerator,
      reviewPose: null,
    };

    loadMatcapTexture(DEFAULT_MATCAP_ID).then((tex) => {
      if (sceneRef.current) sceneRef.current.matcapTexture = tex;
    });

    const resize = () => {
      const w = container.clientWidth;
      const h = container.clientHeight;
      if (w === 0 || h === 0) return;
      renderer.setSize(w, h);
      const ctx = sceneRef.current;
      const active = ctx?.camera ?? perspective;
      if (active instanceof THREE.PerspectiveCamera) {
        active.aspect = w / h;
      } else if (active instanceof THREE.OrthographicCamera) {
        const frustumHeight = Number(active.userData.frustumHeight) || (active.top - active.bottom);
        const halfH = frustumHeight / 2;
        const halfW = halfH * (w / h);
        active.left = -halfW;
        active.right = halfW;
        active.top = halfH;
        active.bottom = -halfH;
      }
      active.updateProjectionMatrix();
      perspective.aspect = w / h;
      perspective.updateProjectionMatrix();
      syncEdgeOverlayResolution(content, w, h);
      composerRef.current?.composer.setSize(w, h);
    };
    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(container);

    let raf = 0;
    let lastPlaybackUiUpdate = 0;
    const clock = new THREE.Clock();
    const tick = () => {
      raf = requestAnimationFrame(tick);
      const deltaSeconds = Math.min(clock.getDelta(), 0.1);
      controls.update();
      const ctx = sceneRef.current;
      if (ctx) {
        const controller = ctx.content.userData.actionController as ActionRuntimeController | undefined;
        controller?.advance(deltaSeconds);
        const now = performance.now();
        if (controller && now - lastPlaybackUiUpdate >= 80) {
          lastPlaybackUiUpdate = now;
          updateAnimationPlayback(controller.getPlaybackState());
        }
        const gizmo = ctx.handles.children.find((c) => c.userData.kind === 'axis-gizmo') as
          | THREE.Group
          | undefined;
        const edit = splineEditRef.current;
        const sel = selectedIndexRef.current;
        if (gizmo && edit && sel >= 0 && sel < edit.controlPoints.length) {
          const pos = unityToThree(edit.controlPoints[sel]);
          const len = gizmoLengthForCamera(
            ctx.perspective,
            pos,
            ctx.renderer.domElement.clientHeight,
          );
          ctx.gizmoLength = len;
          rescaleAxisGizmo(gizmo, len);
        }
      }
      const activeCamera = sceneRef.current?.camera ?? perspective;
      const liveCtx = sceneRef.current;
      if (liveCtx && physicalCameraRef.current.dofEnabled && activeCamera instanceof THREE.PerspectiveCamera) {
        renderWithDof(liveCtx, physicalCameraRef.current);
      } else {
        renderer.render(scene, activeCamera);
      }
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
          ctx.perspective,
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
            ctx.perspective,
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
        ctx.perspective,
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
        ctx.perspective,
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
      drag.livePoints[drag.index] = dragPoint(drag, ctx.perspective, event.clientX, event.clientY, rect);
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

    // ── Node picking: click (not orbit drag) a mesh component → attributed node ──
    const onPickDown = (event: PointerEvent) => {
      pickDownRef.current = {
        x: event.clientX,
        y: event.clientY,
        consumed: dragRef.current != null,
      };
    };
    const onPickUp = (event: PointerEvent) => {
      const start = pickDownRef.current;
      pickDownRef.current = null;
      if (!start || start.consumed || dragRef.current) return;
      if (event.button !== 0) return;
      const dx = event.clientX - start.x;
      const dy = event.clientY - start.y;
      if (dx * dx + dy * dy > 25) return; // orbit drag, not a click
      const ctx = sceneRef.current;
      const mapping = sourceMappingRef.current;
      const pick = onPickNodeRef.current;
      if (!ctx || !mapping || !pick) return;
      const pickRect = ctx.renderer.domElement.getBoundingClientRect();
      if (pickRect.width <= 0 || pickRect.height <= 0) return;
      const ndc = new THREE.Vector2(
        ((event.clientX - pickRect.left) / pickRect.width) * 2 - 1,
        -((event.clientY - pickRect.top) / pickRect.height) * 2 + 1,
      );
      const raycaster = new THREE.Raycaster();
      raycaster.setFromCamera(ndc, ctx.camera);
      const meshes = ctx.content.children.filter(
        (obj): obj is THREE.Mesh => (obj as THREE.Mesh).userData?.kind === 'mesh',
      );
      const hits = raycaster.intersectObjects(meshes, false);
      for (const hit of hits) {
        const triangle = hit.faceIndex;
        if (triangle == null || triangle < 0 || triangle >= mapping.triangleSources.length) continue;
        const source = mapping.triangleSources[triangle];
        if (source < 0 || source >= mapping.sourceNodes.length) continue;
        pick(mapping.sourceNodes[source]);
        return;
      }
    };
    canvas.addEventListener('pointerdown', onPickDown);
    canvas.addEventListener('pointerup', onPickUp);

    return () => {
      invalidateEnvironmentLoads();
      cancelAnimationFrame(raf);
      observer.disconnect();
      canvas.removeEventListener('pointerdown', focusContainer);
      canvas.removeEventListener('pointerdown', onPointerDown);
      canvas.removeEventListener('pointermove', onPointerMove);
      canvas.removeEventListener('pointerup', onPointerUp);
      canvas.removeEventListener('pointercancel', onPointerUp);
      canvas.removeEventListener('pointerdown', onPickDown);
      canvas.removeEventListener('pointerup', onPickUp);
      controls.dispose();
      controls.removeEventListener('change', syncAxisNavigation);
      pmremGenerator.dispose();
      if (sceneRef.current?.envMap && sceneRef.current.envMap !== envMap) {
        sceneRef.current.envMap.dispose();
      }
      envMap.dispose();
      if (sceneRef.current?.backgroundMap) sceneRef.current.backgroundMap.dispose();
      disposeMatcapLibrary();
      disposePbrTextureCache();
      disposeGroup(content);
      disposeObject3D(handles);
      handles.clear();
      composerRef.current?.composer.dispose();
      composerRef.current = null;
      renderer.dispose();
      renderer.domElement.remove();
      sceneRef.current = null;
    };
  }, [syncSplineHandles, focusPreview, invalidateEnvironmentLoads, renderWithDof, updateAnimationPlayback]);

  const loadEnvironmentUrl = useCallback(async (
    url: string,
    name: string,
    id: string,
    format: 'hdr' | 'exr',
  ) => {
    const startingContext = sceneRef.current;
    const pmremGenerator = startingContext?.pmremGenerator;
    if (!startingContext || !pmremGenerator) return;
    const version = ++environmentLoadVersionRef.current;
    try {
      const source = format === 'exr'
        ? await new EXRLoader().loadAsync(url)
        : await new HDRLoader().loadAsync(url);
      const ctx = sceneRef.current;
      if (version !== environmentLoadVersionRef.current || ctx !== startingContext) {
        source.dispose();
        return;
      }
      source.mapping = THREE.EquirectangularReflectionMapping;
      const nextEnvironment = pmremGenerator.fromEquirectangular(source).texture;
      if (ctx.envMap) ctx.envMap.dispose();
      if (ctx.backgroundMap) ctx.backgroundMap.dispose();
      ctx.envMap = nextEnvironment;
      ctx.backgroundMap = source;
      ctx.scene.environment = nextEnvironment;
      ctx.scene.background = backgroundVisibleRef.current ? source : null;
      setEnvironmentId(id);
    } catch (error) {
      if (version === environmentLoadVersionRef.current) {
        console.error(`[PCG] Failed to load HDRI "${name}"`, error);
      }
    }
  }, []);

  const loadBuiltinEnvironment = useCallback((id: string) => {
    const environment = getBuiltinEnvironment(id);
    if (!environment) return;
    void loadEnvironmentUrl(environment.url, environment.name, environment.id, 'hdr');
  }, [loadEnvironmentUrl]);

  const loadEnvironmentFile = useCallback(async (file: File) => {
    const objectUrl = URL.createObjectURL(file);
    try {
      await loadEnvironmentUrl(
        objectUrl,
        file.name,
        EXTERNAL_ENVIRONMENT_ID,
        file.name.toLowerCase().endsWith('.exr') ? 'exr' : 'hdr',
      );
    } finally {
      URL.revokeObjectURL(objectUrl);
    }
  }, [loadEnvironmentUrl]);

  useEffect(() => {
    loadBuiltinEnvironment(DEFAULT_BUILTIN_ENVIRONMENT.id);
  }, [loadBuiltinEnvironment]);

  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    const radians = THREE.MathUtils.degToRad(environmentRotation);
    ctx.scene.environmentRotation.set(0, radians, 0);
    ctx.scene.backgroundRotation.set(0, radians, 0);
    ctx.scene.environmentIntensity = environmentIntensity;
    ctx.renderer.toneMappingExposure = exposure;
    ctx.keyLight.intensity = keyLightIntensity;
    ctx.scene.background = backgroundVisible ? (ctx.backgroundMap ?? ctx.envMap) : null;
  }, [environmentRotation, environmentIntensity, exposure, keyLightIntensity, backgroundVisible]);

  useEffect(() => {
    const canvas = sceneRef.current?.renderer.domElement;
    const controls = sceneRef.current?.controls;
    if (!canvas || !controls) return;
    let rotating = false;
    let startX = 0;
    let startRotation = 0;
    const onPointerDown = (event: PointerEvent) => {
      if (!(event.shiftKey && event.button === 2)) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      rotating = true;
      startX = event.clientX;
      startRotation = environmentRotationRef.current;
      controls.enabled = false;
      canvas.setPointerCapture(event.pointerId);
    };
    const onPointerMove = (event: PointerEvent) => {
      if (!rotating) return;
      event.preventDefault();
      setEnvironmentRotation(startRotation + (event.clientX - startX) * 0.35);
    };
    const onPointerUp = (event: PointerEvent) => {
      if (!rotating) return;
      rotating = false;
      controls.enabled = true;
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
    };
    const onContextMenu = (event: MouseEvent) => {
      if (event.shiftKey || rotating) event.preventDefault();
    };
    canvas.addEventListener('pointerdown', onPointerDown, { capture: true });
    canvas.addEventListener('pointermove', onPointerMove);
    canvas.addEventListener('pointerup', onPointerUp);
    canvas.addEventListener('pointercancel', onPointerUp);
    canvas.addEventListener('contextmenu', onContextMenu);
    return () => {
      canvas.removeEventListener('pointerdown', onPointerDown, { capture: true });
      canvas.removeEventListener('pointermove', onPointerMove);
      canvas.removeEventListener('pointerup', onPointerUp);
      canvas.removeEventListener('pointercancel', onPointerUp);
      canvas.removeEventListener('contextmenu', onContextMenu);
      controls.enabled = true;
    };
  }, []);

  const toggleAnimationPlayback = useCallback(() => {
    const controller = actionController();
    if (!controller || !selectedAnimation) return;
    const current = controller.getPlaybackState();
    if (current.clipName === selectedAnimation && current.playing) {
      controller.pause();
    } else if (current.clipName === selectedAnimation) {
      controller.resume();
    } else {
      controller.play(selectedAnimation);
    }
    updateAnimationPlayback(controller.getPlaybackState());
  }, [actionController, selectedAnimation, updateAnimationPlayback]);

  const stopAnimationPlayback = useCallback(() => {
    const controller = actionController();
    controller?.stop();
    if (controller) updateAnimationPlayback(controller.getPlaybackState());
  }, [actionController, updateAnimationPlayback]);

  const selectAnimationClip = useCallback((name: string) => {
    setSelectedAnimation(name);
    const controller = actionController();
    if (!controller?.seek(name, 0)) return;
    updateAnimationPlayback(controller.getPlaybackState());
  }, [actionController, updateAnimationPlayback]);

  const seekAnimationPlayback = useCallback((timeSeconds: number) => {
    const controller = actionController();
    if (!controller || !selectedAnimation || !controller.seek(selectedAnimation, timeSeconds)) return;
    updateAnimationPlayback(controller.getPlaybackState());
  }, [actionController, selectedAnimation, updateAnimationPlayback]);

  const changeAnimationSpeed = useCallback((speed: number) => {
    const controller = actionController();
    controller?.setPlaybackSpeed(speed);
    if (controller) updateAnimationPlayback(controller.getPlaybackState());
  }, [actionController, updateAnimationPlayback]);

  const changeAnimationLoop = useCallback((loop: boolean) => {
    const controller = actionController();
    if (!controller || !selectedAnimation) return;
    if (controller.currentAnimation !== selectedAnimation) controller.seek(selectedAnimation, 0);
    controller.setLoop(loop);
    updateAnimationPlayback(controller.getPlaybackState());
  }, [actionController, selectedAnimation, updateAnimationPlayback]);

  // F frames the preview; Space controls playback while Animation mode owns focus.
  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;
    const onKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === 'INPUT' || target.tagName === 'SELECT' || target.tagName === 'TEXTAREA') return;
      if (!container.contains(target) && document.activeElement !== container) return;
      if (e.key === 'f' || e.key === 'F') {
        e.preventDefault();
        e.stopPropagation();
        focusPreview();
      } else if (e.code === 'Space' && animationModeOpen && animationClips.length > 0) {
        e.preventDefault();
        e.stopPropagation();
        toggleAnimationPlayback();
      }
    };
    container.addEventListener('keydown', onKeyDown);
    return () => container.removeEventListener('keydown', onKeyDown);
  }, [animationClips.length, animationModeOpen, focusPreview, toggleAnimationPlayback]);

  // ── Content rebuild on new cook data ─────────────────
  useEffect(() => {
    const rebuildStart = performance.now();
    let cancelled = false;
    const ctx = sceneRef.current;
    if (!ctx) return;
    const previousController = ctx.content.userData.actionController as ActionRuntimeController | undefined;
    previousController?.dispose();
    syncAnimationRuntime(null);
    disposeGroup(ctx.content);
    ctx.content.clear();
    delete ctx.content.userData.actionController;
    delete ctx.content.userData.preservedGltfBytes;
    if (!data) {
      hasAutoFramedRef.current = false;
      return;
    }

    const geometry = data.geometry;
    const mesh = data.mesh;
    const cloudPositions = geometry ? geometry.positions : data.scatterPoints;

    if (mesh && data.sourceRig) {
      void loadPreservedGltfRig(data.sourceRig).then((preserved) => {
        if (cancelled) {
          preserved.controller.dispose();
          disposeObject3D(preserved.root);
          return;
        }
        ctx.content.add(preserved.root);
        ctx.content.userData.actionController = preserved.controller;
        ctx.content.userData.preservedGltfBytes = preserved.sourceBytes;
        syncAnimationRuntime(preserved.controller);
        const current = shadingRef.current;
        applyShading(
          preserved.root,
          current.shadingMode,
          current.solidLighting,
          current.wireframeOverlay,
          current.xrayEnabled,
          ctx.matcapTexture,
          data.materials,
          current.pbrDebugView,
        );
      }).catch((loadError) => {
        if (cancelled) return;
        console.error('PreserveGltfRig preview failed:', loadError);
        // Keep a visible diagnostic fallback, but never claim it is the
        // preserved animated result through the runtime controller.
        const fallback = buildMeshObject(mesh, flipZArray);
        fallback.userData.preservedGltfError = loadError instanceof Error
          ? loadError.message
          : String(loadError);
        ctx.content.add(fallback);
        syncAnimationRuntime(null);
      });
    } else if (mesh && data.actionRuntime) {
      const action = buildActionRigObject(mesh, data.actionRuntime);
      ctx.content.add(action.root);
      ctx.content.userData.actionController = action.controller;
      syncAnimationRuntime(action.controller);
    } else if (mesh) {
      ctx.content.add(buildMeshObject(mesh, flipZArray));
    }
    if (!mesh && cloudPositions && cloudPositions.length > 0) {
      ctx.content.add(buildPointsObject(flipZArray(cloudPositions)));
    }
    if (data.splines) ctx.content.add(...buildSplineObjects(data.splines));

    const fitPositions = collectFitPositions(data, splineEdit?.controlPoints);
    if (fitPositions.length > 0) {
      if (reviewCameraRef.current) {
        applyActiveReviewCamera();
        hasAutoFramedRef.current = true;
      } else if (!hasAutoFramedRef.current) {
        ctx.camera = ctx.perspective;
        ctx.controls.object = ctx.perspective;
        fitCamera(ctx.perspective, ctx.controls, fitPositions);
        hasAutoFramedRef.current = true;
      }
    }

    const {
      shadingMode,
      solidLighting,
      wireframeOverlay: activeWireframeOverlay,
      xrayEnabled,
      pbrDebugView,
    } = shadingRef.current;
    applyShading(
      ctx.content,
      shadingMode,
      solidLighting,
      activeWireframeOverlay,
      xrayEnabled,
      ctx.matcapTexture,
      data.materials,
      pbrDebugView,
    );
    sceneTimedDataRef.current = data;
    setSceneMs(performance.now() - rebuildStart);
    return () => { cancelled = true; };
  }, [data]); // eslint-disable-line react-hooks/exhaustive-deps

  // Polygon edges and spline authoring helpers are lightweight overlays. Keep
  // them outside the cooked-content effect so toggling an overlay never
  // rebuilds a dense ActionRig, skin weights, or component buffers.
  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    const old = ctx.content.children.find((child) => child.userData.kind === 'edges');
    if (old) {
      ctx.content.remove(old);
      disposeObject3D(old);
    }
    const geometry = data?.geometry;
    if (!wireframeOverlay || !geometry || geometry.faceCount === 0) return;
    const edgeObj = buildEdgeObject(
      geometry,
      flipZArray(geometry.positions),
      ctx.renderer.domElement.clientWidth,
      ctx.renderer.domElement.clientHeight,
    );
    if (edgeObj) ctx.content.add(edgeObj);
  }, [data?.geometry, wireframeOverlay]);

  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    const old = ctx.content.children.find((child) => child.userData.kind === 'control-line');
    if (old) {
      ctx.content.remove(old);
      disposeObject3D(old);
    }
    if (!data || !splineEdit || splineEdit.controlPoints.length === 0) return;
    ctx.content.add(buildControlPolyline(splineEdit.controlPoints, splineEdit.closed));
  }, [data, splineEdit]);

  // ── Selection edge highlight: overlay the triangles attributed to the
  // graph-selected node. Declared after the content rebuild so a fresh cook
  // re-applies the highlight to the new mesh on the same commit.
  useEffect(() => {
    const ctx = sceneRef.current;
    if (!ctx) return;
    const old = ctx.content.children.find((c) => c.userData.kind === 'selection');
    if (old) {
      ctx.content.remove(old);
      disposeObject3D(old);
    }
    const mapping = data?.sourceMapping;
    if (!mapping || !selectedNodeId) return;
    const highlight = buildSelectionHighlight(
      ctx.content,
      mapping,
      selectedNodeId,
      ctx.renderer.domElement.clientWidth,
      ctx.renderer.domElement.clientHeight,
    );
    if (highlight) ctx.content.add(highlight);
  }, [data, splineEdit, selectedNodeId]);

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
    const ctx = sceneRef.current;
    if (!ctx) return;
    const runShading = () => {
      const { shadingMode, solidLighting, wireframeOverlay, xrayEnabled, pbrDebugView } = shadingRef.current;
      applyShading(
        ctx.content,
        shadingMode,
        solidLighting,
        wireframeOverlay,
        xrayEnabled,
        ctx.matcapTexture,
        data?.materials ?? {},
        pbrDebugView,
      );
    };

    if (shadingMode === 'solid' && solidLighting === 'matcap') {
      loadMatcapTexture(matcapId).then((tex) => {
        if (!sceneRef.current) return;
        sceneRef.current.matcapTexture = tex;
        runShading();
      });
    } else {
      runShading();
    }
  }, [shadingMode, solidLighting, wireframeOverlay, xrayEnabled, matcapId, pbrDebugView, data?.materials]);

  useEffect(() => {
    if (shadingMode === 'rendered') setSolidPopoverOpen(false);
  }, [shadingMode]);

  useEffect(() => {
    if (!overlayPopoverOpen) return;
    const onPointerDown = (e: PointerEvent) => {
      if (overlayPopoverRef.current?.contains(e.target as Node)) return;
      setOverlayPopoverOpen(false);
    };
    window.addEventListener('pointerdown', onPointerDown);
    return () => window.removeEventListener('pointerdown', onPointerDown);
  }, [overlayPopoverOpen]);

  useEffect(() => {
    if (!solidPopoverOpen) return;
    const onPointerDown = (e: PointerEvent) => {
      if (solidPopoverRef.current?.contains(e.target as Node)) return;
      setSolidPopoverOpen(false);
    };
    window.addEventListener('pointerdown', onPointerDown);
    return () => window.removeEventListener('pointerdown', onPointerDown);
  }, [solidPopoverOpen]);

  useEffect(() => {
    if (!cameraPopoverOpen) return;
    const onPointerDown = (e: PointerEvent) => {
      if (cameraPopoverRef.current?.contains(e.target as Node)) return;
      setCameraPopoverOpen(false);
    };
    window.addEventListener('pointerdown', onPointerDown);
    return () => window.removeEventListener('pointerdown', onPointerDown);
  }, [cameraPopoverOpen]);

  useEffect(() => {
    const ctx = sceneRef.current;
    if (ctx) ctx.helpers.visible = !reviewMode;
  }, [reviewMode]);

  useEffect(() => {
    if (reviewCamera && data) applyActiveReviewCamera();
  }, [reviewCamera, data, applyActiveReviewCamera]);

  const xrayActive = xrayEnabled && shadingMode === 'solid';

  const images = data?.images ?? [];
  const has3dContent = !!(data?.geometry || data?.mesh || data?.scatterPoints) || (data?.splines?.splines.length ?? 0) > 0;
  const preferredTab: '3d' | 'image' = images.length > 0 && !has3dContent ? 'image' : '3d';
  const [tabOverride, setTabOverride] = useState<'3d' | 'image' | 'uv' | null>(null);
  // Follow the cooked output type only when it flips (geometry ↔ image-only);
  // re-cooks of the same kind keep the manual tab (3D/UV/Image) selection.
  useEffect(() => setTabOverride(null), [preferredTab]);
  const activeTab = tabOverride ?? preferredTab;

  const geometry = data?.geometry ?? null;
  const splineCount = data?.splines?.splines.length ?? 0;
  const stats = geometry
    ? `${geometry.pointCount} pts · ${geometry.faceCount} faces · ${geometry.triangles.length / 3} tris`
    : data?.mesh && data.heightfield
      ? `${data.heightfield.resolutionX}×${data.heightfield.resolutionZ} terrain · ${data.mesh.vertexCount} verts`
      : data?.mesh
        ? `${data.mesh.vertexCount} verts · ${data.mesh.indexCount / 3} tris`
    : data?.scatterPoints
      ? `${data.scatterPoints.length / 3} scatter pts`
      : splineCount > 0
        ? `${splineCount} spline${splineCount === 1 ? '' : 's'}`
        : images.length > 0
          ? `image · ${images[0].nodeId}`
          : 'no geometry';

  return (
    <div
      className={`pcg-preview${reviewMode ? ' is-review' : ''}${animationModeOpen ? ' has-animation-mode' : ''}`}
      style={{ width: reviewMode ? '100%' : width, height: reviewMode ? '100%' : undefined }}
    >
      <div
        className="pcg-preview__resize-handle"
        onMouseDown={onResizeStart}
        title="Drag to resize"
      />
      {splineEdit && (
        <div className="pcg-preview__header">
          <span
            className="pcg-preview__spline-hint"
            title="Click a control point, then drag center (free) or RGB axes (X/Y/Z)"
          >
            {selectedIndex >= 0 ? 'Drag axis or center' : 'Click control point'}
          </span>
        </div>
      )}
      <div className="pcg-preview__viewport">
        <div className="pcg-preview__tabs" role="tablist" aria-label="Preview type">
          <button
            type="button"
            role="tab"
            aria-selected={activeTab === '3d'}
            className={`pcg-preview__tab${activeTab === '3d' ? ' is-active' : ''}`}
            title={has3dContent ? '3D scene preview' : '3D scene preview (cook produced no geometry)'}
            onClick={() => setTabOverride('3d')}
          >
            3D
          </button>
          <button
            type="button"
            role="tab"
            aria-selected={activeTab === 'uv'}
            className={`pcg-preview__tab${activeTab === 'uv' ? ' is-active' : ''}`}
            title={data?.mesh?.uvs ? 'UV layout preview' : 'UV layout (cook produced no UVs)'}
            onClick={() => setTabOverride('uv')}
          >
            UV
          </button>
          <button
            type="button"
            role="tab"
            aria-selected={activeTab === 'image'}
            className={`pcg-preview__tab${activeTab === 'image' ? ' is-active' : ''}`}
            title={images.length > 0 ? 'Image preview' : 'Cook produced no image'}
            onClick={() => setTabOverride('image')}
          >
            Image
          </button>
        </div>
        <div
          ref={containerRef}
          className="pcg-preview__canvas"
          tabIndex={0}
          style={activeTab !== '3d' ? { visibility: 'hidden' } : undefined}
          title="Click to focus · MMB orbit · Shift+MMB pan · Shift+RMB rotate IBL · scroll zoom · F frame"
          onPointerDown={() => containerRef.current?.focus({ preventScroll: true })}
        />
        {activeTab === 'uv' && <UvPreviewPane mesh={data?.mesh ?? null} />}
        {activeTab === 'image' && (
          images.length > 0 ? (
            <ImagePreviewPane image={images[0]} />
          ) : (
            <div className="pcg-preview__image-pane pcg-preview__image-pane--empty">
              <span>Cook produced no image</span>
            </div>
          )
        )}
        {activeTab === '3d' && (
        <>
        {animationClips.length > 0 && (
          <div className="pcg-preview__animation-entry">
            <button
              type="button"
              className={`pcg-preview__animation-mode${animationModeOpen ? ' is-active' : ''}`}
              aria-label="Animation mode"
              aria-pressed={animationModeOpen}
              title="Animation mode"
              onClick={() => setAnimationModeOpen((open) => !open)}
            >
              <svg viewBox="0 0 16 16" aria-hidden><path d="M4 2.5 13 8l-9 5.5z" /></svg>
              <span>Animate</span>
              <span className="pcg-preview__animation-count">{animationClips.length}</span>
            </button>
          </div>
        )}
        <div className="pcg-preview__axis-navigation" role="group" aria-label="Axis views">
          <svg className="pcg-preview__axis-stems" viewBox="0 0 84 84" aria-hidden="true">
            {(['x', 'y', 'z'] as const).map((axis) => (
              <line
                key={axis}
                className={`pcg-preview__axis-stem pcg-preview__axis-stem--${axis}`}
                x1={AXIS_NAVIGATION_CENTER}
                y1={AXIS_NAVIGATION_CENTER}
                x2={axisNavigation[axis].x}
                y2={axisNavigation[axis].y}
              />
            ))}
          </svg>
          <span className="pcg-preview__axis-origin" aria-hidden="true" />
          {(['x', 'y', 'z'] as const).map((axis) => {
            const point = axisNavigation[axis];
            return (
              <button
                key={axis}
                type="button"
                className={`pcg-preview__axis-button pcg-preview__axis-button--${axis}`}
                title={`${axis.toUpperCase()} axis view`}
                aria-label={`View along ${axis.toUpperCase()} axis`}
                onClick={() => setAxisView(axis)}
                style={{
                  left: point.x,
                  top: point.y,
                  transform: `translate(-50%, -50%) scale(${point.scale})`,
                  zIndex: point.zIndex,
                }}
              >
                {axis.toUpperCase()}
              </button>
            );
          })}
        </div>
        <div className="pcg-preview__shading-bar" role="toolbar" aria-label="Viewport shading">
          <div className="pcg-preview__shading-popover-wrap" ref={overlayPopoverRef}>
            <button
              type="button"
              className={`pcg-preview__shading-chevron${overlayPopoverOpen || wireframeOverlay ? ' is-active' : ''}`}
              title="Viewport overlays"
              aria-expanded={overlayPopoverOpen}
              onClick={() => setOverlayPopoverOpen((v) => !v)}
            >
              ▾
            </button>
            {overlayPopoverOpen && (
              <ViewportOverlaysPopover
                wireframeOverlay={wireframeOverlay}
                onWireframeOverlayChange={setWireframeOverlay}
              />
            )}
          </div>
          <button
            type="button"
            className={`pcg-preview__shading-xray${xrayActive ? ' is-active' : ''}`}
            title="X-Ray"
            disabled={shadingMode !== 'solid'}
            aria-pressed={xrayActive}
            onClick={() => setXrayEnabled((v) => !v)}
          >
            <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden>
              <rect x="1" y="1" width="8" height="8" fill="none" stroke="currentColor" strokeWidth="1.2" />
              <rect x="5" y="5" width="8" height="8" fill="none" stroke="currentColor" strokeWidth="1.2" strokeDasharray="2 1.5" />
            </svg>
          </button>
          <div className="pcg-preview__shading-modes" role="radiogroup" aria-label="Shading mode">
            <button
              type="button"
              className={`pcg-preview__shading-mode${shadingMode === 'solid' ? ' is-active' : ''}`}
              title="Solid"
              aria-pressed={shadingMode === 'solid'}
              onClick={() => setShadingMode('solid')}
            >
              <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden>
                <circle cx="8" cy="8" r="6.5" fill="currentColor" opacity="0.85" />
              </svg>
            </button>
            <button
              type="button"
              className={`pcg-preview__shading-mode${shadingMode === 'material' ? ' is-active' : ''}`}
              title="Material Preview"
              aria-pressed={shadingMode === 'material'}
              onClick={() => setShadingMode('material')}
            >
              <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden>
                <circle cx="8" cy="8" r="6.5" fill="none" stroke="currentColor" strokeWidth="1" />
                <path d="M2 2 L14 14 M14 2 L2 14" stroke="currentColor" strokeWidth="0.7" />
              </svg>
            </button>
            <button
              type="button"
              className="pcg-preview__shading-mode"
              title="Rendered — 后续实现"
              disabled
              aria-pressed={false}
            >
              <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden>
                <circle cx="8" cy="8" r="6.5" fill="currentColor" opacity="0.5" />
                <circle cx="5.5" cy="5.5" r="2" fill="currentColor" opacity="0.9" />
              </svg>
            </button>
          </div>
          <div className="pcg-preview__shading-popover-wrap" ref={solidPopoverRef}>
            <button
              type="button"
              className={`pcg-preview__shading-chevron${shadingMode === 'rendered' ? ' is-disabled' : ''}`}
              title={shadingMode === 'material' ? 'Material preview environment' : 'Solid shading options'}
              disabled={shadingMode === 'rendered'}
              aria-expanded={solidPopoverOpen}
              onClick={() => setSolidPopoverOpen((v) => !v)}
            >
              ▾
            </button>
            {solidPopoverOpen && shadingMode === 'solid' && (
              <SolidShadingPopover
                solidLighting={solidLighting}
                matcapId={matcapId}
                onSolidLightingChange={setSolidLighting}
                onMatcapIdChange={setMatcapId}
              />
            )}
            {solidPopoverOpen && shadingMode === 'material' && (
              <MaterialEnvironmentPopover
                environmentId={environmentId}
                rotation={environmentRotation}
                intensity={environmentIntensity}
                exposure={exposure}
                keyLightIntensity={keyLightIntensity}
                backgroundVisible={backgroundVisible}
                debugView={pbrDebugView}
                onBuiltinEnvironment={loadBuiltinEnvironment}
                onEnvironmentFile={loadEnvironmentFile}
                onRotationChange={setEnvironmentRotation}
                onIntensityChange={setEnvironmentIntensity}
                onExposureChange={setExposure}
                onKeyLightIntensityChange={setKeyLightIntensity}
                onBackgroundVisibleChange={setBackgroundVisible}
                onDebugViewChange={setPbrDebugView}
              />
            )}
          </div>
          <div className="pcg-preview__shading-popover-wrap" ref={cameraPopoverRef}>
            <button
              type="button"
              className={`pcg-preview__shading-mode${cameraPopoverOpen || cameraUiState.dofEnabled ? ' is-active' : ''}`}
              title="Physical camera"
              aria-expanded={cameraPopoverOpen}
              onClick={() => setCameraPopoverOpen((v) => !v)}
            >
              <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden>
                <path d="M1 5 L5 5 L6.5 3 L10 3 L11.5 5 L15 5 L15 13 L1 13 Z" fill="none" stroke="currentColor" strokeWidth="1.1" />
                <circle cx="8" cy="9" r="2.6" fill="none" stroke="currentColor" strokeWidth="1.1" />
              </svg>
            </button>
            {cameraPopoverOpen && (
              <CameraPopover
                state={cameraUiState}
                onCameraChange={(command) => { applyCameraCommand(command); }}
                onSnapshot={snapshotPng}
              />
            )}
          </div>
        </div>
        </>
        )}
        {activeTab === '3d' && animationModeOpen && animationClips.length > 0 && (
          <AnimationTransport
            clips={animationClips}
            selectedClip={selectedAnimation}
            playback={animationPlayback}
            onSelectClip={selectAnimationClip}
            onTogglePlayback={toggleAnimationPlayback}
            onStop={stopAnimationPlayback}
            onSeek={seekAnimationPlayback}
            onSpeedChange={changeAnimationSpeed}
            onLoopChange={changeAnimationLoop}
            onClose={() => setAnimationModeOpen(false)}
          />
        )}
        <div className="pcg-preview__parameter-overlay">
          <PreviewParametersPopover
            parameters={parameters}
            nodeIds={parameterNodeIds}
            values={parameterValues}
            open={parametersPopoverOpen}
            onOpenChange={setParametersPopoverOpen}
            onValueChange={onParameterValueChange}
            onReset={onResetParameters}
            onSaveDefaults={onSaveParameterDefaults}
          />
        </div>
      </div>
      <div className="pcg-preview__footer">
        <div className="pcg-preview__footer-left">
          <span className="pcg-preview__stats">{stats}</span>
          {data?.previewQuality && data.previewQuality.sdfNodeCount > 0 && (
            <span
              className="pcg-preview__perf"
              title={data.previewQuality.fullResolution
                ? 'Full authored SDF resolution'
                : `Bounded Web preview; final export uses the authored cell size${data.previewQuality.triangleBudget ? ` (${data.previewQuality.triangleBudget.toLocaleString()} triangle budget)` : ''}`}
            >
              {data.previewQuality.fullResolution
                ? 'SDF full'
                : `SDF ${data.previewQuality.requestedQuality} ×${data.previewQuality.effectiveScale.toFixed(2)}`}
            </span>
          )}
          {data && (
            <span className="pcg-preview__perf" title="exec: server graph execute · wall: fetch+server total · bin: server binary write · js: client parse+build · scene: three.js rebuild">
              {data.cook.graphExecuteMs.toFixed(0)}ms exec · {data.timings ? data.timings.fetchMs.toFixed(0) : '?'}ms wall · {data.cook.binaryWriteMs.toFixed(0)}ms bin · {data.timings ? (data.timings.parseCookMs + data.timings.buildDataMs).toFixed(0) : '?'}ms js · {sceneMs.toFixed(0)}ms scene · {data.cook.nodesExecuted} nodes
            </span>
          )}
        </div>
        <div className="pcg-preview__footer-actions">
          <button type="button" className="pcg-preview__btn pcg-preview__recook" onClick={onRefresh} disabled={loading}>
            {loading ? 'Cooking…' : 'Re-cook'}
          </button>
        </div>
      </div>
      {error && <div className="pcg-preview__error">{error}</div>}
    </div>
  );
});

export default PreviewViewport;

function applyShading(
  group: THREE.Group,
  shadingMode: ShadingMode,
  solidLighting: SolidLighting,
  wireframeOverlay: boolean,
  xrayEnabled: boolean,
  matcapTexture: THREE.Texture | null,
  materialLibrary: PbrMaterialLibrary,
  pbrDebugView: PbrDebugView,
) {
  let hasMesh = false;
  let hasEdges = false;
  group.traverse((child) => {
    hasMesh ||= child.userData.kind === 'mesh';
    hasEdges ||= child.userData.kind === 'edges';
  });
  const xrayActive = xrayEnabled && shadingMode === 'solid';
  const wireframeOnly = wireframeOverlay && !hasEdges;

  group.traverse((child) => {
    const kind = child.userData.kind as string;
    if (kind === 'spline' || kind === 'control-line' || kind === 'selection') {
      child.visible = true;
      return;
    }
    if (kind === 'edges') {
      child.visible = wireframeOverlay;
      return;
    }
    if (kind === 'points') {
      child.visible = !hasMesh && !wireframeOverlay;
      return;
    }
    if (kind !== 'mesh' || !(child instanceof THREE.Mesh)) return;

    const mesh = child;
    const hasVertexColors = mesh.geometry.hasAttribute('color');
    const oldMaterials = Array.isArray(mesh.material) ? mesh.material : [mesh.material];
    for (const oldMaterial of oldMaterials) oldMaterial.dispose();

    if (wireframeOnly) {
      mesh.visible = true;
      const mat = new THREE.MeshBasicMaterial({
        color: hasVertexColors ? 0xffffff : 0x9aa4ae,
        vertexColors: hasVertexColors,
        wireframe: true,
        side: THREE.DoubleSide,
      });
      applyXray(mat, xrayActive);
      mesh.material = mat;
      return;
    }

    mesh.visible = true;
    let mat: THREE.Material;
    if (shadingMode === 'material') {
      const slots = Array.isArray(mesh.userData.materialSlots)
        ? mesh.userData.materialSlots as string[]
        : [];
      if (slots.length > 0) {
        mesh.material = slots.map((slot) => {
          const definition = materialLibrary[slot] ?? normalizePbrMaterial({ name: slot }, slot);
          return pbrDebugView === 'lit'
            ? createStandardPbrMaterial(definition, hasVertexColors)
            : createPbrDebugMaterial(definition, pbrDebugView);
        });
        return;
      }
      const definition = normalizePbrMaterial({ name: 'Material' });
      mat = pbrDebugView === 'lit'
        ? createStandardPbrMaterial(definition, hasVertexColors)
        : createPbrDebugMaterial(definition, pbrDebugView);
    } else if (shadingMode === 'solid' && solidLighting === 'matcap' && matcapTexture) {
      mat = new THREE.MeshMatcapMaterial({
        color: hasVertexColors ? 0xffffff : 0x9aa4ae,
        vertexColors: hasVertexColors,
        matcap: matcapTexture,
        side: THREE.DoubleSide,
      });
    } else if (shadingMode === 'solid' && solidLighting === 'studio') {
      mat = createBlenderStudioMaterial(hasVertexColors);
    } else {
      mat = new THREE.MeshStandardMaterial({
        color: hasVertexColors ? 0xffffff : 0x9aa4ae,
        vertexColors: hasVertexColors,
        roughness: 0.85,
        metalness: 0.05,
        side: THREE.DoubleSide,
      });
    }
    applyXray(mat, xrayActive);
    mesh.material = mat;
  });
}

function applyXray(material: THREE.Material, active: boolean) {
  if (!active) return;
  material.transparent = true;
  material.opacity = XRAY_OPACITY;
  if (material instanceof THREE.ShaderMaterial && material.uniforms.opacity) {
    material.uniforms.opacity.value = XRAY_OPACITY;
  }
  material.depthWrite = false;
}

function diagnosticBasicMaterial(color: THREE.ColorRepresentation): THREE.MeshBasicMaterial {
  const material = new THREE.MeshBasicMaterial({ color, side: THREE.DoubleSide });
  material.toneMapped = false;
  return material;
}

function disposeMeshMaterials(mesh: THREE.Mesh): void {
  const materials = Array.isArray(mesh.material) ? mesh.material : [mesh.material];
  for (const material of materials) material.dispose();
}

function diagnosticIdColor(id: number): THREE.Color {
  const hue = (0.08 + id * 0.61803398875) % 1;
  return new THREE.Color().setHSL(hue, 0.72, 0.56);
}

function colorHex(color: THREE.Color): string {
  return `#${color.getHexString()}`;
}

function meshDepthRange(
  group: THREE.Group,
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera,
): [number, number] {
  camera.updateMatrixWorld(true);
  const bounds = new THREE.Box3();
  group.traverse((child) => {
    if (child instanceof THREE.Mesh && child.userData.kind === 'mesh') {
      child.updateMatrixWorld(true);
      bounds.expandByObject(child, true);
    }
  });
  if (bounds.isEmpty()) return [camera.near, camera.far];
  const depths: number[] = [];
  for (const x of [bounds.min.x, bounds.max.x]) {
    for (const y of [bounds.min.y, bounds.max.y]) {
      for (const z of [bounds.min.z, bounds.max.z]) {
        const point = new THREE.Vector3(x, y, z).applyMatrix4(camera.matrixWorldInverse);
        if (Number.isFinite(point.z)) depths.push(-point.z);
      }
    }
  }
  const minimum = Math.max(camera.near, Math.min(...depths));
  const maximum = Math.min(camera.far, Math.max(...depths));
  return maximum > minimum + 1e-6 ? [minimum, maximum] : [camera.near, camera.far];
}

function depthDiagnosticMaterial(range: [number, number]): THREE.ShaderMaterial {
  const material = new THREE.ShaderMaterial({
    uniforms: {
      minDepth: { value: range[0] },
      maxDepth: { value: range[1] },
    },
    vertexShader: `
      varying float vViewDepth;
      void main() {
        vec4 viewPosition = modelViewMatrix * vec4(position, 1.0);
        vViewDepth = -viewPosition.z;
        gl_Position = projectionMatrix * viewPosition;
      }
    `,
    fragmentShader: `
      uniform float minDepth;
      uniform float maxDepth;
      varying float vViewDepth;
      void main() {
        float range = max(maxDepth - minDepth, 0.000001);
        float depth = clamp((vViewDepth - minDepth) / range, 0.0, 1.0);
        gl_FragColor = vec4(vec3(1.0 - depth), 1.0);
      }
    `,
    side: THREE.DoubleSide,
  });
  material.toneMapped = false;
  return material;
}

function applySemanticIdPass(
  mesh: THREE.Mesh,
  mapping: SourceMapping | null,
): CaptureDiagnosticLegend {
  const triangleCount = Math.trunc((mesh.geometry.index?.count ?? 0) / 3);
  if (!mapping || mapping.triangleSources.length !== triangleCount) {
    const color = diagnosticIdColor(0);
    mesh.material = diagnosticBasicMaterial(color);
    return { semanticIds: [{ id: 0, sourceNode: 'unattributed', color: colorHex(color) }] };
  }

  const colors = [new THREE.Color(0x555555), ...mapping.sourceNodes.map((_, index) => diagnosticIdColor(index + 1))];
  mesh.material = colors.map((color) => diagnosticBasicMaterial(color));
  mesh.geometry.clearGroups();
  let firstTriangle = 0;
  let activeId = (mapping.triangleSources[0] ?? -1) + 1;
  for (let triangle = 1; triangle <= mapping.triangleSources.length; triangle++) {
    const nextId = triangle < mapping.triangleSources.length
      ? mapping.triangleSources[triangle] + 1
      : Number.NaN;
    if (nextId === activeId) continue;
    mesh.geometry.addGroup(firstTriangle * 3, (triangle - firstTriangle) * 3, Math.max(0, activeId));
    firstTriangle = triangle;
    activeId = nextId;
  }
  return {
    semanticIds: [
      { id: 0, sourceNode: 'unattributed', color: colorHex(colors[0]) },
      ...mapping.sourceNodes.map((sourceNode, index) => ({
        id: index + 1,
        sourceNode,
        color: colorHex(colors[index + 1]),
      })),
    ],
  };
}

function applyMaterialIdPass(
  mesh: THREE.Mesh,
  materialLibrary: PbrMaterialLibrary,
): CaptureDiagnosticLegend {
  const slots = Array.isArray(mesh.userData.materialSlots)
    ? mesh.userData.materialSlots as string[]
    : [];
  const effectiveSlots = slots.length > 0 ? slots : ['Material'];
  const legend = effectiveSlots.map((slot, id) => {
    const definition = materialLibrary[slot] ?? normalizePbrMaterial({ name: slot }, slot);
    const hue = (0.08 + id * 0.61803398875) % 1;
    const color = new THREE.Color().setHSL(
      hue,
      0.35 + definition.metallic * 0.55,
      0.2 + definition.roughness * 0.65,
    );
    return {
      id,
      slot,
      color: colorHex(color),
      roughness: definition.roughness,
      metallic: definition.metallic,
      material: diagnosticBasicMaterial(color),
    };
  });
  mesh.material = legend.map((entry) => entry.material);
  return {
    materialIds: legend.map(({ material: _material, ...entry }) => entry),
  };
}

function applyDiagnosticRenderPass(
  group: THREE.Group,
  renderPass: Exclude<CaptureRenderPass, 'beauty'>,
  materialLibrary: PbrMaterialLibrary,
  sourceMapping: SourceMapping | null,
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera,
): CaptureDiagnosticLegend | null {
  const meshes = collectPreviewMeshes(group);
  const depthRange = renderPass === 'depth' ? meshDepthRange(group, camera) : null;
  let legend: CaptureDiagnosticLegend | null = depthRange ? { depthRange } : null;
  for (const mesh of meshes) {
    disposeMeshMaterials(mesh);
    if (renderPass === 'alpha-silhouette') {
      mesh.material = diagnosticBasicMaterial(0xffffff);
    } else if (renderPass === 'normal') {
      const material = new THREE.MeshNormalMaterial({ side: THREE.DoubleSide });
      material.toneMapped = false;
      mesh.material = material;
    } else if (renderPass === 'depth') {
      mesh.material = depthDiagnosticMaterial(depthRange!);
    } else if (renderPass === 'semantic-id') {
      legend = applySemanticIdPass(mesh, sourceMapping);
    } else if (renderPass === 'roughness-material-id') {
      legend = applyMaterialIdPass(mesh, materialLibrary);
    }
  }
  return legend;
}


function buildMeshObject(mesh: ParsedMesh, flipZ: (src: Float32Array) => Float32Array): THREE.Mesh {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(flipZ(mesh.positions), 3));
  if (mesh.normals) geo.setAttribute('normal', new THREE.BufferAttribute(flipZ(mesh.normals), 3));
  if (mesh.colors && mesh.colors.length === mesh.vertexCount * 4) {
    geo.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 4));
  }
  if (mesh.uvs && mesh.uvs.length === mesh.vertexCount * 2) {
    const uv = new THREE.BufferAttribute(mesh.uvs, 2);
    geo.setAttribute('uv', uv);
    geo.setAttribute('uv1', uv);
  }
  const indices = new Uint32Array(mesh.indices.length);
  for (let i = 0; i < mesh.indices.length; i += 3) {
    indices[i] = mesh.indices[i];
    indices[i + 1] = mesh.indices[i + 2];
    indices[i + 2] = mesh.indices[i + 1];
  }
  geo.setIndex(new THREE.BufferAttribute(indices, 1));
  if (mesh.triangleMaterials && mesh.materialSlots.length > 0) {
    let firstTriangle = 0;
    let activeSlot = mesh.triangleMaterials[0] ?? 0;
    for (let triangle = 1; triangle <= mesh.triangleMaterials.length; triangle++) {
      const slot = triangle < mesh.triangleMaterials.length
        ? mesh.triangleMaterials[triangle]
        : Number.NaN;
      if (slot === activeSlot) continue;
      geo.addGroup(firstTriangle * 3, (triangle - firstTriangle) * 3, activeSlot);
      firstTriangle = triangle;
      activeSlot = slot;
    }
  }
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
  meshObj.userData.materialSlots = [...mesh.materialSlots];
  return meshObj;
}

function buildEdgeObject(
  geometry: ParsedGeometry,
  positions: Float32Array,
  viewportWidth: number,
  viewportHeight: number,
): LineSegments2 | null {
  const edges = buildEdgeIndices(geometry);
  if (edges.length === 0) return null;

  const segmentPositions = new Float32Array((edges.length / 2) * 6);
  for (let i = 0; i < edges.length; i += 2) {
    const a = edges[i] * 3;
    const b = edges[i + 1] * 3;
    const o = (i / 2) * 6;
    segmentPositions[o] = positions[a];
    segmentPositions[o + 1] = positions[a + 1];
    segmentPositions[o + 2] = positions[a + 2];
    segmentPositions[o + 3] = positions[b];
    segmentPositions[o + 4] = positions[b + 1];
    segmentPositions[o + 5] = positions[b + 2];
  }

  const lineGeo = new LineSegmentsGeometry();
  lineGeo.setPositions(segmentPositions);

  const material = new LineMaterial({
    color: EDGE_OVERLAY_COLOR,
    linewidth: EDGE_OVERLAY_LINE_WIDTH,
    worldUnits: false,
    depthTest: true,
    depthWrite: false,
  });
  material.resolution.set(
    Math.max(viewportWidth, 1),
    Math.max(viewportHeight, 1),
  );

  const lines = new LineSegments2(lineGeo, material);
  lines.renderOrder = 1;
  lines.userData.kind = 'edges';
  return lines;
}

function buildSelectionHighlight(
  group: THREE.Group,
  mapping: SourceMapping,
  nodeId: string,
  viewportWidth: number,
  viewportHeight: number,
): LineSegments2 | null {
  const mesh = collectPreviewMeshes(group)[0];
  if (!mesh) return null;
  const index = mesh.geometry.getIndex();
  const position = mesh.geometry.getAttribute('position');
  if (!index || !position) return null;

  // Root-scope cooks flatten subgraph ids to "instance/inner"; interior cooks
  // emit unprefixed ids. Exact match covers both, the prefix form covers a
  // selected subgraph instance highlighting all of its interior output.
  const matched = new Set<number>();
  for (let i = 0; i < mapping.sourceNodes.length; i++) {
    const entry = mapping.sourceNodes[i];
    if (entry === nodeId || entry.startsWith(`${nodeId}/`)) matched.add(i);
  }
  if (matched.size === 0) return null;

  const edgeKeys = new Set<string>();
  const addEdge = (a: number, b: number) => {
    edgeKeys.add(a < b ? `${a}:${b}` : `${b}:${a}`);
  };
  const triangleCount = Math.min(mapping.triangleSources.length, Math.floor(index.count / 3));
  for (let t = 0; t < triangleCount; t++) {
    if (!matched.has(mapping.triangleSources[t])) continue;
    const a = index.getX(t * 3);
    const b = index.getX(t * 3 + 1);
    const c = index.getX(t * 3 + 2);
    addEdge(a, b);
    addEdge(b, c);
    addEdge(c, a);
  }
  if (edgeKeys.size === 0) return null;

  const segmentPositions = new Float32Array(edgeKeys.size * 6);
  let o = 0;
  for (const key of edgeKeys) {
    const sep = key.indexOf(':');
    const a = Number(key.slice(0, sep));
    const b = Number(key.slice(sep + 1));
    segmentPositions[o++] = position.getX(a);
    segmentPositions[o++] = position.getY(a);
    segmentPositions[o++] = position.getZ(a);
    segmentPositions[o++] = position.getX(b);
    segmentPositions[o++] = position.getY(b);
    segmentPositions[o++] = position.getZ(b);
  }

  const lineGeo = new LineSegmentsGeometry();
  lineGeo.setPositions(segmentPositions);

  const material = new LineMaterial({
    color: SELECTION_HIGHLIGHT_COLOR,
    linewidth: SELECTION_HIGHLIGHT_LINE_WIDTH,
    worldUnits: false,
    depthTest: true,
    depthWrite: false,
  });
  material.resolution.set(
    Math.max(viewportWidth, 1),
    Math.max(viewportHeight, 1),
  );

  const lines = new LineSegments2(lineGeo, material);
  lines.renderOrder = 2;
  lines.userData.kind = 'selection';
  return lines;
}

function syncEdgeOverlayResolution(group: THREE.Group, width: number, height: number) {
  for (const child of group.children) {
    const kind = child.userData.kind;
    if (kind !== 'edges' && kind !== 'selection') continue;
    if (!(child instanceof LineSegments2)) continue;
    const material = child.material as LineMaterial;
    material.resolution.set(Math.max(width, 1), Math.max(height, 1));
  }
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
  const primary = data.geometry?.positions ?? data.mesh?.positions ?? data.scatterPoints;
  let scalarCount = primary?.length ?? 0;
  if (data.splines) {
    for (const spline of data.splines.splines) scalarCount += spline.points.length;
  }
  scalarCount += (controlPoints?.length ?? 0) * 3;

  const result = new Float32Array(scalarCount);
  let offset = 0;
  const appendFlipped = (source: Float32Array) => {
    for (let i = 0; i < source.length; i += 3) {
      result[offset++] = source[i];
      result[offset++] = source[i + 1];
      result[offset++] = -source[i + 2];
    }
  };
  if (primary) appendFlipped(primary);
  if (data.splines) {
    for (const spline of data.splines.splines) appendFlipped(spline.points);
  }
  if (controlPoints) {
    for (const point of controlPoints) {
      const transformed = unityToThree(point);
      result[offset++] = transformed.x;
      result[offset++] = transformed.y;
      result[offset++] = transformed.z;
    }
  }
  return result;
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
  group.traverse((child) => {
    if (child === group) return;
    if (
      child instanceof THREE.Mesh ||
      child instanceof THREE.Points ||
      child instanceof THREE.LineSegments ||
      child instanceof THREE.Line ||
      child instanceof LineSegments2
    ) {
      child.geometry.dispose();
      const material = child.material as THREE.Material | THREE.Material[];
      if (Array.isArray(material)) material.forEach((m) => m.dispose());
      else material.dispose();
    }
  });
}

function collectPreviewMeshes(group: THREE.Group): THREE.Mesh[] {
  const meshes: THREE.Mesh[] = [];
  group.traverse((child) => {
    if (child instanceof THREE.Mesh && child.userData.kind === 'mesh') meshes.push(child);
  });
  return meshes;
}
