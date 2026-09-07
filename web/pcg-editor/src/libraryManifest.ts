import type {
  GraphParameter,
  GraphSubgraph,
  GraphSubgraphPort,
  ParameterType,
  SemanticComponent,
} from './graphSchema';
import { pinTypesCompatible, type PinType } from './nodeManifest';

export interface LibraryIndexPin {
  id: string;
  name: string;
  pinType: PinType;
}

export interface LibraryIndexParameter {
  id: string;
  name: string;
  type: ParameterType;
  default: GraphParameter['default'];
  hasRange: boolean;
  min: number;
  max: number;
}

export interface LibraryIndexItem {
  id: string;
  displayName: string;
  category: string;
  file: string;
  assetVersion: string;
  contentHash: string;
  keywords: string[];
  description: string;
  inputs: LibraryIndexPin[];
  outputs: LibraryIndexPin[];
  parameters: LibraryIndexParameter[];
  semantic?: SemanticComponent;
  thumbnail?: string;
  doc?: string;
  minCoreVersion?: string;
}

export interface LibraryIndex {
  version: number;
  items: LibraryIndexItem[];
}

let cachedIndex: LibraryIndex | null = null;
let pendingLoad: Promise<LibraryIndex | null> | null = null;
const assetCache = new Map<string, Promise<GraphSubgraph>>();

function libraryUrl(relative: string): string {
  return `${import.meta.env.BASE_URL}library/${relative}`;
}

export function ensureLibraryIndex(): Promise<LibraryIndex | null> {
  if (cachedIndex) return Promise.resolve(cachedIndex);
  if (pendingLoad) return pendingLoad;
  pendingLoad = fetch(libraryUrl('library-index.json'))
    .then((response) => (response.ok ? response.json() : null))
    .then((json) => {
      cachedIndex = json as LibraryIndex | null;
      return cachedIndex;
    })
    .catch(() => null)
    .finally(() => {
      pendingLoad = null;
    });
  return pendingLoad;
}

export function getLibraryIndexSync(): LibraryIndex | null {
  return cachedIndex;
}

export function getLibraryItemsByCategory(): Map<string, LibraryIndexItem[]> {
  const map = new Map<string, LibraryIndexItem[]>();
  for (const item of cachedIndex?.items ?? []) {
    const list = map.get(item.category);
    if (list) list.push(item);
    else map.set(item.category, [item]);
  }
  return map;
}

export function libraryItemHasCompatibleInput(item: LibraryIndexItem, pinType: PinType): boolean {
  return item.inputs.some((pin) => pinTypesCompatible(pinType, pin.pinType));
}

export function libraryItemHasCompatibleOutput(item: LibraryIndexItem, pinType: PinType): boolean {
  return item.outputs.some((pin) => pinTypesCompatible(pinType, pin.pinType));
}

export function libraryItemMatchesQuery(item: LibraryIndexItem, query: string): boolean {
  const needle = query.toLowerCase();
  if (item.displayName.toLowerCase().includes(needle)) return true;
  if (item.id.toLowerCase().includes(needle)) return true;
  if (item.category.toLowerCase().includes(needle)) return true;
  if (item.description.toLowerCase().includes(needle)) return true;
  if (item.semantic?.componentId.toLowerCase().includes(needle)) return true;
  if (item.semantic?.role?.toLowerCase().includes(needle)) return true;
  return item.keywords.some((keyword) => keyword.toLowerCase().includes(needle));
}

export function libraryDefinitionId(item: LibraryIndexItem): string {
  return `lib_${item.id.replace(/[^A-Za-z0-9]+/g, '_')}`;
}

interface SubgraphAssetJson {
  version: string;
  name: string;
  inputs?: GraphSubgraphPort[];
  outputs?: GraphSubgraphPort[];
  parameters?: GraphParameter[];
  nodes: GraphSubgraph['nodes'];
  edges: GraphSubgraph['edges'];
}

export function loadLibrarySubgraph(item: LibraryIndexItem): Promise<GraphSubgraph> {
  const cacheKey = `${item.id}@${item.contentHash}`;
  const pending = assetCache.get(cacheKey);
  if (pending) return pending;
  const load = fetch(libraryUrl(item.file))
    .then((response) => {
      if (!response.ok) throw new Error(`failed to load library asset ${item.file}`);
      return response.json() as Promise<SubgraphAssetJson>;
    })
    .then((asset) => ({
      id: libraryDefinitionId(item),
      name: asset.name || item.displayName,
      inputs: asset.inputs ?? [],
      outputs: asset.outputs ?? [],
      nodes: asset.nodes ?? [],
      edges: asset.edges ?? [],
      parameters: asset.parameters ?? [],
      semantic: item.semantic,
    }));
  assetCache.set(cacheKey, load);
  return load;
}
