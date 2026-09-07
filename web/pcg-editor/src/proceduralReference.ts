import type {
  CaptureRenderPass,
  CaptureOptions,
  PreviewCapture,
  PreviewViewportHandle,
} from './PreviewViewport';
import type { CameraCommand, PhysicalCameraState } from './physicalCamera';
import type { MeshBandProfile } from './meshBandProfile';

export const PROCEDURAL_REFERENCE_VIEWS = [
  { id: 'azimuth-000', azimuth: 0, elevation: 10 },
  { id: 'azimuth-p035', azimuth: 35, elevation: 10 },
  { id: 'azimuth-m035', azimuth: -35, elevation: 10 },
  { id: 'azimuth-p090', azimuth: 90, elevation: 10 },
  { id: 'azimuth-p145', azimuth: 145, elevation: 10 },
  { id: 'azimuth-p180', azimuth: 180, elevation: 10 },
] as const;

export const PROCEDURAL_REFERENCE_PASSES = [
  'beauty',
  'alpha-silhouette',
  'semantic-id',
  'depth',
  'normal',
  'roughness-material-id',
] as const satisfies readonly CaptureRenderPass[];

const CAPTURE_SIZE = 1024;
const MIN_CAPTURE_BYTES = 1024;
const PNG_SIGNATURE = [137, 80, 78, 71, 13, 10, 26, 10] as const;

export interface ProceduralReferenceSource {
  nodeId: string;
  referencePath: string;
  bakedSurfaceNodeId?: string;
  bakedMaterialNodeId?: string;
  sourceImage?: string;
  sourceImageFile?: File | null;
  graphPath?: string;
}

export interface AgentLaunchRequest {
  id: string;
  message: string;
  files: File[];
}

export interface ProceduralReferenceManifest {
  schemaVersion: 'pcg-procedural-reference/v1';
  capturedAt: string;
  reference: {
    kind: 'tripo-glb';
    nodeId: string;
    /** Native topology-free reconstruction node pre-baked from the reference. */
    bakedSurfaceNodeId: string | null;
    /** Material node preloaded with embedded GLB PBR images when available. */
    bakedMaterialNodeId: string | null;
    path: string;
    /** Cache key encoded in the local filename; it is not the GLB content hash. */
    cacheKey: string | null;
    /** Filled only when a trusted probe supplies it; capture alone cannot infer it. */
    contentSha256: null;
    sourceImage: string | null;
    sourceImageFile: string | null;
    graphPath: string | null;
    semanticLabels: 'unverified';
    /** Quantitative GLB evidence; copied topology is never part of this handoff. */
    meshBandProfile: MeshBandProfile | null;
  };
  captureProfile: {
    renderer: 'pcg-web-preview';
    shadingMode: 'material';
    passes: readonly CaptureRenderPass[];
    width: number;
    height: number;
    devicePixelRatio: 1;
    sharedCamera: {
      target: [number, number, number];
      distance: number;
      projection: PhysicalCameraState['projection'];
      focalLengthMm: number;
      sensorWidthMm: number;
      sensorHeightMm: number;
      sensorFit: PhysicalCameraState['sensorFit'];
      shiftX: number;
      shiftY: number;
      exposure: number;
      near: number;
      far: number;
      orthographicScale: number;
    };
  };
  views: Array<{
    id: string;
    file: string;
    azimuth: number;
    elevation: number;
    camera: PreviewCapture['metadata']['camera'];
    passes: Array<{
      pass: CaptureRenderPass;
      file: string;
      diagnosticLegend: PreviewCapture['metadata']['diagnosticLegend'];
    }>;
  }>;
  targetGraphPath: string;
}

type CaptureAdapter = Pick<
  PreviewViewportHandle,
  'captureFrame' | 'applyCameraCommand' | 'getCameraState'
>;

function cameraDistance(state: PhysicalCameraState): number {
  const dx = state.position[0] - state.target[0];
  const dy = state.position[1] - state.target[1];
  const dz = state.position[2] - state.target[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function exactCameraCommand(state: PhysicalCameraState): CameraCommand {
  return {
    position: [...state.position],
    target: [...state.target],
    up: [...state.up],
    projection: state.projection,
    focalLengthMm: state.focalLengthMm,
    sensorWidthMm: state.sensorWidthMm,
    sensorHeightMm: state.sensorHeightMm,
    sensorFit: state.sensorFit,
    shiftX: state.shiftX,
    shiftY: state.shiftY,
    apertureFstop: state.apertureFstop,
    apertureBlades: state.apertureBlades,
    apertureRotationDeg: state.apertureRotationDeg,
    apertureRatio: state.apertureRatio,
    focusDistance: state.focusDistance,
    dofEnabled: state.dofEnabled,
    exposure: state.exposure,
    near: state.near,
    far: state.far,
    orthographicScale: state.orthographicScale,
  };
}

function viewCameraCommand(
  state: PhysicalCameraState,
  view: (typeof PROCEDURAL_REFERENCE_VIEWS)[number],
  distance: number,
): CameraCommand {
  return {
    target: [...state.target],
    up: [...state.up],
    azimuth: view.azimuth,
    elevation: view.elevation,
    distance,
    projection: state.projection,
    focalLengthMm: state.focalLengthMm,
    sensorWidthMm: state.sensorWidthMm,
    sensorHeightMm: state.sensorHeightMm,
    sensorFit: state.sensorFit,
    shiftX: state.shiftX,
    shiftY: state.shiftY,
    apertureFstop: state.apertureFstop,
    apertureBlades: state.apertureBlades,
    apertureRotationDeg: state.apertureRotationDeg,
    apertureRatio: state.apertureRatio,
    focusDistance: distance,
    dofEnabled: false,
    exposure: state.exposure,
    near: state.near,
    far: state.far,
    orthographicScale: state.orthographicScale,
  };
}

function decodePng(pngBase64: string, viewId: string): ArrayBuffer {
  let binary: string;
  try {
    binary = atob(pngBase64);
  } catch {
    throw new Error(`Reference capture ${viewId} is not valid base64.`);
  }
  const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
  const hasSignature = PNG_SIGNATURE.every((value, index) => bytes[index] === value);
  if (!hasSignature || bytes.byteLength < MIN_CAPTURE_BYTES) {
    throw new Error(`Reference capture ${viewId} is empty or unreadable.`);
  }
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
}

function referenceCacheKey(path: string): string | null {
  const filename = path.split('/').pop() ?? '';
  const match = filename.match(/^([a-f0-9]{64})\.glb$/i);
  return match ? match[1].toLowerCase() : null;
}

function targetGraphPath(path: string): string {
  const cacheKey = referenceCacheKey(path);
  const stem = cacheKey?.slice(0, 12) ?? 'reference';
  return `examples/tripo-${stem}-procedural.pcg`;
}

export function buildProceduralReferencePrompt(manifest: ProceduralReferenceManifest): string {
  const viewList = manifest.views
    .map((view) => `${view.id}: azimuth ${view.azimuth}°, elevation ${view.elevation}°`)
    .join('; ');
  return [
    '执行 Tripo GLB-mediated 程序化重建。附件 PNG 是 PCG Web 预览器刚刚从参考 GLB 采集的固定机位、多诊断通道基准，JSON 是同批次的相机、pass 与来源清单。',
    '',
    `参考 GLB：${manifest.reference.path}`,
    `源图证据：${manifest.reference.sourceImageFile ?? manifest.reference.sourceImage ?? '未提供'}`,
    `建议输出：${manifest.targetGraphPath}`,
    `视图：${viewList}`,
    '',
    '硬约束：',
    '0. 先逐张检查参考准入；若出现多个重复主体、角色板/拼图残留、裁切或不可读视图，停止写图并要求用干净的单主体图重新生成 GLB。',
    `1. 参考 GLB 只用于结构和视觉基准；不得复制源 faces/indices 或直接交付 ImportMesh/Tripo3DGenerator。允许并优先使用已烘焙的 OrientedSdfSurface 节点（${manifest.reference.bakedSurfaceNodeId ?? '未预烘焙'}）：其 OPC1 仅保存量化采样位置、定向法线和可选颜色，不含源拓扑，并在 cook 时以稀疏 MLS-SDF + Surface Nets 生成全新拓扑。`,
    `1.1 已提取的 PBR 材质节点：${manifest.reference.bakedMaterialNodeId ?? '无'}。它只保存 GLB 内嵌纹理数据，不携带源网格拓扑；先验证 UV/颜色对应关系，再接入 AssignMaterial。`,
    '2. 该来源的语义标签未经验证。除非实时清单证明为多部件，否则把区域命名当作视觉假设并明确记录。',
    '2.1 颜色、花纹和配饰以源图证据为主；GLB 截图若因导入链未携带贴图而呈白色，不得据此把程序化材质误判为全白。',
    '3. 先调用 pcg_get_editor_context，检索项目 PCG KB 的 graph-contract、assembly-bevel 与对应资产类型规则，再按精确类型调用 pcg_get_node_types；不得臆造节点、属性或 pin。',
    '4. 先写 AssetSpec：比例、连续主体体积、语义部件、连接关系、材质、SDF cellSize/support/isoOffset、参数与确定性 seed；随后只通过 PCG MCP 在当前 Web 编辑器中创建独立、可编辑的程序化图。',
    '5. 每次结构写入后执行 Houdini 风格 top-down 布局；主链同 X、行距约 160、同层兄弟横距至少 320，并保存命名图。',
    '6. 运行 pcg_validate、pcg_cook、pcg_capture_preview；程序化结果必须复用 JSON 中完全相同的 target、distance、lens、near/far、曝光、六个方位和 beauty / alpha-silhouette / semantic-id / depth / normal / roughness-material-id 六种 renderPass。',
    '6.1 JSON 的 reference.meshBandProfile 是参考 GLB 的脚底对齐、身高归一化定量轮廓；候选的同构数据位于每次 pcg_capture_preview 返回的 metadata.meshBandProfile。逐高度带比较 widthP90、depthP90、centroidX、centroidZ，以定位比例和错位，但不得用单一总分替代视觉验收。',
    '7. 按 camera → silhouette → subject-defining forms → accessories → materials → lighting 的顺序，每轮只修一组并重采完整视图。连续主体必须以 OrientedSdfSurface 为高保真基底；TaperedSweep/其他节点只用于可解释修正或独立配饰，禁止退化成基础几何体拼装。一般确定性点变形可用 AttributeWrangle，禁止执行任意 JavaScript。',
    '7.1 使用完全相同相机对 alpha-silhouette 做像素门禁：逐视图 silhouette IoU 目标 ≥ 0.98、轮廓双向距离 P95 ≤ 2 px；同时报告 beauty 交集 RGB 误差与 Laplacian 噪声。未过门禁不得声称像素级或 FINAL_ACCEPTED。',
    '8. 交付必须包含命名语义部件、可调参数、稳定 seed、真实连接和完整材质；不要停在白模。一直迭代到 FINAL_ACCEPTED，或只在真实能力 GAP 时停止。',
  ].join('\n');
}

export function captureProceduralReferenceBundle(
  viewport: CaptureAdapter,
  source: ProceduralReferenceSource,
  options: { requestId?: string; capturedAt?: string } = {},
): { request: AgentLaunchRequest; manifest: ProceduralReferenceManifest } {
  const initialCamera = viewport.getCameraState();
  if (!initialCamera) throw new Error('3D Preview camera is not ready.');
  const distance = cameraDistance(initialCamera);
  if (!Number.isFinite(distance) || distance <= 0.001) {
    throw new Error('3D Preview camera has an invalid framing distance.');
  }

  const imageFiles: File[] = [];
  const views: ProceduralReferenceManifest['views'] = [];
  let meshBandProfile: MeshBandProfile | null = null;
  try {
    for (const view of PROCEDURAL_REFERENCE_VIEWS) {
      const camera = viewCameraCommand(initialCamera, view, distance);
      const capturedPasses: ProceduralReferenceManifest['views'][number]['passes'] = [];
      let beautyCapture: PreviewCapture | null = null;
      for (const renderPass of PROCEDURAL_REFERENCE_PASSES) {
        const captureOptions: CaptureOptions = {
          width: CAPTURE_SIZE,
          height: CAPTURE_SIZE,
          transparent: renderPass === 'alpha-silhouette',
          dof: false,
          shadingMode: 'material',
          renderPass,
          camera,
        };
        const capture = viewport.captureFrame(captureOptions);
        if (!capture) throw new Error(`Reference capture ${view.id}/${renderPass} did not render.`);
        const bytes = decodePng(capture.pngBase64, `${view.id}/${renderPass}`);
        const filename = `tripo-reference-${view.id}-${renderPass}.png`;
        imageFiles.push(new File([bytes], filename, { type: 'image/png' }));
        meshBandProfile ??= capture.metadata.meshBandProfile ?? null;
        if (renderPass === 'beauty') beautyCapture = capture;
        capturedPasses.push({
          pass: renderPass,
          file: filename,
          diagnosticLegend: capture.metadata.diagnosticLegend ?? null,
        });
      }
      if (!beautyCapture) throw new Error(`Reference beauty capture ${view.id} did not render.`);
      views.push({
        id: view.id,
        file: `tripo-reference-${view.id}-beauty.png`,
        azimuth: view.azimuth,
        elevation: view.elevation,
        camera: beautyCapture.metadata.camera,
        passes: capturedPasses,
      });
    }
  } finally {
    viewport.applyCameraCommand(exactCameraCommand(initialCamera));
  }

  const manifest: ProceduralReferenceManifest = {
    schemaVersion: 'pcg-procedural-reference/v1',
    capturedAt: options.capturedAt ?? new Date().toISOString(),
    reference: {
      kind: 'tripo-glb',
      nodeId: source.nodeId,
      bakedSurfaceNodeId: source.bakedSurfaceNodeId?.trim() || null,
      bakedMaterialNodeId: source.bakedMaterialNodeId?.trim() || null,
      path: source.referencePath,
      cacheKey: referenceCacheKey(source.referencePath),
      contentSha256: null,
      sourceImage: source.sourceImage?.trim() || null,
      sourceImageFile: source.sourceImageFile?.name ?? null,
      graphPath: source.graphPath?.trim() || null,
      semanticLabels: 'unverified',
      meshBandProfile,
    },
    captureProfile: {
      renderer: 'pcg-web-preview',
      shadingMode: 'material',
      passes: PROCEDURAL_REFERENCE_PASSES,
      width: CAPTURE_SIZE,
      height: CAPTURE_SIZE,
      devicePixelRatio: 1,
      sharedCamera: {
        target: [...initialCamera.target],
        distance,
        projection: initialCamera.projection,
        focalLengthMm: initialCamera.focalLengthMm,
        sensorWidthMm: initialCamera.sensorWidthMm,
        sensorHeightMm: initialCamera.sensorHeightMm,
        sensorFit: initialCamera.sensorFit,
        shiftX: initialCamera.shiftX,
        shiftY: initialCamera.shiftY,
        exposure: initialCamera.exposure,
        near: initialCamera.near,
        far: initialCamera.far,
        orthographicScale: initialCamera.orthographicScale,
      },
    },
    views,
    targetGraphPath: targetGraphPath(source.referencePath),
  };
  const manifestFile = new File(
    [JSON.stringify(manifest, null, 2)],
    'tripo-reference-manifest.json',
    { type: 'application/json' },
  );
  return {
    manifest,
    request: {
      id: options.requestId ?? crypto.randomUUID(),
      message: buildProceduralReferencePrompt(manifest),
      files: [
        ...imageFiles,
        ...(source.sourceImageFile ? [source.sourceImageFile] : []),
        manifestFile,
      ],
    },
  };
}

function sourceImageFetchUrl(storage: string): string | null {
  const value = storage.trim();
  if (!value) return null;
  if (value.toLowerCase().startsWith('pcg-resource://')) {
    const key = value.slice('pcg-resource://'.length).replace(/^\/+/, '');
    return key ? `/assets/${key}` : null;
  }
  if (value.startsWith('/assets/') || /^https?:\/\//i.test(value) || value.startsWith('data:')) {
    return value;
  }
  return null;
}

/** Best-effort source-image attachment. Workspace-relative paths stay in the manifest for the Agent to read. */
export async function loadProceduralSourceImageFile(storage: string): Promise<File | null> {
  const url = sourceImageFetchUrl(storage);
  if (!url) return null;
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Could not attach source image (HTTP ${response.status}).`);
  const blob = await response.blob();
  if (!blob.type.startsWith('image/')) throw new Error('Source image response is not an image.');
  const extension = blob.type === 'image/jpeg' ? 'jpg'
    : blob.type === 'image/webp' ? 'webp'
      : 'png';
  return new File([blob], `tripo-source-reference.${extension}`, { type: blob.type });
}
