import {
  defaultPhysicalCamera,
  mergeCameraCommand,
  type PhysicalCameraState,
} from './physicalCamera';

export const CAMERA_PRESET_CATEGORIES = ['cinema', 'photo'] as const;
export type CameraPresetCategory = (typeof CAMERA_PRESET_CATEGORIES)[number];

export interface CameraPreset {
  id: string;
  name: string;
  category: CameraPresetCategory;
  sensorWidthMm: number;
  sensorHeightMm: number;
  defaultFocalLengthMm: number;
  notes?: string;
}

export const DEFAULT_CAMERA_PRESET_ID = 'full_frame';

export const CAMERA_PRESET_CATEGORY_LABELS: Record<CameraPresetCategory, string> = {
  cinema: 'Cinema',
  photo: 'Photo',
};

export const CAMERA_PRESETS: readonly CameraPreset[] = [
  {
    id: 'arri_alexa_35',
    name: 'ARRI Alexa 35',
    category: 'cinema',
    sensorWidthMm: 27.99,
    sensorHeightMm: 19.22,
    defaultFocalLengthMm: 32,
    notes: 'Super 35 4.6K open gate',
  },
  {
    id: 'arri_alexa_mini_lf',
    name: 'ARRI Alexa Mini LF',
    category: 'cinema',
    sensorWidthMm: 36.70,
    sensorHeightMm: 25.54,
    defaultFocalLengthMm: 35,
    notes: 'Large Format open gate',
  },
  {
    id: 'arri_alexa_65',
    name: 'ARRI Alexa 65',
    category: 'cinema',
    sensorWidthMm: 54.12,
    sensorHeightMm: 25.58,
    defaultFocalLengthMm: 50,
    notes: '65 mm open gate',
  },
  {
    id: 'red_v_raptor_8k_vv',
    name: 'RED V-Raptor 8K VV',
    category: 'cinema',
    sensorWidthMm: 40.96,
    sensorHeightMm: 21.60,
    defaultFocalLengthMm: 40,
    notes: 'VistaVision 8K',
  },
  {
    id: 'red_komodo_6k',
    name: 'RED Komodo 6K',
    category: 'cinema',
    sensorWidthMm: 27.03,
    sensorHeightMm: 14.26,
    defaultFocalLengthMm: 32,
    notes: 'Super 35 6K',
  },
  {
    id: 'sony_venice_2',
    name: 'Sony Venice 2',
    category: 'cinema',
    sensorWidthMm: 36.2,
    sensorHeightMm: 24.1,
    defaultFocalLengthMm: 35,
    notes: 'Full Frame 8.6K',
  },
  {
    id: 'blackmagic_ursa_12k',
    name: 'Blackmagic URSA Mini Pro 12K',
    category: 'cinema',
    sensorWidthMm: 27.03,
    sensorHeightMm: 14.25,
    defaultFocalLengthMm: 32,
    notes: 'Super 35 12K',
  },
  {
    id: 'blackmagic_pocket_6k_g2',
    name: 'Blackmagic Pocket 6K G2',
    category: 'cinema',
    sensorWidthMm: 23.10,
    sensorHeightMm: 12.97,
    defaultFocalLengthMm: 25,
    notes: 'Super 35 6K',
  },
  {
    id: 'full_frame',
    name: 'Full Frame',
    category: 'photo',
    sensorWidthMm: 36,
    sensorHeightMm: 24,
    defaultFocalLengthMm: 26,
    notes: '36×24 stills / cinema FF',
  },
  {
    id: 'super_35',
    name: 'Super 35',
    category: 'photo',
    sensorWidthMm: 24.89,
    sensorHeightMm: 18.66,
    defaultFocalLengthMm: 32,
    notes: 'Academy / production S35',
  },
  {
    id: 'aps_c',
    name: 'APS-C',
    category: 'photo',
    sensorWidthMm: 23.6,
    sensorHeightMm: 15.6,
    defaultFocalLengthMm: 23,
    notes: 'Sony / Nikon APS-C',
  },
  {
    id: 'canon_aps_c',
    name: 'Canon APS-C',
    category: 'photo',
    sensorWidthMm: 22.3,
    sensorHeightMm: 14.9,
    defaultFocalLengthMm: 22,
    notes: 'Canon crop',
  },
  {
    id: 'micro_four_thirds',
    name: 'Micro Four Thirds',
    category: 'photo',
    sensorWidthMm: 17.3,
    sensorHeightMm: 13.0,
    defaultFocalLengthMm: 17,
    notes: 'MFT stills',
  },
  {
    id: 'medium_format',
    name: 'Medium Format',
    category: 'photo',
    sensorWidthMm: 44,
    sensorHeightMm: 33,
    defaultFocalLengthMm: 45,
    notes: 'GFX / X2D 44×33',
  },
  {
    id: 'film_16mm',
    name: '16mm',
    category: 'photo',
    sensorWidthMm: 10.26,
    sensorHeightMm: 7.49,
    defaultFocalLengthMm: 12,
    notes: '16mm film gate',
  },
];

const presetById = new Map(CAMERA_PRESETS.map((preset) => [preset.id, preset]));

export function getCameraPreset(id: string | null | undefined): CameraPreset | undefined {
  if (!id) return undefined;
  return presetById.get(id);
}

export function getCameraPresetsByCategory(): Map<string, CameraPreset[]> {
  const grouped = new Map<string, CameraPreset[]>();
  for (const category of CAMERA_PRESET_CATEGORIES) {
    grouped.set(
      CAMERA_PRESET_CATEGORY_LABELS[category],
      CAMERA_PRESETS.filter((preset) => preset.category === category),
    );
  }
  return grouped;
}

export function inferCameraPresetId(
  state: Pick<PhysicalCameraState, 'sensorWidthMm' | 'sensorHeightMm'>,
): string {
  let bestId = DEFAULT_CAMERA_PRESET_ID;
  let bestDist = Number.POSITIVE_INFINITY;
  for (const preset of CAMERA_PRESETS) {
    const widthDelta = preset.sensorWidthMm - state.sensorWidthMm;
    const heightDelta = preset.sensorHeightMm - state.sensorHeightMm;
    const dist = widthDelta * widthDelta + heightDelta * heightDelta;
    if (dist < bestDist) {
      bestDist = dist;
      bestId = preset.id;
    }
  }
  return bestDist > 0.25 ? DEFAULT_CAMERA_PRESET_ID : bestId;
}

export function resolveCameraPresetId(
  presetId: unknown,
  state: Pick<PhysicalCameraState, 'sensorWidthMm' | 'sensorHeightMm'>,
): string {
  return getCameraPreset(typeof presetId === 'string' ? presetId : '')?.id
    ?? inferCameraPresetId(state);
}

/** Writes sensor size, auto fit, and the preset default lens. Pose, keys, aperture, and DOF stay. */
export function applyCameraPreset(
  state: PhysicalCameraState,
  presetId: string,
): PhysicalCameraState {
  const preset = getCameraPreset(presetId);
  if (!preset) return state;
  return mergeCameraCommand(state, {
    sensorWidthMm: preset.sensorWidthMm,
    sensorHeightMm: preset.sensorHeightMm,
    sensorFit: 'auto',
    focalLengthMm: preset.defaultFocalLengthMm,
  });
}

export function cameraFromPreset(presetId = DEFAULT_CAMERA_PRESET_ID): PhysicalCameraState {
  const preset = getCameraPreset(presetId) ?? getCameraPreset(DEFAULT_CAMERA_PRESET_ID)!;
  return applyCameraPreset(defaultPhysicalCamera(), preset.id);
}
