export interface AssetFile { name: string; path: string }
export interface AssetFolder { root: string; files: AssetFile[]; truncated: boolean; warnings: string[] }
export async function assetFileRequest<T>(request: { action: 'list' | 'read'; folder: string; path?: string }, signal?: AbortSignal): Promise<T> {
  const response = await fetch('/api/asset-files', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(request), signal });
  if (!response.headers.get('content-type')?.includes('application/json')) throw new Error('Folder browsing requires the local PICG development server.');
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Could not read the asset folder.');
  return result as T;
}
