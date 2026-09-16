import catalog from './builtinEnvironments.json';

export interface BuiltinEnvironment {
  id: string;
  name: string;
  category: string;
  url: string;
  previewUrl: string;
  sourceUrl: string;
}

export const EXTERNAL_ENVIRONMENT_ID = 'external';

export const BUILTIN_ENVIRONMENTS: readonly BuiltinEnvironment[] = catalog.environments.map((environment) => ({
  id: environment.id,
  name: environment.name,
  category: environment.category,
  url: `/environments/polyhaven/${environment.file}`,
  previewUrl: `/environments/polyhaven/${environment.preview}`,
  sourceUrl: environment.sourceUrl,
}));

export const DEFAULT_BUILTIN_ENVIRONMENT: BuiltinEnvironment = BUILTIN_ENVIRONMENTS[0] ?? (() => {
  throw new Error('The built-in environment catalog must contain at least one HDRI');
})();

export function getBuiltinEnvironment(id: string): BuiltinEnvironment | undefined {
  return BUILTIN_ENVIRONMENTS.find((environment) => environment.id === id);
}
