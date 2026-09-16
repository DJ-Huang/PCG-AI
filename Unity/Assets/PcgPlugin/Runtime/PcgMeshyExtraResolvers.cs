using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Shared cook-prepare engine + local-file resolution for the Meshy node family
    /// (TextTo3D / MeshOps / Retexture / ImageGen). Same contract as
    /// <see cref="PcgMeshyResolver"/>: cook never calls the API; Generate is explicit.
    /// </summary>
#if UNITY_EDITOR
    internal static class PcgMeshyNodeCookUtil
    {
        public delegate bool NodePrepare(PcgGraphNodeRecord node, ref bool changed, out string error);

        public static bool TryPrepareForCook(
            ref string json, string nodeType, NodePrepare prepare, out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(json))
                return true;
            if (json.IndexOf(nodeType, StringComparison.Ordinal) < 0)
                return true;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out error))
                return false;

            var changed = false;
            if (!PrepareList(doc.nodes, nodeType, prepare, ref changed, out error))
                return false;
            if (doc.subgraphs != null)
            {
                foreach (var subgraph in doc.subgraphs)
                {
                    if (subgraph == null)
                        continue;
                    if (!PrepareList(subgraph.nodes, nodeType, prepare, ref changed, out error))
                        return false;
                }
            }

            if (changed)
                json = PcgGraphSerializer.ToJson(doc, pretty: false);
            return true;
        }

        private static bool PrepareList(
            List<PcgGraphNodeRecord> nodes, string nodeType, NodePrepare prepare,
            ref bool changed, out string error)
        {
            error = null;
            if (nodes == null)
                return true;

            foreach (var node in nodes)
            {
                if (node == null || node.type != nodeType || string.IsNullOrEmpty(node.id))
                    continue;
                node.data ??= new PcgNodeData();
                if (!prepare(node, ref changed, out error))
                    return false;
            }
            return true;
        }

        /// <summary>Saved Assets path → deterministic cache → fail with a Generate hint.</summary>
        public static bool TryResolveForCook(
            string label, string nodeId, PcgNodeData data,
            string cacheKey, string primaryFormat, IEnumerable<string> fallbackFormats,
            out string absolutePath, out string error)
        {
            error = null;
            data ??= new PcgNodeData();

            if (PcgMeshyResolver.TryGetSavedModelPath(data, out absolutePath))
                return true;

            absolutePath = PcgMeshyResolver.GetCachePath(cacheKey, primaryFormat);
            if (File.Exists(absolutePath) && new FileInfo(absolutePath).Length > 0)
                return true;

            if (fallbackFormats != null)
            {
                foreach (var format in fallbackFormats)
                {
                    var candidate = PcgMeshyResolver.GetCachePath(cacheKey, format);
                    if (File.Exists(candidate) && new FileInfo(candidate).Length > 0)
                    {
                        absolutePath = candidate;
                        return true;
                    }
                }
            }

            error =
                $"{label} '{nodeId}': no saved/cached result. " +
                "Click Generate, save the result into the project, then cook/preview " +
                "(preview / auto cook never generate).";
            absolutePath = null;
            return false;
        }

        /// <summary>Resolves the result file and injects its absolute path for cook.</summary>
        public static bool ResolveAndInjectPath(
            string label, string cacheKey, string primaryFormat, IEnumerable<string> fallbackFormats,
            PcgGraphNodeRecord node, ref bool changed, out string error)
        {
            if (!TryResolveForCook(
                    label, node.id, node.data, cacheKey, primaryFormat, fallbackFormats,
                    out var absolutePath, out error))
                return false;

            var previous = node.data.GetRaw("path")?.ToString() ?? "";
            if (!string.Equals(previous, absolutePath, StringComparison.Ordinal))
            {
                node.data.SetRaw("path", absolutePath);
                node.data.SetRaw("projectRoot", "");
                changed = true;
            }
            return true;
        }

        public static bool TryGetCachedPath(
            string cacheKey, string primaryFormat, IEnumerable<string> fallbackFormats,
            out string absolutePath)
        {
            absolutePath = PcgMeshyResolver.GetCachePath(cacheKey, primaryFormat);
            if (File.Exists(absolutePath) && new FileInfo(absolutePath).Length > 0)
                return true;
            if (fallbackFormats != null)
            {
                foreach (var format in fallbackFormats)
                {
                    var candidate = PcgMeshyResolver.GetCachePath(cacheKey, format);
                    if (File.Exists(candidate) && new FileInfo(candidate).Length > 0)
                    {
                        absolutePath = candidate;
                        return true;
                    }
                }
            }
            return false;
        }

        public static bool CheckApiKey(string label, string nodeId, out string error)
        {
            error = null;
            if (PcgMeshySettings.HasApiKey)
                return true;
            error =
                $"{label} '{nodeId}': API key missing. " +
                "Open PCG → Settings (or Project Settings → PICG) and enter your Meshy API key.";
            return false;
        }

        public static string BuildCacheKey(string nodeId, PcgNodeData data, params string[] semanticKeys)
        {
            var sb = new StringBuilder(256);
            sb.Append(nodeId);
            if (semanticKeys != null)
            {
                foreach (var key in semanticKeys)
                {
                    sb.Append('|').Append(key).Append('=');
                    sb.Append(data?.GetRaw(key)?.ToString() ?? "");
                }
            }
            return PcgMeshyResolver.ComputeSha256Hex(Encoding.UTF8.GetBytes(sb.ToString()));
        }
    }
#endif

    /// <summary>Runs every third-party cook resolver in chain order.</summary>
    public static class PcgThirdPartyResolvers
    {
        public static bool TryPrepareAll(ref string json, out string error)
        {
            if (!PcgMeshyResolver.TryPrepareForCook(ref json, out error))
                return false;
            if (!PcgTripoResolver.TryPrepareForCook(ref json, out error))
                return false;
            if (!PcgMeshyTextTo3DResolver.TryPrepareForCook(ref json, out error))
                return false;
            if (!PcgMeshyMeshOpsResolver.TryPrepareForCook(ref json, out error))
                return false;
            if (!PcgMeshyRetextureResolver.TryPrepareForCook(ref json, out error))
                return false;
            if (!PcgMeshyImageGenResolver.TryPrepareForCook(ref json, out error))
                return false;
            return true;
        }
    }

    internal static class PcgMeshyNodeTypes
    {
        /// <summary>Platform guard shared by all Meshy extra resolvers (editor-only features).</summary>
        public static bool TryGuard(ref string json, string nodeType, out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(json))
                return true;
            if (json.IndexOf(nodeType, StringComparison.Ordinal) < 0)
                return true;
#if !UNITY_EDITOR
            error = $"{nodeType} requires the Unity Editor.";
            return false;
#else
            return true;
#endif
        }
    }

    /// <summary>MeshyTextTo3D node: text-to-3d v2 (preview → refine) local-path resolution.</summary>
    public static class PcgMeshyTextTo3DResolver
    {
        public const string NodeType = "MeshyTextTo3D";
        private const string Label = "MeshyTextTo3D";

        private static readonly string[] SemanticKeys =
        {
            "prompt", "aiModel", "modelType", "shouldRemesh", "topology", "targetPolycount",
            "poseMode", "shouldTexture", "enablePbr", "textureResolution", "texturePrompt",
            "removeLighting", "saveGlb", "saveFbx",
        };

        public static bool TryPrepareForCook(ref string json, out string error)
        {
#if UNITY_EDITOR
            return PcgMeshyNodeCookUtil.TryPrepareForCook(ref json, NodeType, PrepareNode, out error);
#else
            return PcgMeshyNodeTypes.TryGuard(ref json, NodeType, out error);
#endif
        }

#if UNITY_EDITOR
        private static bool PrepareNode(PcgGraphNodeRecord node, ref bool changed, out string error)
        {
            var formats = PcgMeshySaveFormats.ReadSelected(node.data);
            return PcgMeshyNodeCookUtil.ResolveAndInjectPath(
                Label,
                BuildCacheKey(node.id, node.data),
                PcgMeshySaveFormats.Primary(formats),
                PcgMeshySaveFormats.Supported,
                node, ref changed, out error);
        }

        public static bool TryGetCachedModelPath(string nodeId, PcgNodeData data, out string absolutePath)
        {
            data ??= new PcgNodeData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            return PcgMeshyNodeCookUtil.TryGetCachedPath(
                BuildCacheKey(nodeId ?? "", data),
                PcgMeshySaveFormats.Primary(formats),
                PcgMeshySaveFormats.Supported,
                out absolutePath);
        }

        public static bool TryBuildGenerateRequest(
            string nodeId,
            PcgNodeData data,
            out PcgMeshyClient.TextTo3dRequest request,
            out string absolutePath,
            out string error)
        {
            request = default;
            error = null;
            data ??= new PcgNodeData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            absolutePath = PcgMeshyResolver.GetCachePath(
                BuildCacheKey(nodeId ?? "", data), PcgMeshySaveFormats.Primary(formats));

            if (!PcgMeshyNodeCookUtil.CheckApiKey(Label, nodeId, out error))
                return false;

            var prompt = data.GetRaw("prompt")?.ToString()?.Trim() ?? "";
            if (string.IsNullOrWhiteSpace(prompt))
            {
                error = $"{Label} '{nodeId}': enter a Prompt describing the model.";
                return false;
            }

            request = new PcgMeshyClient.TextTo3dRequest
            {
                Prompt = prompt,
                AiModel = data.GetRaw("aiModel")?.ToString() ?? "latest",
                ModelType = data.GetRaw("modelType")?.ToString() ?? "standard",
                ShouldRemesh = PcgMeshyResolver.ReadBool(data, "shouldRemesh", true),
                Topology = data.GetRaw("topology")?.ToString() ?? "triangle",
                TargetPolycount = PcgMeshyResolver.ReadInt(data, "targetPolycount", 30000),
                PoseMode = data.GetRaw("poseMode")?.ToString() ?? "",
                ShouldTexture = PcgMeshyResolver.ReadBool(data, "shouldTexture", true),
                EnablePbr = PcgMeshyResolver.ReadBool(data, "enablePbr", false),
                TextureResolution = data.GetRaw("textureResolution")?.ToString() ?? "2k",
                TexturePrompt = data.GetRaw("texturePrompt")?.ToString() ?? "",
                RemoveLighting = PcgMeshyResolver.ReadBool(data, "removeLighting", true),
                TargetFormats = formats,
            };
            return true;
        }

        private static string BuildCacheKey(string nodeId, PcgNodeData data) =>
            PcgMeshyNodeCookUtil.BuildCacheKey(nodeId ?? "", data, SemanticKeys);
#endif
    }

    /// <summary>MeshyMeshOps node: remesh / resize / uv-unwrap via one operation enum.</summary>
    public static class PcgMeshyMeshOpsResolver
    {
        public const string NodeType = "MeshyMeshOps";
        private const string Label = "MeshyMeshOps";

        private static readonly string[] SemanticKeys =
        {
            "operation", "topology", "targetPolycount", "resizeMode", "resizeHeight",
            "resizeLongestSide", "originAt", "saveGlb", "saveFbx",
        };

        public static bool TryPrepareForCook(ref string json, out string error)
        {
#if UNITY_EDITOR
            return PcgMeshyNodeCookUtil.TryPrepareForCook(ref json, NodeType, PrepareNode, out error);
#else
            return PcgMeshyNodeTypes.TryGuard(ref json, NodeType, out error);
#endif
        }

#if UNITY_EDITOR
        private static bool PrepareNode(PcgGraphNodeRecord node, ref bool changed, out string error)
        {
            var formats = FormatsFor(node.data);
            return PcgMeshyNodeCookUtil.ResolveAndInjectPath(
                Label,
                BuildCacheKey(node.id, node.data),
                PcgMeshySaveFormats.Primary(formats),
                PcgMeshySaveFormats.Supported,
                node, ref changed, out error);
        }

        public static bool TryGetCachedModelPath(string nodeId, PcgNodeData data, out string absolutePath)
        {
            data ??= new PcgNodeData();
            var formats = FormatsFor(data);
            return PcgMeshyNodeCookUtil.TryGetCachedPath(
                BuildCacheKey(nodeId ?? "", data),
                PcgMeshySaveFormats.Primary(formats),
                PcgMeshySaveFormats.Supported,
                out absolutePath);
        }

        /// <summary>Only remesh honors the GLB/FBX save toggles; resize/uv-unwrap always emit GLB.</summary>
        public static List<string> FormatsFor(PcgNodeData data)
        {
            var operation = data?.GetRaw("operation")?.ToString() ?? PcgMeshyClient.MeshOpRemesh;
            return operation == PcgMeshyClient.MeshOpRemesh
                ? PcgMeshySaveFormats.ReadSelected(data)
                : new List<string> { PcgMeshySaveFormats.Glb };
        }

        /// <param name="modelDataUri">GLB data URI from the upstream cook (editor action).</param>
        public static bool TryBuildGenerateRequest(
            string nodeId,
            PcgNodeData data,
            string modelDataUri,
            out PcgMeshyClient.MeshOpsRequest request,
            out string absolutePath,
            out string error)
        {
            request = default;
            error = null;
            data ??= new PcgNodeData();
            var formats = FormatsFor(data);
            absolutePath = PcgMeshyResolver.GetCachePath(
                BuildCacheKey(nodeId ?? "", data), PcgMeshySaveFormats.Primary(formats));

            if (!PcgMeshyNodeCookUtil.CheckApiKey(Label, nodeId, out error))
                return false;
            if (string.IsNullOrWhiteSpace(modelDataUri))
            {
                error = $"{Label} '{nodeId}': upstream mesh is empty — cook the input first.";
                return false;
            }

            var operation = data.GetRaw("operation")?.ToString() ?? PcgMeshyClient.MeshOpRemesh;
            if (operation != PcgMeshyClient.MeshOpRemesh &&
                operation != PcgMeshyClient.MeshOpResize &&
                operation != PcgMeshyClient.MeshOpUvUnwrap)
            {
                error = $"{Label} '{nodeId}': operation '{operation}' is not supported.";
                return false;
            }

            var resizeMode = data.GetRaw("resizeMode")?.ToString() ?? "height";
            if (operation == PcgMeshyClient.MeshOpResize)
            {
                var height = ReadDouble(data, "resizeHeight", 1.0);
                var longest = ReadDouble(data, "resizeLongestSide", 1.0);
                if (resizeMode == "height" && height <= 0)
                {
                    error = $"{Label} '{nodeId}': Resize Height must be greater than zero.";
                    return false;
                }
                if (resizeMode == "longestSide" && longest <= 0)
                {
                    error = $"{Label} '{nodeId}': Resize Longest Side must be greater than zero.";
                    return false;
                }
            }

            request = new PcgMeshyClient.MeshOpsRequest
            {
                Operation = operation,
                ModelDataUri = modelDataUri,
                Topology = data.GetRaw("topology")?.ToString() ?? "triangle",
                TargetPolycount = PcgMeshyResolver.ReadInt(data, "targetPolycount", 30000),
                ResizeMode = resizeMode,
                ResizeHeight = ReadDouble(data, "resizeHeight", 1.0),
                ResizeLongestSide = ReadDouble(data, "resizeLongestSide", 1.0),
                OriginAt = data.GetRaw("originAt")?.ToString() ?? "bottom",
                TargetFormats = formats,
            };
            return true;
        }

        private static double ReadDouble(PcgNodeData data, string key, double fallback)
        {
            var raw = data?.GetRaw(key);
            if (raw == null)
                return fallback;
            if (raw is double d)
                return d;
            if (raw is float f)
                return f;
            if (raw is int i)
                return i;
            if (raw is long l)
                return l;
            return double.TryParse(
                raw.ToString(),
                System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture,
                out var parsed)
                ? parsed
                : fallback;
        }

        private static string BuildCacheKey(string nodeId, PcgNodeData data) =>
            PcgMeshyNodeCookUtil.BuildCacheKey(nodeId ?? "", data, SemanticKeys);
#endif
    }

    /// <summary>MeshyRetexture node: re-style an upstream mesh with a text/image style.</summary>
    public static class PcgMeshyRetextureResolver
    {
        public const string NodeType = "MeshyRetexture";
        private const string Label = "MeshyRetexture";

        private static readonly string[] SemanticKeys =
        {
            "styleMode", "textStylePrompt", "styleImage", "styleImageUrl", "aiModel",
            "enableOriginalUv", "enablePbr", "textureResolution", "removeLighting",
            "saveGlb", "saveFbx",
        };

        public static bool TryPrepareForCook(ref string json, out string error)
        {
#if UNITY_EDITOR
            return PcgMeshyNodeCookUtil.TryPrepareForCook(ref json, NodeType, PrepareNode, out error);
#else
            return PcgMeshyNodeTypes.TryGuard(ref json, NodeType, out error);
#endif
        }

#if UNITY_EDITOR
        private static bool PrepareNode(PcgGraphNodeRecord node, ref bool changed, out string error)
        {
            var formats = PcgMeshySaveFormats.ReadSelected(node.data);
            return PcgMeshyNodeCookUtil.ResolveAndInjectPath(
                Label,
                BuildCacheKey(node.id, node.data),
                PcgMeshySaveFormats.Primary(formats),
                PcgMeshySaveFormats.Supported,
                node, ref changed, out error);
        }

        public static bool TryGetCachedModelPath(string nodeId, PcgNodeData data, out string absolutePath)
        {
            data ??= new PcgNodeData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            return PcgMeshyNodeCookUtil.TryGetCachedPath(
                BuildCacheKey(nodeId ?? "", data),
                PcgMeshySaveFormats.Primary(formats),
                PcgMeshySaveFormats.Supported,
                out absolutePath);
        }

        /// <param name="modelDataUri">GLB data URI from the upstream cook (editor action).</param>
        public static bool TryBuildGenerateRequest(
            string nodeId,
            PcgNodeData data,
            string modelDataUri,
            out PcgMeshyClient.RetextureRequest request,
            out string absolutePath,
            out string error)
        {
            request = default;
            error = null;
            data ??= new PcgNodeData();
            var formats = PcgMeshySaveFormats.ReadSelected(data);
            absolutePath = PcgMeshyResolver.GetCachePath(
                BuildCacheKey(nodeId ?? "", data), PcgMeshySaveFormats.Primary(formats));

            if (!PcgMeshyNodeCookUtil.CheckApiKey(Label, nodeId, out error))
                return false;
            if (string.IsNullOrWhiteSpace(modelDataUri))
            {
                error = $"{Label} '{nodeId}': upstream mesh is empty — cook the input first.";
                return false;
            }

            var styleMode = data.GetRaw("styleMode")?.ToString() ?? "text";
            string imageStyleDataUri = null;
            var stylePrompt = data.GetRaw("textStylePrompt")?.ToString()?.Trim() ?? "";
            if (styleMode == "image")
            {
                var styleUrl = data.GetRaw("styleImageUrl")?.ToString()?.Trim();
                if (!string.IsNullOrWhiteSpace(styleUrl) &&
                    (styleUrl.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
                     styleUrl.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ||
                     styleUrl.StartsWith("data:", StringComparison.OrdinalIgnoreCase)))
                {
                    imageStyleDataUri = styleUrl;
                }
                else
                {
                    var stored = data.GetRaw("styleImage")?.ToString();
                    if (string.IsNullOrWhiteSpace(stored))
                    {
                        error =
                            $"{Label} '{nodeId}': image style mode needs a Style Image " +
                            "(texture) or Style Image URL.";
                        return false;
                    }

                    var source = PcgTextureAssetUtil.LoadTextureFromStorage(stored);
                    if (source == null)
                    {
                        error = $"{Label} '{nodeId}': failed to load style texture '{stored}'.";
                        return false;
                    }

                    imageStyleDataUri = PcgMeshyResolver.EncodeTextureToPngDataUri(source, out error);
                    if (imageStyleDataUri == null)
                    {
                        if (string.IsNullOrEmpty(error))
                            error = $"{Label} '{nodeId}': EncodeToPNG failed for style image.";
                        return false;
                    }
                }
            }
            else if (string.IsNullOrWhiteSpace(stylePrompt))
            {
                error = $"{Label} '{nodeId}': enter a Text Style Prompt (or switch to image style).";
                return false;
            }

            request = new PcgMeshyClient.RetextureRequest
            {
                ModelDataUri = modelDataUri,
                TextStylePrompt = stylePrompt,
                ImageStyleDataUri = imageStyleDataUri,
                AiModel = data.GetRaw("aiModel")?.ToString() ?? "latest",
                EnableOriginalUv = PcgMeshyResolver.ReadBool(data, "enableOriginalUv", false),
                EnablePbr = PcgMeshyResolver.ReadBool(data, "enablePbr", false),
                TextureResolution = data.GetRaw("textureResolution")?.ToString() ?? "2k",
                RemoveLighting = PcgMeshyResolver.ReadBool(data, "removeLighting", true),
                TargetFormats = formats,
            };
            return true;
        }

        private static string BuildCacheKey(string nodeId, PcgNodeData data) =>
            PcgMeshyNodeCookUtil.BuildCacheKey(nodeId ?? "", data, SemanticKeys);
#endif
    }

    /// <summary>MeshyImageGen node: text-to-image / image-to-image, Texture output.</summary>
    public static class PcgMeshyImageGenResolver
    {
        public const string NodeType = "MeshyImageGen";
        private const string Label = "MeshyImageGen";
        private const string PngExtension = "png";

        private static readonly string[] SemanticKeys =
        {
            "mode", "aiModel", "prompt", "aspectRatio", "generateMultiView",
            "referenceImage", "referenceImageUrl",
        };

        public static bool TryPrepareForCook(ref string json, out string error)
        {
#if UNITY_EDITOR
            return PcgMeshyNodeCookUtil.TryPrepareForCook(ref json, NodeType, PrepareNode, out error);
#else
            return PcgMeshyNodeTypes.TryGuard(ref json, NodeType, out error);
#endif
        }

#if UNITY_EDITOR
        private static bool PrepareNode(PcgGraphNodeRecord node, ref bool changed, out string error)
        {
            // Texture pixels stream through TextureRuntime (slot = node id); the resolver
            // only guarantees a local PNG exists. Nothing to inject into execution JSON.
            return PcgMeshyNodeCookUtil.TryResolveForCook(
                Label, node.id, node.data,
                BuildCacheKey(node.id, node.data),
                PngExtension, null,
                out _, out error);
        }

        public static bool TryGetCachedImagePath(string nodeId, PcgNodeData data, out string absolutePath)
        {
            data ??= new PcgNodeData();
            return PcgMeshyNodeCookUtil.TryGetCachedPath(
                BuildCacheKey(nodeId ?? "", data), PngExtension, null, out absolutePath);
        }

        public static bool TryBuildGenerateRequest(
            string nodeId,
            PcgNodeData data,
            out PcgMeshyClient.ImageGenRequest request,
            out string absolutePath,
            out string error)
        {
            request = default;
            error = null;
            data ??= new PcgNodeData();
            absolutePath = PcgMeshyResolver.GetCachePath(
                BuildCacheKey(nodeId ?? "", data), PngExtension);

            if (!PcgMeshyNodeCookUtil.CheckApiKey(Label, nodeId, out error))
                return false;

            var prompt = data.GetRaw("prompt")?.ToString()?.Trim() ?? "";
            if (string.IsNullOrWhiteSpace(prompt))
            {
                error = $"{Label} '{nodeId}': enter a Prompt describing the image.";
                return false;
            }

            var imageToImage =
                string.Equals(data.GetRaw("mode")?.ToString(), "imageToImage", StringComparison.Ordinal);
            var references = new List<string>();
            if (imageToImage)
            {
                var referenceUrl = data.GetRaw("referenceImageUrl")?.ToString()?.Trim();
                if (!string.IsNullOrWhiteSpace(referenceUrl) &&
                    (referenceUrl.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
                     referenceUrl.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ||
                     referenceUrl.StartsWith("data:", StringComparison.OrdinalIgnoreCase)))
                {
                    references.Add(referenceUrl);
                }
                else
                {
                    var stored = data.GetRaw("referenceImage")?.ToString();
                    if (string.IsNullOrWhiteSpace(stored))
                    {
                        error =
                            $"{Label} '{nodeId}': image-to-image needs a Reference Image " +
                            "(texture) or Reference Image URL.";
                        return false;
                    }

                    var source = PcgTextureAssetUtil.LoadTextureFromStorage(stored);
                    if (source == null)
                    {
                        error = $"{Label} '{nodeId}': failed to load reference texture '{stored}'.";
                        return false;
                    }

                    var dataUri = PcgMeshyResolver.EncodeTextureToPngDataUri(source, out error);
                    if (dataUri == null)
                    {
                        if (string.IsNullOrEmpty(error))
                            error = $"{Label} '{nodeId}': EncodeToPNG failed for reference image.";
                        return false;
                    }
                    references.Add(dataUri);
                }
            }

            request = new PcgMeshyClient.ImageGenRequest
            {
                ImageToImage = imageToImage,
                AiModel = data.GetRaw("aiModel")?.ToString() ?? "nano-banana",
                Prompt = prompt,
                AspectRatio = data.GetRaw("aspectRatio")?.ToString() ?? "1:1",
                GenerateMultiView = PcgMeshyResolver.ReadBool(data, "generateMultiView", false),
                ReferenceImageDataUris = references,
            };
            return true;
        }

        private static string BuildCacheKey(string nodeId, PcgNodeData data) =>
            PcgMeshyNodeCookUtil.BuildCacheKey(nodeId ?? "", data, SemanticKeys);
#endif
    }
}
