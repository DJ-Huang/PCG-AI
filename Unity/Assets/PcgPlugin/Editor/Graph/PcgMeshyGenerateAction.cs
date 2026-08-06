using System.Collections.Generic;
using System.IO;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Meshy3DGenerator bottom action: the only path that calls the Meshy API.
    /// Formats (GLB / FBX) are chosen in the inspector; on success, prompts to save
    /// each selected format into Assets/ and records the primary path on the node.
    /// </summary>
    public static class PcgMeshyGenerateAction
    {
        public static readonly PcgAsyncNodeActionDef Def = new()
        {
            ActionId = "MeshyGenerate",
            UndoLabel = "Meshy 3D Generate",
            Caption =
                "Calls Meshy for the checked formats, then prompts to save into Assets/. " +
                "Cook/preview load the saved primary path (never call the API).",
            GetIdleView = GetIdleView,
            Prepare = Prepare,
            Apply = Apply,
            BuildExtraUi = BuildFormatToggles,
        };

        private static VisualElement BuildFormatToggles(
            PcgManifestNodeView node, System.Action notifyChanged)
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
            toggle.tooltip =
                format == PcgMeshySaveFormats.Glb
                    ? "Download/save GLB (preferred for cook)."
                    : "Download/save FBX from Meshy (same task).";
            toggle.RegisterValueChangedCallback(evt =>
            {
                node.SetPropertyValue(key, evt.newValue);
                if (!evt.newValue &&
                    PcgMeshySaveFormats.ReadSelected(node.CollectData()).Count == 0)
                {
                    // Keep at least one format so Generate always has a target_formats entry.
                    node.SetPropertyValue("saveGlb", true);
                    if (key == "saveGlb")
                        toggle.SetValueWithoutNotify(true);
                }
                notifyChanged?.Invoke();
            });
            row.Add(toggle);
        }

        private static PcgNodeActionIdleView GetIdleView(
            PcgManifestNodeView node, PcgNodeAsyncActionController.State state)
        {
            var data = node.CollectData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            var hasSaved = PcgMeshyResolver.TryGetSavedModelPath(data, out var savedPath);
            var hasCache = PcgMeshyResolver.TryGetCachedModelPath(
                node.NodeId, data, out var cachePath);
            var hasModel = hasSaved || hasCache;

            var view = new PcgNodeActionIdleView
            {
                ButtonLabel = hasModel ? "Regenerate" : "Generate",
                ButtonTooltip = hasModel
                    ? $"Call Meshy again for {string.Join("/", formats).ToUpperInvariant()}, then save."
                    : $"Create Meshy Image-to-3D ({string.Join("/", formats).ToUpperInvariant()}) and save.",
            };

            if (state.Succeeded && !string.IsNullOrEmpty(state.Payload))
            {
                view.StatusText = hasSaved
                    ? $"Saved → {FormatDisplayPath(savedPath)}"
                    : $"Cached → {state.Payload}";
                view.StatusTone = PcgNodeActionStatusTone.Success;
            }
            else if (hasSaved)
            {
                view.StatusText = $"Saved → {FormatDisplayPath(savedPath)}";
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
            if (!PcgMeshyResolver.TryBuildGenerateRequest(nodeId, data, out var request, out var path, out var error))
                return PcgNodeAsyncActionController.PrepareResult.Failure(error);

            return PcgNodeAsyncActionController.PrepareResult.Success((ct, report) =>
            {
                var result = PcgMeshyClient.GenerateAndDownload(request, path, ct, report);
                if (!result.Ok)
                    return PcgNodeAsyncActionController.WorkResult.Failure(result.Error);

                var formats = result.ModelPathsByFormat != null
                    ? string.Join(",", result.ModelPathsByFormat.Keys)
                    : Path.GetExtension(result.ModelPath);
                var log =
                    $"[PCG] Meshy3DGenerator '{nodeId}' downloaded [{formats}] → {result.ModelPath}" +
                    (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : "");
                return PcgNodeAsyncActionController.WorkResult.Success(result.ModelPath, log);
            });
        }

        private static void Apply(PcgManifestNodeView node, string cachePayload)
        {
            if (string.IsNullOrWhiteSpace(cachePayload) || !File.Exists(cachePayload))
            {
                Debug.LogError($"[PCG] Meshy Generate finished but cache file is missing: {cachePayload}");
                return;
            }

            if (TryPromptAndSave(node, cachePayload, out var projectRelativePath))
            {
                node.SetPropertyValue("path", projectRelativePath);
                node.SetPropertyValue("projectRoot", "");
                return;
            }

            if (PcgMeshyResolver.TryGetSavedModelPath(node.CollectData(), out _))
            {
                Debug.Log(
                    "[PCG] Meshy save canceled — keeping the previously saved model path.");
                return;
            }

            node.SetPropertyValue("path", cachePayload);
            node.SetPropertyValue("projectRoot", "");
            Debug.Log(
                "[PCG] Meshy save canceled — using temporary MeshyCache path for this session.");
        }

        private static bool TryPromptAndSave(
            PcgManifestNodeView node, string cachePayload, out string projectRelativePath)
        {
            projectRelativePath = null;

            var data = node.CollectData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            var primary = PcgMeshySaveFormats.Primary(formats);
            var current = data.GetRaw("path")?.ToString()?.Replace('\\', '/') ?? "";
            var defaultName = "MeshyModel";
            var defaultFolder = "Assets";

            if (!string.IsNullOrWhiteSpace(current) &&
                current.StartsWith("Assets/", System.StringComparison.OrdinalIgnoreCase))
            {
                defaultFolder = Path.GetDirectoryName(current)?.Replace('\\', '/') ?? "Assets";
                defaultName = Path.GetFileNameWithoutExtension(current);
            }
            else
            {
                var title = data.GetRaw("__nodeTitle")?.ToString();
                if (!string.IsNullOrWhiteSpace(title))
                    defaultName = SanitizeFilename(title);
                else if (!string.IsNullOrWhiteSpace(node.NodeId))
                    defaultName = SanitizeFilename("Meshy_" + node.NodeId);
            }

            if (string.IsNullOrWhiteSpace(defaultFolder) ||
                !defaultFolder.StartsWith("Assets", System.StringComparison.OrdinalIgnoreCase))
                defaultFolder = "Assets";

            var savePath = EditorUtility.SaveFilePanelInProject(
                "Save Meshy Model",
                defaultName,
                primary,
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

            string primaryRelative = null;
            var saved = new List<string>();
            try
            {
                foreach (var format in formats)
                {
                    var src = Path.Combine(cacheDir, cacheBase + "." + format);
                    if (!File.Exists(src))
                    {
                        Debug.LogWarning(
                            $"[PCG] Meshy cache missing {format}: {src}");
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
                Debug.LogError($"[PCG] Failed to save Meshy model: {ex.Message}");
                return false;
            }

            if (saved.Count == 0)
            {
                Debug.LogError("[PCG] No Meshy format files were saved.");
                return false;
            }

            projectRelativePath = primaryRelative ?? saved[0];
            Debug.Log(
                $"[PCG] Meshy3DGenerator '{node.NodeId}' saved → {string.Join(", ", saved)}");
            return true;
        }

        private static string FormatDisplayPath(string absolutePath)
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

        private static string SanitizeFilename(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return "MeshyModel";
            foreach (var c in Path.GetInvalidFileNameChars())
                value = value.Replace(c, '_');
            value = value.Trim();
            return string.IsNullOrEmpty(value) ? "MeshyModel" : value;
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
}
