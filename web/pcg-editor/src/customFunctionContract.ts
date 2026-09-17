/** Static Custom Function v1 contract. No script parsing, evaluation, imports or I/O.
 * Wire format: ordinary node.data.customFunction; edges use declaration IDs.
 * JSON Schema and integration boundaries: schema/custom-function-contract.md.
 */
export const CUSTOM_FUNCTION_SCHEMA_VERSION = 1;
export const CUSTOM_FUNCTION_API_VERSION = '1';
export const PIN_TYPES = [
  'Any', 'Param', 'SpatialPoint', 'SpatialSpline', 'SpatialSurface',
  'SpatialMesh', 'SpatialGeometry', 'Texture', 'HeightField', 'Material',
] as const;
export type ContractPinType = typeof PIN_TYPES[number];
export type ValueType = 'integer' | 'number' | 'boolean' | 'string' | 'vector3';
export type Literal = number | boolean | string | [number, number, number];
export interface Constraints {
  minimum?: number;
  maximum?: number;
  enum?: Literal[];
  minItems?: number;
  maxItems?: number;
}
export interface ParameterDeclaration {
  id: string;
  label: string;
  type: ValueType;
  required: boolean;
  default?: Literal;
  constraints?: Constraints;
}
export interface PortDeclaration {
  id: string;
  label: string;
  pinType: ContractPinType;
  cardinality: 'single' | 'many';
  required: boolean;
  /** Required for Param; forbidden for native payloads and Any. */
  valueType?: ValueType;
  /** Only input Param ports may persist defaults; never native handles. */
  default?: Literal | Literal[];
  constraints?: Constraints;
}
export interface DependencyDescriptor {
  id: string;
  kind: 'resource' | 'module';
  reference: string;
  version: string;
  contentHash: string;
}
export interface CustomFunctionContract {
  schemaVersion: 1;
  language: 'javascript';
  /** PCG's synchronous module authoring profile, not an ECMAScript year. */
  languageVersion: 1;
  apiVersion: '1';
  entryPoint: string;
  source: string;
  seed: number;
  dependencies: DependencyDescriptor[];
  parameters: ParameterDeclaration[];
  parameterValues: Record<string, Literal>;
  inputs: PortDeclaration[];
  outputs: PortDeclaration[];
  primaryOutput: string | null;
}
export interface Diagnostic {
  code: string;
  severity: 'error' | 'warning';
  path: string;
  message: string;
  nodeId?: string;
  portId?: string;
  parameterId?: string;
  edgeId?: string;
  direction?: 'input' | 'output';
}
export type Result<T> =
  | { ok: true; value: T; diagnostics: Diagnostic[] }
  | { ok: false; diagnostics: Diagnostic[] };

const VALUE_TYPES = ['integer', 'number', 'boolean', 'string', 'vector3'];
const ID = /^[A-Za-z_][A-Za-z0-9_-]{0,63}$/;
const RESERVED = new Set(['__proto__', 'prototype', 'constructor']);
const own = (object: object, key: PropertyKey) => Object.prototype.hasOwnProperty.call(object, key);
const record = (value: unknown): value is Record<string, unknown> => {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
};
const dataMap = (value: unknown): value is Record<string, unknown> => {
  if (!record(value)) return false;
  const descriptors = Object.getOwnPropertyDescriptors(value);
  return Reflect.ownKeys(descriptors).every((key) => typeof key === 'string' &&
    'value' in descriptors[key] && descriptors[key].enumerable);
};
const dataArray = (value: unknown): value is unknown[] => {
  if (!Array.isArray(value) || Object.getPrototypeOf(value) !== Array.prototype) return false;
  const descriptors = Object.getOwnPropertyDescriptors(value);
  const keys = Reflect.ownKeys(descriptors);
  return keys.length === value.length + 1 && keys.every((key) => key === 'length' || (
    typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key) && Number(key) < value.length &&
    'value' in descriptors[key] && descriptors[key].enumerable
  ));
};
const finite = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
const validId = (value: unknown): value is string => typeof value === 'string' && ID.test(value) && !RESERVED.has(value);
const pointer = (key: string) => key.replace(/~/g, '~0').replace(/\//g, '~1');
export const diagnostic = (code: string, path: string, message: string, extra: Partial<Diagnostic> = {}): Diagnostic =>
  ({ code, severity: 'error', path, message, ...extra });
const failure = <T>(diagnostics: Diagnostic[]): Result<T> => ({ ok: false, diagnostics });

/** Reject non-JSON objects/accessors without invoking them. Input is persisted data,
 * never runtime handles. This also prevents silent NaN/undefined loss on save. */
function assertJson(value: unknown, ancestors = new Set<object>(), depth = 0): void {
  if (depth > 64) throw new Error('JSON nesting exceeds 64 levels');
  if (value === null || typeof value === 'string' || typeof value === 'boolean' || finite(value)) return;
  if (typeof value !== 'object' || value === null) throw new Error('Expected finite, JSON-serializable data');
  if (ancestors.has(value)) throw new Error('Cyclic data cannot be persisted');
  if (Array.isArray(value) ? !dataArray(value) : !record(value)) throw new Error('Expected a plain JSON object or dense data array without accessors');
  ancestors.add(value);
  const descriptors = Object.getOwnPropertyDescriptors(value);
  for (const key of Reflect.ownKeys(descriptors)) {
    if (Array.isArray(value) && key === 'length') continue;
    if (typeof key !== 'string') throw new Error('Symbol properties cannot be persisted');
    const descriptor = descriptors[key];
    if (!('value' in descriptor) || !descriptor.enumerable) throw new Error('Accessors/non-enumerable properties cannot be persisted');
    if (Array.isArray(value) && !/^(0|[1-9][0-9]*)$/.test(key)) throw new Error('Array properties cannot be persisted');
    assertJson(descriptor.value, ancestors, depth + 1);
  }
  if (Array.isArray(value) && Object.keys(value).length !== value.length) throw new Error('Sparse arrays cannot be persisted');
  ancestors.delete(value);
}

/** Deterministic serialization of JSON data, not a cryptographic cache hash. */
export function canonicalJson(value: unknown): string {
  assertJson(value);
  const encode = (item: unknown): string => {
    if (Array.isArray(item)) return `[${item.map(encode).join(',')}]`;
    if (record(item)) return `{${Object.keys(item).sort().map((key) => `${JSON.stringify(key)}:${encode(item[key])}`).join(',')}}`;
    return JSON.stringify(item);
  };
  return encode(value);
}
export function cloneJson<T>(value: T): T {
  return JSON.parse(canonicalJson(value)) as T;
}

function literalMatches(value: unknown, type: ValueType): boolean {
  switch (type) {
    case 'integer': return finite(value) && Number.isSafeInteger(value);
    case 'number': return finite(value);
    case 'boolean': return typeof value === 'boolean';
    case 'string': return typeof value === 'string';
    case 'vector3': return dataArray(value) && value.length === 3 && value.every(finite);
    default: return false;
  }
}
function literalError(value: unknown, type: ValueType, constraints?: Constraints): string | undefined {
  if (!literalMatches(value, type)) return `Expected ${type}${type === 'integer' ? ' in the JSON safe-integer range' : ''}`;
  if (finite(value)) {
    if (constraints?.minimum !== undefined && value < constraints.minimum) return `Value must be >= ${constraints.minimum}`;
    if (constraints?.maximum !== undefined && value > constraints.maximum) return `Value must be <= ${constraints.maximum}`;
  }
  if (constraints?.enum && !constraints.enum.some((entry) => canonicalJson(entry) === canonicalJson(value))) return 'Value is not in the declared enum';
  return undefined;
}

/** Read/validate a declaration without reading or compiling its source as code.
 * No defaults, versions, identifiers or source bytes are silently migrated. */
export function validateCustomFunction(input: unknown, nodeId?: string): Result<CustomFunctionContract> {
  const errors: Diagnostic[] = [];
  const add = (code: string, path: string, message: string, extra: Partial<Diagnostic> = {}) =>
    errors.push(diagnostic(code, path, message, { ...(nodeId === undefined ? {} : { nodeId }), ...extra }));
  try { assertJson(input); } catch (error) {
    return failure([diagnostic('CF_NON_JSON', '', String(error), nodeId === undefined ? {} : { nodeId })]);
  }
  if (!record(input)) return failure([diagnostic('CF_SCHEMA', '', 'Custom Function must be a JSON object')]);
  const versions: [string, unknown, string][] = [
    ['schemaVersion', 1, 'Use a host supporting this schema or an explicit versioned migration. No legacy schema migration is registered; keep the original source and declarations.'],
    ['language', 'javascript', 'Only JavaScript is supported. Convert other languages using an explicitly selected toolchain before replacing source; inspection never transpiles.'],
    ['languageVersion', 1, 'Use a host supporting this JavaScript authoring profile or explicitly migrate the module.'],
    ['apiVersion', '1', 'Use a host supporting this PCG API or migrate operation calls and declarations explicitly.'],
  ];
  for (const [field, expected, guidance] of versions) {
    if (input[field] !== expected) add(`CF_UNSUPPORTED_${field.replace(/([A-Z])/g, '_$1').toUpperCase()}`, `/${field}`, `Expected ${JSON.stringify(expected)}. ${guidance}`);
  }
  // Never interpret an unknown version using the v1 layout.
  if (errors.length) return failure(errors);
  const keys = (object: Record<string, unknown>, allowed: string[], path: string, extra: Partial<Diagnostic> = {}) => {
    for (const key of Object.keys(object)) if (!allowed.includes(key)) add('CF_UNKNOWN_FIELD', `${path}/${pointer(key)}`, `Unknown field "${key}"; use an explicit schema migration`, extra);
  };
  const required = (object: Record<string, unknown>, fields: string[], path: string, extra: Partial<Diagnostic> = {}) => {
    for (const field of fields) if (!own(object, field)) add('CF_REQUIRED_FIELD', `${path}/${field}`, `Missing required field "${field}"`, extra);
  };
  const declarationBase = (object: Record<string, unknown>, path: string, seen: Set<string>, extra: Partial<Diagnostic>) => {
    if (!validId(object.id)) add('CF_INVALID_ID', `${path}/id`, 'Use a stable 1-64 character identifier; reserved object keys are forbidden', extra);
    else if (seen.has(object.id)) add('CF_DUPLICATE_ID', `${path}/id`, `Duplicate stable ID "${object.id}"`, extra);
    else seen.add(object.id);
    if (typeof object.label !== 'string') add('CF_SCHEMA', `${path}/label`, 'label must be a string', extra);
    if (typeof object.required !== 'boolean') add('CF_SCHEMA', `${path}/required`, 'required must be boolean', extra);
  };
  const constraints = (value: unknown, type: unknown, many: boolean, path: string, extra: Partial<Diagnostic>) => {
    if (!record(value)) { add('CF_CONSTRAINT', path, 'constraints must be an object', extra); return; }
    keys(value, ['minimum', 'maximum', 'enum', 'minItems', 'maxItems'], path, extra);
    for (const key of ['minimum', 'maximum']) if (own(value, key)) {
      if (!['number', 'integer'].includes(String(type)) || !finite(value[key])) add('CF_CONSTRAINT', `${path}/${key}`, 'Numeric ranges require a numeric value type and finite bounds', extra);
    }
    if (finite(value.minimum) && finite(value.maximum) && value.minimum > value.maximum) add('CF_CONSTRAINT', path, 'minimum exceeds maximum', extra);
    for (const key of ['minItems', 'maxItems']) if (own(value, key)) {
      if (!many || !Number.isSafeInteger(value[key]) || Number(value[key]) < 0) add('CF_CONSTRAINT', `${path}/${key}`, 'Item bounds require a many port and a nonnegative safe integer', extra);
    }
    if (finite(value.minItems) && finite(value.maxItems) && value.minItems > value.maxItems) add('CF_CONSTRAINT', path, 'minItems exceeds maxItems', extra);
    if (own(value, 'enum')) {
      if (!Array.isArray(value.enum) || value.enum.length === 0 || !VALUE_TYPES.includes(String(type))) add('CF_CONSTRAINT', `${path}/enum`, 'enum must be a nonempty array of typed literal values', extra);
      else {
        const unique = new Set<string>();
        for (const entry of value.enum) {
          const key = canonicalJson(entry);
          if (!literalMatches(entry, type as ValueType) || unique.has(key)) add('CF_CONSTRAINT', `${path}/enum`, 'enum entries must match the value type and be unique', extra);
          unique.add(key);
        }
      }
    }
  };
  keys(input, ['schemaVersion', 'language', 'languageVersion', 'apiVersion', 'entryPoint', 'source', 'seed', 'dependencies', 'parameters', 'parameterValues', 'inputs', 'outputs', 'primaryOutput'], '');
  required(input, ['entryPoint', 'source', 'seed', 'dependencies', 'parameters', 'parameterValues', 'inputs', 'outputs', 'primaryOutput'], '');
  if (typeof input.entryPoint !== 'string' || !/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(input.entryPoint) || RESERVED.has(input.entryPoint) || input.entryPoint === 'default') add('CF_ENTRY_POINT', '/entryPoint', 'Use an explicit named module export such as main (not default)');
  if (typeof input.source !== 'string') add('CF_SOURCE', '/source', 'Portable source must be a string; bytecode is not an asset replacement');
  if (!Number.isInteger(input.seed) || Number(input.seed) < 0 || Number(input.seed) > 0xffffffff) add('CF_SEED', '/seed', 'seed must be an unsigned 32-bit integer');

  if (!Array.isArray(input.dependencies)) add('CF_SCHEMA', '/dependencies', 'dependencies must be an array');
  else {
    const seen = new Set<string>();
    input.dependencies.forEach((dep, index) => {
      const path = `/dependencies/${index}`;
      if (!record(dep)) { add('CF_SCHEMA', path, 'Dependency must be an object'); return; }
      keys(dep, ['id', 'kind', 'reference', 'version', 'contentHash'], path);
      required(dep, ['id', 'kind', 'reference', 'version', 'contentHash'], path);
      if (!validId(dep.id) || seen.has(String(dep.id))) add('CF_DEPENDENCY', `${path}/id`, 'Dependency IDs must be valid and unique');
      seen.add(String(dep.id));
      if (dep.kind !== 'resource' && dep.kind !== 'module') add('CF_DEPENDENCY', `${path}/kind`, 'Expected resource or module metadata; this does not grant execution capability');
      for (const key of ['reference', 'version']) if (typeof dep[key] !== 'string' || !dep[key].trim()) add('CF_DEPENDENCY', `${path}/${key}`, 'Expected a nonempty, explicit dependency reference/version');
      if (typeof dep.contentHash !== 'string' || !/^sha256:[0-9a-f]{64}$/.test(dep.contentHash)) add('CF_DEPENDENCY', `${path}/contentHash`, 'Pin dependency content with sha256:<64 lowercase hexadecimal digits>');
    });
  }
  if (!Array.isArray(input.parameters)) add('CF_SCHEMA', '/parameters', 'parameters must be an array');
  else {
    const seen = new Set<string>();
    input.parameters.forEach((param, index) => {
      const path = `/parameters/${index}`;
      if (!record(param)) { add('CF_SCHEMA', path, 'Parameter must be an object'); return; }
      const extra = typeof param.id === 'string' ? { parameterId: param.id } : {};
      keys(param, ['id', 'label', 'type', 'required', 'default', 'constraints'], path, extra);
      required(param, ['id', 'label', 'type', 'required'], path, extra);
      declarationBase(param, path, seen, extra);
      if (!VALUE_TYPES.includes(String(param.type))) add('CF_PARAMETER_TYPE', `${path}/type`, 'Unknown literal parameter type', extra);
      if (own(param, 'constraints')) constraints(param.constraints, param.type, false, `${path}/constraints`, extra);
    });
  }
  for (const direction of ['inputs', 'outputs'] as const) {
    const ports = input[direction];
    if (!Array.isArray(ports)) { add('CF_SCHEMA', `/${direction}`, `${direction} must be an array`); continue; }
    if (direction === 'outputs' && ports.length === 0) add('CF_OUTPUTS', '/outputs', 'Declare at least one output');
    const seen = new Set<string>();
    ports.forEach((port, index) => {
      const path = `/${direction}/${index}`;
      if (!record(port)) { add('CF_SCHEMA', path, 'Port must be an object'); return; }
      const extra: Partial<Diagnostic> = { direction: direction === 'inputs' ? 'input' : 'output', ...(typeof port.id === 'string' ? { portId: port.id } : {}) };
      keys(port, ['id', 'label', 'pinType', 'cardinality', 'required', 'valueType', 'default', 'constraints'], path, extra);
      required(port, ['id', 'label', 'pinType', 'cardinality', 'required'], path, extra);
      declarationBase(port, path, seen, extra);
      if (!PIN_TYPES.includes(port.pinType as ContractPinType)) add('CF_PORT_TYPE', `${path}/pinType`, 'Use an ordinary graph pin type', extra);
      if (port.cardinality !== 'single' && port.cardinality !== 'many') add('CF_CARDINALITY', `${path}/cardinality`, 'Expected single or many', extra);
      if (port.pinType === 'Param') {
        if (!VALUE_TYPES.includes(String(port.valueType))) add('CF_PORT_TYPE', `${path}/valueType`, 'Param ports require a literal valueType', extra);
      } else if (own(port, 'valueType')) add('CF_PORT_TYPE', `${path}/valueType`, 'Only Param ports have a valueType', extra);
      if (own(port, 'default') && (direction === 'outputs' || port.pinType !== 'Param')) add('CF_DEFAULT', `${path}/default`, 'Only Param inputs may persist defaults; outputs and native handles cannot', extra);
      if (own(port, 'constraints')) constraints(port.constraints, port.valueType, port.cardinality === 'many', `${path}/constraints`, extra);
      if (port.required === true && record(port.constraints) && port.constraints.maxItems === 0) add('CF_CONSTRAINT', path, 'A required many port cannot have maxItems=0', extra);
    });
  }
  if (input.primaryOutput !== null) {
    const port = Array.isArray(input.outputs) ? input.outputs.find((p) => record(p) && p.id === input.primaryOutput) : undefined;
    if (!validId(input.primaryOutput) || !record(port) || port.cardinality !== 'single' || port.required !== true) add('CF_PRIMARY_OUTPUT', '/primaryOutput', 'Select a declared, required single output ID, or null; preview eligibility is checked by the host');
  }
  if (!record(input.parameterValues)) add('CF_SCHEMA', '/parameterValues', 'parameterValues must be an object keyed by parameter ID');
  if (errors.length) return failure(errors);
  const contract = cloneJson(input) as unknown as CustomFunctionContract;
  // Shape is now trusted, so enforce typed defaults and supplied values.
  for (const param of contract.parameters) if (own(param, 'default')) {
    const message = literalError(param.default, param.type, param.constraints);
    if (message) add('CF_DEFAULT', `/parameters/${contract.parameters.indexOf(param)}/default`, message, { parameterId: param.id });
  }
  for (const port of contract.inputs) if (own(port, 'default')) {
    const result = validatePortValue(port, port.default, () => undefined);
    if (result) add('CF_DEFAULT', `/inputs/${contract.inputs.indexOf(port)}/default`, result, { portId: port.id, direction: 'input' });
  }
  const supplied = validateParameters(contract, contract.parameterValues, false);
  errors.push(...supplied.diagnostics.map((error) => ({ ...error, ...(nodeId === undefined ? {} : { nodeId }) })));
  return errors.length ? failure(errors) : { ok: true, value: contract, diagnostics: [] };
}

/** v1 is the first published contract: unknown/missing versions are NOT guessed. */
export function migrateCustomFunction(input: unknown): Result<CustomFunctionContract> {
  return validateCustomFunction(input);
}
export function parseCustomFunction(text: string): Result<CustomFunctionContract> {
  try { return migrateCustomFunction(JSON.parse(text)); }
  catch { return failure([diagnostic('CF_JSON_PARSE', '', 'Invalid JSON; original text has not been modified')]); }
}

/** Missing required values are a pre-Cook error, not a reason to lose a draft. */
export function validateParameters(contract: CustomFunctionContract, values: unknown = contract.parameterValues, requireComplete = true): Result<Record<string, Literal>> {
  if (!dataMap(values)) return failure([diagnostic('CF_PARAMETERS', '/parameterValues', 'Expected a plain parameter map')]);
  const errors: Diagnostic[] = [];
  const normalized: Record<string, Literal> = {};
  for (const id of Object.keys(values)) if (!contract.parameters.some((p) => p.id === id)) errors.push(diagnostic('CF_UNDECLARED_PARAMETER', `/parameterValues/${pointer(id)}`, 'Declare this parameter before supplying it', { parameterId: id }));
  for (const param of contract.parameters) {
    const path = `/parameterValues/${pointer(param.id)}`;
    const present = own(values, param.id) || own(param, 'default');
    const value = own(values, param.id) ? values[param.id] : param.default;
    if (!present) {
      if (param.required && requireComplete) errors.push(diagnostic('CF_MISSING_PARAMETER', path, 'Supply a value or declare a default', { parameterId: param.id }));
      continue;
    }
    const message = literalError(value, param.type, param.constraints);
    if (message) errors.push(diagnostic('CF_PARAMETER_VALUE', path, message, { parameterId: param.id }));
    else normalized[param.id] = cloneJson(value) as Literal;
  }
  return errors.length ? failure(errors) : { ok: true, value: normalized, diagnostics: [] };
}

/** Implemented by a TRUSTED native-handle adapter, never by reading a user object's
 * `pinType` tag. It must reject invalid/released handles and must not execute JS. */
export type ValueInspector = (value: unknown) => ContractPinType | undefined;
export type TypeCompatibility = (source: ContractPinType, target: ContractPinType) => boolean;
// The host supplies its ordinary graph compatibility function. Exact matching is
// the safe default for standalone contract tests, not a second coercion engine.
const exactTypes: TypeCompatibility = (source, target) => source === target || target === 'Any';
function validatePortValue(port: PortDeclaration, value: unknown, inspect: ValueInspector, compatible: TypeCompatibility = exactTypes): string | undefined {
  if (value === null || value === undefined) return 'Null/undefined is not a payload; omit an optional port instead';
  let items: unknown[];
  if (port.cardinality === 'many') {
    if (!dataArray(value)) return 'many requires a dense data array without accessors';
    items = value;
    const min = Math.max(port.required ? 1 : 0, port.constraints?.minItems ?? 0);
    if (items.length < min || (port.constraints?.maxItems !== undefined && items.length > port.constraints.maxItems)) return 'Value count is outside the declared cardinality bounds';
  } else items = [value];
  for (const item of items) {
    if (port.pinType === 'Param') {
      const message = literalError(item, port.valueType!, port.constraints);
      if (message) return message;
    } else {
      let actual: ContractPinType | undefined;
      try { actual = inspect(item); } catch { return 'Native value inspection failed; no outputs may be published'; }
      if (!actual || !PIN_TYPES.includes(actual) || !compatible(actual, port.pinType)) return `Expected a live ${port.pinType} payload from the trusted host adapter`;
    }
  }
  return undefined;
}

/** Validate the COMPLETE input/output map. Failure deliberately returns no partial
 * values. Input defaults apply only to unbound inputs, never to outputs/errors. */
export function validatePortValues(contract: CustomFunctionContract, direction: 'input' | 'output', values: unknown, inspect: ValueInspector, compatible: TypeCompatibility = exactTypes, nodeId?: string): Result<Record<string, unknown>> {
  if (!dataMap(values)) return failure([diagnostic('CF_VALUE_MAP', `/${direction}s`, 'Expected a plain stable-ID keyed value map', { direction, ...(nodeId === undefined ? {} : { nodeId }) })]);
  const ports = direction === 'input' ? contract.inputs : contract.outputs;
  const errors: Diagnostic[] = [];
  const normalized: Record<string, unknown> = {};
  const extra = (portId: string): Partial<Diagnostic> => ({ portId, direction, ...(nodeId === undefined ? {} : { nodeId }) });
  for (const id of Object.keys(values)) if (!ports.some((p) => p.id === id)) errors.push(diagnostic(direction === 'output' ? 'CF_UNDECLARED_OUTPUT' : 'CF_UNDECLARED_INPUT', `/${direction}s/${pointer(id)}`, 'Value has no declared port', extra(id)));
  for (const port of ports) {
    const path = `/${direction}s/${pointer(port.id)}`;
    const hasDefault = direction === 'input' && own(port, 'default');
    if (!own(values, port.id) && !hasDefault) {
      if (port.required) errors.push(diagnostic(direction === 'output' ? 'CF_MISSING_OUTPUT' : 'CF_MISSING_INPUT', path, `Supply required ${direction} "${port.label}" (${port.id})`, extra(port.id)));
      continue;
    }
    const value = own(values, port.id) ? values[port.id] : cloneJson(port.default);
    const message = validatePortValue(port, value, inspect, compatible);
    if (message) errors.push(diagnostic('CF_PORT_VALUE', path, message, extra(port.id)));
    else normalized[port.id] = value;
  }
  return errors.length ? failure(errors) : { ok: true, value: normalized, diagnostics: [] };
}

/** Conservative payload-affecting descriptor, NOT the final whole-node cache key.
 * #27 must additionally hash evaluated inputs/resources and runtime/Core/options.
 * Keeping labels/order/primaryOutput here may over-invalidate but cannot under-key. */
export function customFunctionCacheDescriptor(contract: CustomFunctionContract): string {
  return canonicalJson(contract);
}
