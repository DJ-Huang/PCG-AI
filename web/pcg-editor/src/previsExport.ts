import {
  BufferTarget,
  CanvasSource,
  Mp4OutputFormat,
  Output,
  Quality,
  WebMOutputFormat,
  canEncodeVideo,
  type VideoCodec,
} from 'mediabunny';

import type { PreviewViewportHandle } from './PreviewViewport';
import type { ShotDocument } from './shot';

export interface PrevisCodecInfo {
  codec: Extract<VideoCodec, 'avc' | 'vp9'>;
  label: 'H.264';
  container: 'mp4';
  extension: 'mp4';
  mimeType: 'video/mp4';
}

export interface PrevisWebmCodecInfo {
  codec: Extract<VideoCodec, 'vp9'>;
  label: 'VP9';
  container: 'webm';
  extension: 'webm';
  mimeType: 'video/webm';
}

export type ResolvedPrevisCodec = PrevisCodecInfo | PrevisWebmCodecInfo;

export interface PrevisExportProgress {
  frame: number;
  frameCount: number;
  progress: number;
}

export interface PrevisExportResult {
  blob: Blob;
  codec: ResolvedPrevisCodec;
  frameCount: number;
}

type PrevisFrameSource = Pick<
  PreviewViewportHandle,
  'captureFrame' | 'seekShot' | 'getShotTime'
>;

export function previsFrameCount(shot: Pick<ShotDocument, 'durationSeconds' | 'fps'>): number {
  return Math.max(1, Math.round(shot.durationSeconds * shot.fps));
}

export function previsFrameTimes(
  shot: Pick<ShotDocument, 'durationSeconds' | 'fps'>,
): number[] {
  return Array.from({ length: previsFrameCount(shot) }, (_, frame) => frame / shot.fps);
}

export async function resolvePrevisCodec(width: number, height: number): Promise<ResolvedPrevisCodec> {
  if (typeof VideoEncoder === 'undefined') {
    throw new Error('WebCodecs VideoEncoder is unavailable in this browser.');
  }
  if (await canEncodeVideo('avc', { width, height, latencyMode: 'quality' })) {
    return {
      codec: 'avc',
      label: 'H.264',
      container: 'mp4',
      extension: 'mp4',
      mimeType: 'video/mp4',
    };
  }
  if (await canEncodeVideo('vp9', { width, height, latencyMode: 'quality' })) {
    return {
      codec: 'vp9',
      label: 'VP9',
      container: 'webm',
      extension: 'webm',
      mimeType: 'video/webm',
    };
  }
  throw new Error('This browser cannot encode H.264 or VP9 with WebCodecs.');
}

function throwIfAborted(signal?: AbortSignal) {
  if (!signal?.aborted) return;
  throw signal.reason instanceof Error
    ? signal.reason
    : new DOMException('Previs export canceled.', 'AbortError');
}

function pngBytes(pngBase64: string): Uint8Array<ArrayBuffer> {
  const binary = atob(pngBase64);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}

export async function exportPrevisVideo(
  source: PrevisFrameSource,
  shot: ShotDocument,
  options: {
    codec?: ResolvedPrevisCodec;
    signal?: AbortSignal;
    onProgress?: (progress: PrevisExportProgress) => void;
  } = {},
): Promise<PrevisExportResult> {
  const codec = options.codec ?? await resolvePrevisCodec(shot.width, shot.height);
  const target = new BufferTarget();
  const format = codec.container === 'mp4'
    ? new Mp4OutputFormat({ fastStart: 'in-memory' })
    : new WebMOutputFormat();
  const output = new Output({ format, target });
  const canvas = document.createElement('canvas');
  canvas.width = shot.width;
  canvas.height = shot.height;
  const context = canvas.getContext('2d', { alpha: false });
  if (!context) throw new Error('Unable to create the previs export canvas.');
  const video = new CanvasSource(canvas, {
    codec: codec.codec,
    quality: new Quality('high'),
    keyFrameInterval: 2,
    latencyMode: 'quality',
    alpha: 'discard',
  });
  const frameTimes = previsFrameTimes(shot);
  output.addVideoTrack(video, { maximumPacketCount: frameTimes.length });
  const previousTime = source.getShotTime();

  try {
    throwIfAborted(options.signal);
    await output.start();
    for (let frame = 0; frame < frameTimes.length; frame++) {
      throwIfAborted(options.signal);
      const timeSeconds = frameTimes[frame];
      if (!source.seekShot(shot, timeSeconds)) {
        throw new Error('The preview scene is unavailable for previs export.');
      }
      const capture = source.captureFrame({
        width: shot.width,
        height: shot.height,
        renderPass: 'beauty',
      });
      if (!capture) throw new Error(`Failed to render previs frame ${frame + 1}.`);
      const bitmap = await createImageBitmap(new Blob(
        [pngBytes(capture.pngBase64)],
        { type: 'image/png' },
      ));
      try {
        context.clearRect(0, 0, shot.width, shot.height);
        context.drawImage(bitmap, 0, 0, shot.width, shot.height);
      } finally {
        bitmap.close();
      }
      throwIfAborted(options.signal);
      await video.add(timeSeconds, 1 / shot.fps, {
        keyFrame: frame % Math.max(1, Math.round(shot.fps * 2)) === 0,
      });
      options.onProgress?.({
        frame: frame + 1,
        frameCount: frameTimes.length,
        progress: (frame + 1) / frameTimes.length,
      });
    }
    video.close();
    await output.finalize();
    if (!target.buffer) throw new Error('Video encoder produced no output.');
    return {
      blob: new Blob([target.buffer], { type: codec.mimeType }),
      codec,
      frameCount: frameTimes.length,
    };
  } catch (error) {
    video.close();
    if (output.state !== 'finalized' && output.state !== 'canceled') {
      await output.cancel();
    }
    throw error;
  } finally {
    source.seekShot(shot, Math.min(previousTime, shot.durationSeconds));
    canvas.width = 0;
    canvas.height = 0;
  }
}
