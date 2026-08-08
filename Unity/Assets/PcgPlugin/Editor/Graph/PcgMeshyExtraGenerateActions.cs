using System.Collections.Generic;
using System.IO;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>Shared save/display helpers for the Meshy extra Generate actions.</summary>
    internal static class PcgMeshyGenerateSaveUtil
    {
        public static string FormatDisplayPath(string absolutePath)
        {
            var projectRoot = PcgMeshyResolver.GetUnityProjectRoot();
            if (string.IsNullOrEmpty(projectRoot) || string.IsNullOrEmpty(absolutePath))
                return absolutePath;
            try
            {
                var relative = Path.GetRelativePath(projectRoot, absolutePath).Replace('\\', '/');
                if (!relative.StartsWith("..", System.StringComparison.Ordinal))
                    return relative;
            }
            catch (System.Exception)
            {
                // Fall through to absolute.
            }
            return absolutePath;
        }

        public static string SanitizeFilename(string value, string fallback)
        {
            if (string.IsNullOrWhiteSpace(value))
                return fallback;
            foreach (var c in Path.GetInvalidFileNameChars())
                value = value.Replace(c, '_');
            value = value.Trim();
            return string.IsNullOrEmpty(value) ? fallback : value;
        }

        public static string DefaultSaveBaseName(PcgManifestNodeView node, PcgNodeData data, string prefix)
        {
            var title = data?.GetRaw("__nodeTitle")?.ToString();
            if (!string.IsNullOrWhiteSpace(title))
                return SanitizeFilename(title, prefix);
            if (!string.IsNullOrWhiteSpace(node?.NodeId))
                return SanitizeFilename(prefix + "_" + node.NodeId, prefix);
            return prefix;
        }

        /// <summary>Save-panel + copy every downloaded format into Assets/. Returns primary path.</summary>
        public static bool TryPromptAndSaveFormats(
            PcgManifestNodeView node,
            string cachePayload,
            List<string> formats,
            string title,
            string extension,
            out string projectRelativePath)
        {
            projectRelativePath = null;
            var data = node.CollectData();
            var current = data.GetRaw("path")?.ToString()?.Replace('\\', '/') ?? "";
            var defaultName = DefaultSaveBaseName(node, data, title);
            var defaultFolder = "Assets";
            if (!string.IsNullOrWhiteSpace(current) &&
                current.StartsWith("Assets/", System.StringComparison.OrdinalIgnoreCase))
            {
                defaultFolder = Path.GetDirectoryName(current)?.Replace('\\', '/') ?? "Assets";
                defaultName = Path.GetFileNameWithoutExtension(current);
            }
            if (string.IsNullOrWhiteSpace(defaultFolder) ||
                !defaultFolder.StartsWith("Assets", System.StringComparison.OrdinalIgnoreCase))
                defaultFolder = "Assets";

            var savePath = EditorUtility.SaveFilePanelInProject(
                $"Save {node.NodeType} Result",
                defaultName,
                extension,
                $"Choose a base name under Assets/. Saves: {string.Join(", ", formats).ToUpperInvariant()}.",
                defaultFolder);
            if (string.IsNullOrEmpty(savePath))
                return false;

            savePath = savePath.Replace('\\', '/');
            var destDir = Path.GetDirectoryName(savePath)?.Replace('\\', '/') ?? "Assets";
            var destBase = Path.GetFileNameWithoutExtension(savePath);
            var cacheDir = Path.GetDirectoryName(cachePayload);
            var cacheBase = Path.GetFileNameWithoutExtension(cachePayload);
            if (string.IsNullOrEmpty(cacheDir) || string.IsNullOrEmpty(cacheBase))
            {
                Debug.LogError("[PCG] Meshy cache path is invalid.");
                return false;
            }

            var primary = PcgMeshySaveFormats.Primary(formats);
            string primaryRelative = null;
            var saved = new List<string>();
            try
            {
                foreach (var format in formats)
                {
                    var src = Path.Combine(cacheDir, cacheBase + "." + format);
                    if (!File.Exists(src))
                    {
                        Debug.LogWarning($"[PCG] Meshy cache missing {format}: {src}");
                        continue;
                    }

                    var relative = $"{destDir}/{destBase}.{format}".Replace('\\', '/');
                    var absoluteDest = PcgMeshyResolver.ResolveProjectAbsolutePath(relative);
                    var dir = Path.GetDirectoryName(absoluteDest);
                    if (!string.IsNullOrEmpty(dir))
                        Directory.CreateDirectory(dir);
                    File.Copy(src, absoluteDest, overwrite: true);
                    AssetDatabase.ImportAsset(relative);
                    saved.Add(relative);
                    if (string.Equals(format, primary, System.StringComparison.OrdinalIgnoreCase))
                        primaryRelative = relative;
                }
            }
            catch (System.Exception ex)
            {
                Debug.LogError($"[PCG] Failed to save Meshy result: {ex.Message}");
                return false;
            }

            if (saved.Count == 0)
            {
                Debug.LogError("[PCG] No Meshy files were saved.");
                return false;
            }

            projectRelativePath = primaryRelative ?? saved[0];
            Debug.Log($"[PCG] {node.NodeType} '{node.NodeId}' saved → {string.Join(", ", saved)}");
            return true;
        }

        /// <summary>Single-file save (e.g. generated PNG) into Assets/.</summary>
        public static bool TryPromptAndSaveSingle(
            PcgManifestNodeView node,
            string cachePayload,
            string extension,
            out string projectRelativePath)
        {
            projectRelativePath = null;
            var data = node.CollectData();
            var current = data.GetRaw("path")?.ToString()?.Replace('\\', '/') ?? "";
            var defaultName = DefaultSaveBaseName(node, data, "MeshyImage");
            var defaultFolder = "Assets";
            if (!string.IsNullOrWhiteSpace(current) &&
                current.StartsWith("Assets/", System.StringComparison.OrdinalIgnoreCase))
            {
                defaultFolder = Path.GetDirectoryName(current)?.Replace('\\', '/') ?? "Assets";
                defaultName = Path.GetFileNameWithoutExtension(current);
            }
            if (string.IsNullOrWhiteSpace(defaultFolder) ||
                !defaultFolder.StartsWith("Assets", System.StringComparison.OrdinalIgnoreCase))
                defaultFolder = "Assets";

            var savePath = EditorUtility.SaveFilePanelInProject(
                $"Save {node.NodeType} Result",
                defaultName,
                extension,
                $"Choose a {extension.ToUpperInvariant()} path under Assets/.",
                defaultFolder);
            if (string.IsNullOrEmpty(savePath))
                return false;

            savePath = savePath.Replace('\\', '/');
            try
            {
                var absoluteDest = PcgMeshyResolver.ResolveProjectAbsolutePath(savePath);
                var dir = Path.GetDirectoryName(absoluteDest);
                if (!string.IsNullOrEmpty(dir))
                    Directory.CreateDirectory(dir);
                File.Copy(cachePayload, absoluteDest, overwrite: true);
                AssetDatabase.ImportAsset(savePath);
            }
            catch (System.Exception ex)
            {
                Debug.LogError($"[PCG] Failed to save Meshy result: {ex.Message}");
                return false;
            }

            projectRelativePath = savePath;
            Debug.Log($"[PCG] {node.NodeType} '{node.NodeId}' saved → {savePath}");
            return true;
        }

        /// <summary>Apply write-back shared by all model-output Meshy actions.</summary>
        public static void ApplyModelResult(
            PcgManifestNodeView node,
            string cachePayload,
            List<string> formats,
            string title,
            string extension)
        {
            if (string.IsNullOrWhiteSpace(cachePayload) || !File.Exists(cachePayload))
            {
                Debug.LogError($"[PCG] Meshy Generate finished but cache file is missing: {cachePayload}");
                return;
            }

            if (TryPromptAndSaveFormats(node, cachePayload, formats, title, extension, out var saved))
            {
                node.SetPropertyValue("path", saved);
                node.SetPropertyValue("projectRoot", "");
                return;
            }

            if (PcgMeshyResolver.TryGetSavedModelPath(node.CollectData(), out _))
            {
                Debug.Log("[PCG] Meshy save canceled — keeping the previously saved model path.");
                return;
            }

            node.SetPropertyValue("path", cachePayload);
            node.SetPropertyValue("projectRoot", "");
            Debug.Log("[PCG] Meshy save canceled — using temporary MeshyCache path for this session.");
        }

        public static void ApplyImageResult(PcgManifestNodeView node, string cachePayload)
        {
            if (string.IsNullOrWhiteSpace(cachePayload) || !File.Exists(cachePayload))
            {
                Debug.LogError($"[PCG] Meshy Generate finished but cache file is missing: {cachePayload}");
                return;
            }

            if (TryPromptAndSaveSingle(node, cachePayload, "png", out var saved))
            {
                node.SetPropertyValue("path", saved);
                node.SetPropertyValue("projectRoot", "");
                return;
            }

            if (PcgMeshyResolver.TryGetSavedModelPath(node.CollectData(), out _))
            {
                Debug.Log("[PCG] Meshy save canceled — keeping the previously saved image path.");
                return;
            }

            node.SetPropertyValue("path", cachePayload);
            node.SetPropertyValue("projectRoot", "");
            Debug.Log("[PCG] Meshy save canceled — using temporary MeshyCache path for this session.");
        }
    }

    /// <summary>MeshyTextTo3D bottom action: preview → refine chained in one Generate.</summary>
    public static class PcgMeshyTextTo3DGenerateAction
    {
        public static readonly PcgAsyncNodeActionDef Def = new()
        {
            ActionId = "MeshyTextTo3DGenerate",
            UndoLabel = "Meshy Text to 3D Generate",
            Caption =
                "Calls Meshy Text-to-3D (preview, then refine when texture is on), then prompts " +
                "to save into Assets/. Cook/preview load the saved path (never call the API).",
            GetIdleView = GetIdleView,
            Prepare = Prepare,
            Apply = Apply,
            BuildExtraUi = PcgMeshyGenerateActionFormatToggles.Build,
        };

        private static PcgNodeActionIdleView GetIdleView(
            PcgManifestNodeView node, PcgNodeAsyncActionController.State state)
        {
            var data = node.CollectData();
            var hasSaved = PcgMeshyResolver.TryGetSavedModelPath(data, out var savedPath);
            var hasCache = PcgMeshyTextTo3DResolver.TryGetCachedModelPath(node.NodeId, data, out var cachePath);
            var hasModel = hasSaved || hasCache;

            var view = new PcgNodeActionIdleView
            {
                ButtonLabel = hasModel ? "Regenerate" : "Generate",
                ButtonTooltip = hasModel
                    ? "Call Meshy Text-to-3D again, then save."
                    : "Create Meshy Text-to-3D (preview → refine) and save.",
            };

            if (state.Succeeded && !string.IsNullOrEmpty(state.Payload))
            {
                view.StatusText = hasSaved
                    ? $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}"
                    : $"Cached → {state.Payload}";
                view.StatusTone = PcgNodeActionStatusTone.Success;
            }
            else if (hasSaved)
            {
                view.StatusText = $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            else if (hasCache)
            {
                view.StatusText = $"Cached → {cachePath}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            return view;
        }

        private static PcgNodeAsyncActionController.PrepareResult Prepare(string nodeId, PcgNodeData data)
        {
            if (!PcgMeshyTextTo3DResolver.TryBuildGenerateRequest(nodeId, data, out var request, out var path, out var error))
                return PcgNodeAsyncActionController.PrepareResult.Failure(error);

            return PcgNodeAsyncActionController.PrepareResult.Success((ct, report) =>
            {
                var result = PcgMeshyClient.GenerateTextTo3d(request, path, ct, report);
                if (!result.Ok)
                    return PcgNodeAsyncActionController.WorkResult.Failure(result.Error);

                var formats = result.ModelPathsByFormat != null
                    ? string.Join(",", result.ModelPathsByFormat.Keys)
                    : Path.GetExtension(result.ModelPath);
                var log =
                    $"[PCG] MeshyTextTo3D '{nodeId}' downloaded [{formats}] → {result.ModelPath}" +
                    (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : "");
                return PcgNodeAsyncActionController.WorkResult.Success(result.ModelPath, log);
            });
        }

        private static void Apply(PcgManifestNodeView node, string cachePayload)
        {
            var formats = PcgMeshySaveFormats.ReadSelected(node.CollectData());
            PcgMeshyGenerateSaveUtil.ApplyModelResult(node, cachePayload, formats, "MeshyText3D", "glb");
        }
    }

    /// <summary>GLB/FBX save-format toggles row (same behavior as Meshy3DGenerator).</summary>
    internal static class PcgMeshyGenerateActionFormatToggles
    {
        public static VisualElement Build(PcgManifestNodeView node, System.Action notifyChanged)
        {
            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    marginBottom = 6,
                    flexWrap = Wrap.Wrap,
                },
            };
            row.Add(new Label("Save formats")
            {
                style =
                {
                    color = new Color(0.75f, 0.75f, 0.75f),
                    fontSize = 10,
                    marginRight = 8,
                    unityTextAlign = TextAnchor.MiddleLeft,
                },
            });

            AddFormatToggle(row, node, notifyChanged, PcgMeshySaveFormats.Glb, "GLB", true);
            AddFormatToggle(row, node, notifyChanged, PcgMeshySaveFormats.Fbx, "FBX", false);
            return row;
        }

        private static void AddFormatToggle(
            VisualElement row,
            PcgManifestNodeView node,
            System.Action notifyChanged,
            string format,
            string label,
            bool defaultOn)
        {
            var data = node.CollectData();
            var key = format == PcgMeshySaveFormats.Glb ? "saveGlb" : "saveFbx";
            var initial = ReadBool(data, key, defaultOn);

            var toggle = new Toggle(label) { value = initial };
            toggle.style.marginRight = 10;
            toggle.RegisterValueChangedCallback(evt =>
            {
                node.SetPropertyValue(key, evt.newValue);
                if (!evt.newValue &&
                    PcgMeshySaveFormats.ReadSelected(node.CollectData()).Count == 0)
                {
                    node.SetPropertyValue("saveGlb", true);
                    if (key == "saveGlb")
                        toggle.SetValueWithoutNotify(true);
                }
                notifyChanged?.Invoke();
            });
            row.Add(toggle);
        }

        private static bool ReadBool(PcgNodeData data, string key, bool fallback)
        {
            var raw = data?.GetRaw(key);
            if (raw == null)
                return fallback;
            if (raw is bool b)
                return b;
            return bool.TryParse(raw.ToString(), out var parsed) ? parsed : fallback;
        }
    }

    /// <summary>MeshyMeshOps bottom action: cooks the mesh input once, uploads GLB.</summary>
    public static class PcgMeshyMeshOpsGenerateAction
    {
        public static readonly PcgAsyncNodeActionDef Def = new()
        {
            ActionId = "MeshyMeshOpsGenerate",
            UndoLabel = "Meshy Mesh Ops Generate",
            Caption =
                "Cooks the connected mesh input once, uploads it as GLB, runs the selected " +
                "Meshy operation, then prompts to save. Cook/preview load the saved path.",
            GetIdleView = GetIdleView,
            PrepareWithWindow = Prepare,
            Apply = Apply,
            BuildExtraUi = BuildFormatToggles,
        };

        private static VisualElement BuildFormatToggles(
            PcgManifestNodeView node, System.Action notifyChanged)
        {
            var operation = node.CollectData().GetRaw("operation")?.ToString()
                            ?? PcgMeshyClient.MeshOpRemesh;
            return operation == PcgMeshyClient.MeshOpRemesh
                ? PcgMeshyGenerateActionFormatToggles.Build(node, notifyChanged)
                : null;
        }

        private static PcgNodeActionIdleView GetIdleView(
            PcgManifestNodeView node, PcgNodeAsyncActionController.State state)
        {
            var data = node.CollectData();
            var hasSaved = PcgMeshyResolver.TryGetSavedModelPath(data, out var savedPath);
            var hasCache = PcgMeshyMeshOpsResolver.TryGetCachedModelPath(node.NodeId, data, out var cachePath);
            var hasModel = hasSaved || hasCache;
            var operation = data.GetRaw("operation")?.ToString() ?? PcgMeshyClient.MeshOpRemesh;

            var view = new PcgNodeActionIdleView
            {
                ButtonLabel = hasModel ? "Regenerate" : "Generate",
                ButtonTooltip = hasModel
                    ? $"Cook the input and call Meshy {operation} again, then save."
                    : $"Cook the input, call Meshy {operation}, and save.",
            };

            if (state.Succeeded && !string.IsNullOrEmpty(state.Payload))
            {
                view.StatusText = hasSaved
                    ? $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}"
                    : $"Cached → {state.Payload}";
                view.StatusTone = PcgNodeActionStatusTone.Success;
            }
            else if (hasSaved)
            {
                view.StatusText = $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            else if (hasCache)
            {
                view.StatusText = $"Cached → {cachePath}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            return view;
        }

        private static PcgNodeAsyncActionController.PrepareResult Prepare(
            PcgManifestNodeView node, PcgGraphEditorWindow window)
        {
            if (window == null)
                return PcgNodeAsyncActionController.PrepareResult.Failure(
                    "MeshyMeshOps needs the graph editor window (open the .pcg in Graph Editor).");

            var data = node.CollectData();
            if (!PcgMeshyUpstreamExport.TryCookInputToGlbDataUri(window, node, out var dataUri, out var cookError))
                return PcgNodeAsyncActionController.PrepareResult.Failure(cookError);

            if (!PcgMeshyMeshOpsResolver.TryBuildGenerateRequest(
                    node.NodeId, data, dataUri, out var request, out var path, out var error))
                return PcgNodeAsyncActionController.PrepareResult.Failure(error);

            return PcgNodeAsyncActionController.PrepareResult.Success((ct, report) =>
            {
                var result = PcgMeshyClient.GenerateMeshOp(request, path, ct, report);
                if (!result.Ok)
                    return PcgNodeAsyncActionController.WorkResult.Failure(result.Error);

                var log =
                    $"[PCG] MeshyMeshOps '{node.NodeId}' ({request.Operation}) downloaded → {result.ModelPath}" +
                    (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : "");
                return PcgNodeAsyncActionController.WorkResult.Success(result.ModelPath, log);
            });
        }

        private static void Apply(PcgManifestNodeView node, string cachePayload)
        {
            var formats = PcgMeshyMeshOpsResolver.FormatsFor(node.CollectData());
            PcgMeshyGenerateSaveUtil.ApplyModelResult(node, cachePayload, formats, "MeshyMeshOps", "glb");
        }
    }

    /// <summary>MeshyRetexture bottom action: cooks the mesh input once, uploads GLB.</summary>
    public static class PcgMeshyRetextureGenerateAction
    {
        public static readonly PcgAsyncNodeActionDef Def = new()
        {
            ActionId = "MeshyRetextureGenerate",
            UndoLabel = "Meshy Retexture Generate",
            Caption =
                "Cooks the connected mesh input once, uploads it as GLB, re-textures with the " +
                "chosen style, then prompts to save. Cook/preview load the saved path.",
            GetIdleView = GetIdleView,
            PrepareWithWindow = Prepare,
            Apply = Apply,
            BuildExtraUi = PcgMeshyGenerateActionFormatToggles.Build,
        };

        private static PcgNodeActionIdleView GetIdleView(
            PcgManifestNodeView node, PcgNodeAsyncActionController.State state)
        {
            var data = node.CollectData();
            var hasSaved = PcgMeshyResolver.TryGetSavedModelPath(data, out var savedPath);
            var hasCache = PcgMeshyRetextureResolver.TryGetCachedModelPath(node.NodeId, data, out var cachePath);
            var hasModel = hasSaved || hasCache;

            var view = new PcgNodeActionIdleView
            {
                ButtonLabel = hasModel ? "Regenerate" : "Generate",
                ButtonTooltip = hasModel
                    ? "Cook the input and call Meshy Retexture again, then save."
                    : "Cook the input, call Meshy Retexture, and save.",
            };

            if (state.Succeeded && !string.IsNullOrEmpty(state.Payload))
            {
                view.StatusText = hasSaved
                    ? $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}"
                    : $"Cached → {state.Payload}";
                view.StatusTone = PcgNodeActionStatusTone.Success;
            }
            else if (hasSaved)
            {
                view.StatusText = $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            else if (hasCache)
            {
                view.StatusText = $"Cached → {cachePath}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            return view;
        }

        private static PcgNodeAsyncActionController.PrepareResult Prepare(
            PcgManifestNodeView node, PcgGraphEditorWindow window)
        {
            if (window == null)
                return PcgNodeAsyncActionController.PrepareResult.Failure(
                    "MeshyRetexture needs the graph editor window (open the .pcg in Graph Editor).");

            var data = node.CollectData();
            if (!PcgMeshyUpstreamExport.TryCookInputToGlbDataUri(window, node, out var dataUri, out var cookError))
                return PcgNodeAsyncActionController.PrepareResult.Failure(cookError);

            if (!PcgMeshyRetextureResolver.TryBuildGenerateRequest(
                    node.NodeId, data, dataUri, out var request, out var path, out var error))
                return PcgNodeAsyncActionController.PrepareResult.Failure(error);

            return PcgNodeAsyncActionController.PrepareResult.Success((ct, report) =>
            {
                var result = PcgMeshyClient.GenerateRetexture(request, path, ct, report);
                if (!result.Ok)
                    return PcgNodeAsyncActionController.WorkResult.Failure(result.Error);

                var formats = result.ModelPathsByFormat != null
                    ? string.Join(",", result.ModelPathsByFormat.Keys)
                    : Path.GetExtension(result.ModelPath);
                var log =
                    $"[PCG] MeshyRetexture '{node.NodeId}' downloaded [{formats}] → {result.ModelPath}" +
                    (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : "");
                return PcgNodeAsyncActionController.WorkResult.Success(result.ModelPath, log);
            });
        }

        private static void Apply(PcgManifestNodeView node, string cachePayload)
        {
            var formats = PcgMeshySaveFormats.ReadSelected(node.CollectData());
            PcgMeshyGenerateSaveUtil.ApplyModelResult(node, cachePayload, formats, "MeshyRetexture", "glb");
        }
    }

    /// <summary>MeshyImageGen bottom action: text/image-to-image, saves a PNG.</summary>
    public static class PcgMeshyImageGenGenerateAction
    {
        public static readonly PcgAsyncNodeActionDef Def = new()
        {
            ActionId = "MeshyImageGenGenerate",
            UndoLabel = "Meshy Image Generate",
            Caption =
                "Calls Meshy image generation, then prompts to save the PNG into Assets/. " +
                "Cook/preview stream the saved image (never call the API).",
            GetIdleView = GetIdleView,
            Prepare = Prepare,
            Apply = Apply,
        };

        private static PcgNodeActionIdleView GetIdleView(
            PcgManifestNodeView node, PcgNodeAsyncActionController.State state)
        {
            var data = node.CollectData();
            var hasSaved = PcgMeshyResolver.TryGetSavedModelPath(data, out var savedPath);
            var hasCache = PcgMeshyImageGenResolver.TryGetCachedImagePath(node.NodeId, data, out var cachePath);
            var hasImage = hasSaved || hasCache;

            var view = new PcgNodeActionIdleView
            {
                ButtonLabel = hasImage ? "Regenerate" : "Generate",
                ButtonTooltip = hasImage
                    ? "Call Meshy image generation again, then save the PNG."
                    : "Create a Meshy image task and save the PNG.",
            };

            if (state.Succeeded && !string.IsNullOrEmpty(state.Payload))
            {
                view.StatusText = hasSaved
                    ? $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}"
                    : $"Cached → {state.Payload}";
                view.StatusTone = PcgNodeActionStatusTone.Success;
            }
            else if (hasSaved)
            {
                view.StatusText = $"Saved → {PcgMeshyGenerateSaveUtil.FormatDisplayPath(savedPath)}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            else if (hasCache)
            {
                view.StatusText = $"Cached → {cachePath}";
                view.StatusTone = PcgNodeActionStatusTone.Muted;
            }
            return view;
        }

        private static PcgNodeAsyncActionController.PrepareResult Prepare(string nodeId, PcgNodeData data)
        {
            if (!PcgMeshyImageGenResolver.TryBuildGenerateRequest(nodeId, data, out var request, out var path, out var error))
                return PcgNodeAsyncActionController.PrepareResult.Failure(error);

            return PcgNodeAsyncActionController.PrepareResult.Success((ct, report) =>
            {
                var result = PcgMeshyClient.GenerateImage(request, path, ct, report);
                if (!result.Ok)
                    return PcgNodeAsyncActionController.WorkResult.Failure(result.Error);

                var log =
                    $"[PCG] MeshyImageGen '{nodeId}' downloaded PNG → {result.ModelPath}" +
                    (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : "");
                return PcgNodeAsyncActionController.WorkResult.Success(result.ModelPath, log);
            });
        }

        private static void Apply(PcgManifestNodeView node, string cachePayload)
        {
            PcgMeshyGenerateSaveUtil.ApplyImageResult(node, cachePayload);
        }
    }
}
