// Generated from node-manifest.json + script-operation-policy-v1.json.
export type PcgScriptOperationName =
  | "AssignMaterial"
  | "BevelMesh"
  | "BooleanMesh"
  | "CreateBoxMesh"
  | "CreateCylinderMesh"
  | "CreateGridMesh"
  | "MergeMesh"
  | "PolyExtrude"
  | "SubdivideMesh"
  | "TransformMesh";

export type PcgOperationParameters = Readonly<Record<string, unknown>>;
export type PcgOperationInputs = Readonly<Record<string, unknown>>;
export type PcgOperationOutputs = Readonly<Record<string, unknown>>;

export interface PcgOperationInvoker {
  invoke(operation: PcgScriptOperationName, parameters?: PcgOperationParameters, inputs?: PcgOperationInputs): PcgOperationOutputs;
}
