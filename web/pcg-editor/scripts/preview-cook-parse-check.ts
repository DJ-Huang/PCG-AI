// preview-cook-parse-check.ts — malformed heightfield preview errors.
// Run: npx tsx scripts/preview-cook-parse-check.ts

import { PcgExecuteKind, type CookResult } from '../src/cookResult';
import { buildPreviewDataFromCook } from '../src/previewCook';

function makeCook(overrides: Partial<CookResult>): CookResult {
  return {
    code: 0,
    kind: PcgExecuteKind.Json,
    nodesExecuted: 1,
    nodesSkipped: 0,
    graphExecuteMs: 0,
    binaryWriteMs: 0,
    pointCount: 0,
    pointAttrFlags: 0,
    vertexCount: 0,
    indexCount: 0,
    error: '',
    json: '{"kind":"heightfield"}',
    mesh: new Uint8Array(),
    points: new Uint8Array(),
    geometry: new Uint8Array(),
    heightfield: new Uint8Array(),
    perf: '',
    ...overrides,
  };
}

const failures: string[] = [];
const check = (label: string, ok: boolean) => {
  if (!ok) failures.push(label);
};

const malformed = makeCook({
  heightfield: new Uint8Array([0x50, 0x47, 0x43, 0x48, 99, 0, 0, 0]), // PCGH v99
});
const malformedResult = buildPreviewDataFromCook(malformed);
check(
  'malformed heightfield reports invalid payload',
  malformedResult.ok === false &&
    (malformedResult.error?.startsWith('Invalid heightfield payload:') ?? false),
);

const missing = makeCook({
  json: '{"kind":"heightfield"}',
  heightfield: new Uint8Array(),
});
const missingResult = buildPreviewDataFromCook(missing);
check(
  'missing heightfield keeps missing-binary message',
  missingResult.ok === false &&
    missingResult.error?.includes('heightfield binary is missing') === true,
);

if (failures.length > 0) {
  console.error('PREVIEW COOK PARSE CHECK FAILED:');
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}

console.log('Preview cook parse check OK');
