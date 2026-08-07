// roundtrip-check.ts — Web import→export round-trip regression entry.
// Bundled by scripts/validate-web-roundtrip.py with rolldown and run under node.
// Verifies that every field present in the golden .pcg survives
// parseGraphJson → exportGraph (golden ⊆ exported, key-by-key).

import { readFileSync } from 'node:fs';
import { parseGraphJson } from '../src/importGraph';
import { exportGraph } from '../src/exportGraph';

const fixturePath = process.argv[2];
if (!fixturePath) {
  console.error('Usage: node roundtrip-check.mjs <fixture.pcg>');
  process.exit(2);
}

const text = readFileSync(fixturePath, 'utf8');
const golden = JSON.parse(text) as unknown;

const result = parseGraphJson(text);
if (!result.ok) {
  console.error(`IMPORT FAILED: ${result.error}`);
  process.exit(1);
}

const exported = exportGraph(result.nodes, result.edges, result.parameters, result.subgraphs);

const failures: string[] = [];

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function assertSubset(path: string, goldenValue: unknown, exportedValue: unknown): void {
  if (Array.isArray(goldenValue)) {
    if (!Array.isArray(exportedValue)) {
      failures.push(`${path}: expected array, got ${typeof exportedValue}`);
      return;
    }
    const byId = goldenValue.every(
      (item) => isRecord(item) && typeof item.id === 'string',
    );
    goldenValue.forEach((item, index) => {
      let candidate: unknown;
      if (byId && isRecord(item)) {
        candidate = (exportedValue as unknown[]).find(
          (other) => isRecord(other) && other.id === item.id,
        );
        if (candidate === undefined) {
          failures.push(`${path}: missing array element with id "${String(item.id)}"`);
          return;
        }
      } else {
        candidate = (exportedValue as unknown[])[index];
        if (index >= (exportedValue as unknown[]).length) {
          failures.push(`${path}[${index}]: missing array element`);
          return;
        }
      }
      assertSubset(byId && isRecord(item) ? `${path}[id=${String(item.id)}]` : `${path}[${index}]`, item, candidate);
    });
    return;
  }
  if (isRecord(goldenValue)) {
    if (!isRecord(exportedValue)) {
      failures.push(`${path}: expected object, got ${typeof exportedValue}`);
      return;
    }
    for (const [key, value] of Object.entries(goldenValue)) {
      if (!(key in exportedValue)) {
        failures.push(`${path}.${key}: key dropped`);
        continue;
      }
      assertSubset(`${path}.${key}`, value, exportedValue[key]);
    }
    return;
  }
  if (goldenValue !== exportedValue) {
    failures.push(`${path}: value changed ${JSON.stringify(goldenValue)} -> ${JSON.stringify(exportedValue)}`);
  }
}

assertSubset('$', golden, exported);

if (failures.length > 0) {
  console.error(`ROUND-TRIP FAILED (${failures.length} difference(s)):`);
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}

console.log(`Round-trip OK: ${fixturePath} (golden ⊆ exported, no field loss)`);
