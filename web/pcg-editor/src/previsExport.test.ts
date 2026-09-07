import { describe, expect, it } from 'vitest';

import { previsFrameCount, previsFrameTimes } from './previsExport';

describe('previsExport', () => {
  it('creates exactly 120 fixed timestamps for a five-second 24fps shot', () => {
    const shot = { durationSeconds: 5, fps: 24 };
    const times = previsFrameTimes(shot);
    expect(previsFrameCount(shot)).toBe(120);
    expect(times).toHaveLength(120);
    expect(times[0]).toBe(0);
    expect(times[119]).toBe(119 / 24);
    expect(times.every((time, frame) => time === frame / 24)).toBe(true);
  });

  it('rounds non-integral duration products deterministically', () => {
    expect(previsFrameCount({ durationSeconds: 1.1, fps: 24 })).toBe(26);
    expect(previsFrameTimes({ durationSeconds: 1.1, fps: 24 })).toHaveLength(26);
  });
});
