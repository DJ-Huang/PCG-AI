// Graph JSON v1 types — shared contract between Web editor, C++ core, and Unity plugin.

export type NodeType = 'ParseConfig' | 'SpawnPoints' | 'PlaceInScene';

export interface Vec2 { x: number; y: number; }

export interface ParseConfigData {
  seed: number;
  density: number;
}

export interface SpawnPointsData {
  count: number;
  radius: number;
}

export interface PlaceInSceneData {
  prefab: string;
  scale: number;
}

export type NodeData = ParseConfigData | SpawnPointsData | PlaceInSceneData;

export interface GraphNode {
  id: string;
  type: NodeType;
  position: Vec2;
  data: NodeData;
}

export interface GraphEdge {
  id: string;
  source: string;
  target: string;
  sourceHandle?: string;
  targetHandle?: string;
}

export interface GraphJson {
  version: '1.0';
  nodes: GraphNode[];
  edges: GraphEdge[];
}

export const defaultData: Record<NodeType, NodeData> = {
  ParseConfig: { seed: 42, density: 0.5 },
  SpawnPoints: { count: 100, radius: 10.0 },
  PlaceInScene: { prefab: '', scale: 1.0 },
};
