using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Threading;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Thin Meshy HTTP client. All endpoints share one task pipeline
    /// (create → poll → download); endpoint paths are constants here so
    /// v1/v2 URL drift is caught in one place (unit tests assert them).
    /// </summary>
    public static class PcgMeshyClient
    {
        public const string DefaultBaseUrl = "https://api.meshy.ai";
        public const string ImageTo3dPath = "/openapi/v1/image-to-3d";
        public const string MultiImageTo3dPath = "/openapi/v1/multi-image-to-3d";
        public const string TextTo3dPath = "/openapi/v2/text-to-3d";
        public const string RemeshPath = "/openapi/v1/remesh";
        public const string ResizePath = "/openapi/v1/resize";
        public const string UvUnwrapPath = "/openapi/v1/uv-unwrap";
        public const string RetexturePath = "/openapi/v1/retexture";
        public const string TextToImagePath = "/openapi/v1/text-to-image";
        public const string ImageToImagePath = "/openapi/v1/image-to-image";

        private static HttpClient Http =>
            PcgThirdPartyHttpSettings.GetHttpClient(TimeSpan.FromMinutes(15));

        public struct GenerateRequest
        {
            public string ImageDataUri;
            /// <summary>Optional extra views (URL / data URI). 2+ total images → multi-image endpoint.</summary>
            public List<string> ExtraImageDataUris;
            public string AiModel;
            public bool EnablePbr;
            public bool ShouldTexture;
            public bool ShouldRemesh;
            public int TargetPolycount;
            /// <summary>Meshy target_formats (e.g. glb, fbx). Empty → glb only.</summary>
            public List<string> TargetFormats;
        }

        public struct GenerateResult
        {
            public bool Ok;
            public string TaskId;
            /// <summary>Primary model path used for cook (prefers GLB).</summary>
            public string ModelPath;
            /// <summary>All downloaded cache paths keyed by format (glb/fbx/…).</summary>
            public Dictionary<string, string> ModelPathsByFormat;
            public string Error;
            public int ConsumedCredits;
        }

        public static GenerateResult GenerateAndDownload(
            GenerateRequest request,
            string outputPrimaryPath,
            CancellationToken cancellationToken = default,
            Action<string, float> progress = null)
        {
            var images = new List<string>();
            if (!string.IsNullOrWhiteSpace(request.ImageDataUri))
                images.Add(request.ImageDataUri.Trim());
            if (request.ExtraImageDataUris != null)
            {
                foreach (var extra in request.ExtraImageDataUris)
                {
                    if (!string.IsNullOrWhiteSpace(extra))
                        images.Add(extra.Trim());
                }
            }

            var result = ValidateBasic(outputPrimaryPath, images.Count == 0 ? "Meshy image is empty." : null);
            if (result.HasValue)
                return result.Value;
            if (images.Count > 4)
                return Failure("Meshy multi-image supports at most 4 images (1–4).");

            var formats = NormalizeFormats(request.TargetFormats);
            var multi = images.Count > 1;
            var body = new Dictionary<string, object>
            {
                ["ai_model"] = string.IsNullOrWhiteSpace(request.AiModel) ? "latest" : request.AiModel,
                ["enable_pbr"] = request.EnablePbr,
                ["should_texture"] = request.ShouldTexture,
                ["should_remesh"] = request.ShouldRemesh,
                ["target_formats"] = new List<object>(formats),
            };
            if (multi)
                body["image_urls"] = new List<object>(images);
            else
                body["image_url"] = images[0];
            if (request.ShouldRemesh && request.TargetPolycount > 0)
                body["target_polycount"] = request.TargetPolycount;

            return RunTaskToModel(
                multi ? MultiImageTo3dPath : ImageTo3dPath,
                body, formats, outputPrimaryPath, cancellationToken, progress);
        }

        public struct TextTo3dRequest
        {
            public string Prompt;
            public string AiModel;
            /// <summary>standard | lowpoly (lowpoly skips remesh/polycount/topology).</summary>
            public string ModelType;
            public bool ShouldRemesh;
            public string Topology;
            public int TargetPolycount;
            /// <summary>"" | a-pose | t-pose.</summary>
            public string PoseMode;
            /// <summary>false → preview only (untextured mesh); true → preview then refine.</summary>
            public bool ShouldTexture;
            public bool EnablePbr;
            /// <summary>2k | 4k | 8k (4k/8k need meshy-6/latest).</summary>
            public string TextureResolution;
            public string TexturePrompt;
            public bool RemoveLighting;
            public List<string> TargetFormats;
        }

        /// <summary>Text-to-3D v2: preview task, then refine task chained via preview_task_id.</summary>
        public static GenerateResult GenerateTextTo3d(
            TextTo3dRequest request,
            string outputPrimaryPath,
            CancellationToken cancellationToken = default,
            Action<string, float> progress = null)
        {
            var result = ValidateBasic(
                outputPrimaryPath,
                string.IsNullOrWhiteSpace(request.Prompt) ? "Meshy text-to-3d prompt is empty." : null);
            if (result.HasValue)
                return result.Value;
            if (request.Prompt != null && request.Prompt.Length > 600)
                return Failure("Meshy text-to-3d prompt exceeds 600 characters.");

            var formats = NormalizeFormats(request.TargetFormats);
            var lowpoly = string.Equals(request.ModelType, "lowpoly", StringComparison.OrdinalIgnoreCase);
            var previewSpan = request.ShouldTexture ? 0.45f : 1f;
            try
            {
                var previewBody = new Dictionary<string, object>
                {
                    ["mode"] = "preview",
                    ["prompt"] = request.Prompt.Trim(),
                    ["model_type"] = lowpoly ? "lowpoly" : "standard",
                    ["target_formats"] = new List<object>(formats),
                };
                if (!lowpoly)
                {
                    previewBody["ai_model"] =
                        string.IsNullOrWhiteSpace(request.AiModel) ? "latest" : request.AiModel;
                    previewBody["should_remesh"] = request.ShouldRemesh;
                    if (!string.IsNullOrWhiteSpace(request.Topology))
                        previewBody["topology"] = request.Topology;
                    if (request.ShouldRemesh && request.TargetPolycount > 0)
                        previewBody["target_polycount"] = request.TargetPolycount;
                }
                if (!string.IsNullOrWhiteSpace(request.PoseMode))
                    previewBody["pose_mode"] = request.PoseMode;

                progress?.Invoke("Creating Meshy preview task…", 0.03f);
                var previewId = CreateTask(TextTo3dPath, previewBody, cancellationToken);
                var previewJson = PollUntilDone(
                    TextTo3dPath, previewId, cancellationToken,
                    ScaleProgress(progress, 0.05f, previewSpan - 0.1f));
                if (!TryReadSucceeded(previewJson, out var previewError))
                    return Failure($"Meshy preview task failed: {previewError}");

                if (!request.ShouldTexture)
                {
                    var previewResult = new GenerateResult
                    {
                        TaskId = previewId,
                        ConsumedCredits = ReadDictInt(previewJson, "consumed_credits"),
                    };
                    return DownloadFormats(
                        previewResult, previewJson, formats, outputPrimaryPath,
                        cancellationToken, ScaleProgress(progress, 0.85f, 0.15f));
                }

                progress?.Invoke("Creating Meshy refine task…", previewSpan + 0.02f);
                var refineBody = new Dictionary<string, object>
                {
                    ["mode"] = "refine",
                    ["preview_task_id"] = previewId,
                    ["enable_pbr"] = request.EnablePbr,
                    ["remove_lighting"] = request.RemoveLighting,
                    ["target_formats"] = new List<object>(formats),
                };
                if (!string.IsNullOrWhiteSpace(request.TextureResolution))
                    refineBody["texture_resolution"] = request.TextureResolution;
                if (!string.IsNullOrWhiteSpace(request.TexturePrompt))
                    refineBody["texture_prompt"] = request.TexturePrompt.Trim();
                if (!string.IsNullOrWhiteSpace(request.AiModel))
                    refineBody["ai_model"] = request.AiModel;

                var refineId = CreateTask(TextTo3dPath, refineBody, cancellationToken);
                var refineJson = PollUntilDone(
                    TextTo3dPath, refineId, cancellationToken,
                    ScaleProgress(progress, previewSpan + 0.05f, 0.85f - previewSpan));
                if (!TryReadSucceeded(refineJson, out var refineError))
                    return Failure($"Meshy refine task failed: {refineError}");

                var refineResult = new GenerateResult
                {
                    TaskId = refineId,
                    ConsumedCredits =
                        ReadDictInt(previewJson, "consumed_credits") +
                        ReadDictInt(refineJson, "consumed_credits"),
                };
                return DownloadFormats(
                    refineResult, refineJson, formats, outputPrimaryPath,
                    cancellationToken, ScaleProgress(progress, 0.9f, 0.1f));
            }
            catch (OperationCanceledException)
            {
                return Failure("Meshy request canceled.");
            }
            catch (Exception ex)
            {
                return Failure(ex.GetBaseException().Message);
            }
        }

        public const string MeshOpRemesh = "remesh";
        public const string MeshOpResize = "resize";
        public const string MeshOpUvUnwrap = "uvUnwrap";

        public struct MeshOpsRequest
        {
            /// <summary>MeshOpRemesh | MeshOpResize | MeshOpUvUnwrap.</summary>
            public string Operation;
            /// <summary>data:application/octet-stream;base64,… (GLB bytes from upstream cook).</summary>
            public string ModelDataUri;
            public string Topology;
            public int TargetPolycount;
            /// <summary>height | longestSide | auto (resize only).</summary>
            public string ResizeMode;
            public double ResizeHeight;
            public double ResizeLongestSide;
            /// <summary>bottom | center (resize only).</summary>
            public string OriginAt;
            /// <summary>Remesh target_formats; resize/uv-unwrap always produce GLB.</summary>
            public List<string> TargetFormats;
        }

        public static GenerateResult GenerateMeshOp(
            MeshOpsRequest request,
            string outputPrimaryPath,
            CancellationToken cancellationToken = default,
            Action<string, float> progress = null)
        {
            var result = ValidateBasic(
                outputPrimaryPath,
                string.IsNullOrWhiteSpace(request.ModelDataUri) ? "Meshy mesh-op input model is empty." : null);
            if (result.HasValue)
                return result.Value;

            string endpoint;
            var body = new Dictionary<string, object>
            {
                ["model_url"] = request.ModelDataUri,
            };
            List<string> formats;
            switch (request.Operation)
            {
                case MeshOpRemesh:
                    endpoint = RemeshPath;
                    formats = NormalizeFormats(request.TargetFormats);
                    body["target_formats"] = new List<object>(formats);
                    if (!string.IsNullOrWhiteSpace(request.Topology))
                        body["topology"] = request.Topology;
                    if (request.TargetPolycount > 0)
                        body["target_polycount"] = request.TargetPolycount;
                    break;
                case MeshOpResize:
                    endpoint = ResizePath;
                    formats = new List<string> { PcgMeshySaveFormats.Glb };
                    switch (request.ResizeMode)
                    {
                        case "longestSide":
                            if (request.ResizeLongestSide <= 0)
                                return Failure("Meshy resize longest side must be greater than zero.");
                            body["resize_longest_side"] = request.ResizeLongestSide;
                            break;
                        case "auto":
                            body["auto_size"] = true;
                            break;
                        default:
                            if (request.ResizeHeight <= 0)
                                return Failure("Meshy resize height must be greater than zero.");
                            body["resize_height"] = request.ResizeHeight;
                            break;
                    }
                    if (!string.IsNullOrWhiteSpace(request.OriginAt))
                        body["origin_at"] = request.OriginAt;
                    break;
                case MeshOpUvUnwrap:
                    endpoint = UvUnwrapPath;
                    formats = new List<string> { PcgMeshySaveFormats.Glb };
                    break;
                default:
                    return Failure($"Meshy mesh-op '{request.Operation}' is not supported.");
            }

            return RunTaskToModel(endpoint, body, formats, outputPrimaryPath, cancellationToken, progress);
        }

        public struct RetextureRequest
        {
            /// <summary>data:application/octet-stream;base64,… (GLB bytes from upstream cook).</summary>
            public string ModelDataUri;
            public string TextStylePrompt;
            /// <summary>Optional style image (URL / data URI). Takes precedence over the text prompt.</summary>
            public string ImageStyleDataUri;
            public string AiModel;
            public bool EnableOriginalUv;
            public bool EnablePbr;
            public string TextureResolution;
            public bool RemoveLighting;
            public List<string> TargetFormats;
        }

        public static GenerateResult GenerateRetexture(
            RetextureRequest request,
            string outputPrimaryPath,
            CancellationToken cancellationToken = default,
            Action<string, float> progress = null)
        {
            var invalid =
                string.IsNullOrWhiteSpace(request.ModelDataUri)
                    ? "Meshy retexture input model is empty."
                    : string.IsNullOrWhiteSpace(request.TextStylePrompt) &&
                      string.IsNullOrWhiteSpace(request.ImageStyleDataUri)
                        ? "Meshy retexture needs a style prompt or a style image."
                        : null;
            var result = ValidateBasic(outputPrimaryPath, invalid);
            if (result.HasValue)
                return result.Value;
            if (string.IsNullOrWhiteSpace(request.TextStylePrompt) == false &&
                request.TextStylePrompt.Length > 600)
                return Failure("Meshy retexture style prompt exceeds 600 characters.");

            var formats = NormalizeFormats(request.TargetFormats);
            var body = new Dictionary<string, object>
            {
                ["model_url"] = request.ModelDataUri,
                ["ai_model"] = string.IsNullOrWhiteSpace(request.AiModel) ? "latest" : request.AiModel,
                ["enable_original_uv"] = request.EnableOriginalUv,
                ["enable_pbr"] = request.EnablePbr,
                ["remove_lighting"] = request.RemoveLighting,
                ["target_formats"] = new List<object>(formats),
            };
            if (!string.IsNullOrWhiteSpace(request.ImageStyleDataUri))
                body["image_style_url"] = request.ImageStyleDataUri;
            else
                body["text_style_prompt"] = request.TextStylePrompt.Trim();
            if (!string.IsNullOrWhiteSpace(request.TextureResolution))
                body["texture_resolution"] = request.TextureResolution;

            return RunTaskToModel(RetexturePath, body, formats, outputPrimaryPath, cancellationToken, progress);
        }

        public struct ImageGenRequest
        {
            /// <summary>false → text-to-image; true → image-to-image (needs reference images).</summary>
            public bool ImageToImage;
            /// <summary>nano-banana | nano-banana-2 | nano-banana-pro | gpt-image-2.</summary>
            public string AiModel;
            public string Prompt;
            public string AspectRatio;
            public bool GenerateMultiView;
            /// <summary>Image-to-image references (URL / data URI), 1–5.</summary>
            public List<string> ReferenceImageDataUris;
        }

        /// <summary>Text/image-to-image. Downloads the first generated image as PNG.</summary>
        public static GenerateResult GenerateImage(
            ImageGenRequest request,
            string outputImagePath,
            CancellationToken cancellationToken = default,
            Action<string, float> progress = null)
        {
            var result = new GenerateResult
            {
                ModelPathsByFormat = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
            };
            if (string.IsNullOrWhiteSpace(PcgMeshySettings.ApiKey))
            {
                result.Error = "Meshy API key is empty. Set it in PCG → Settings or Project Settings → PCG AI.";
                return result;
            }
            if (string.IsNullOrWhiteSpace(request.Prompt))
            {
                result.Error = "Meshy image prompt is empty.";
                return result;
            }
            if (string.IsNullOrWhiteSpace(outputImagePath))
            {
                result.Error = "Meshy output path is empty.";
                return result;
            }

            var references = new List<string>();
            if (request.ReferenceImageDataUris != null)
            {
                foreach (var reference in request.ReferenceImageDataUris)
                {
                    if (!string.IsNullOrWhiteSpace(reference))
                        references.Add(reference.Trim());
                }
            }
            if (request.ImageToImage && references.Count == 0)
            {
                result.Error = "Meshy image-to-image needs at least one reference image.";
                return result;
            }
            if (references.Count > 5)
            {
                result.Error = "Meshy image-to-image supports at most 5 reference images.";
                return result;
            }

            try
            {
                var endpoint = request.ImageToImage ? ImageToImagePath : TextToImagePath;
                var body = new Dictionary<string, object>
                {
                    ["ai_model"] = string.IsNullOrWhiteSpace(request.AiModel) ? "nano-banana" : request.AiModel,
                    ["prompt"] = request.Prompt.Trim(),
                };
                if (request.GenerateMultiView)
                {
                    body["generate_multi_view"] = true;
                }
                else if (!string.IsNullOrWhiteSpace(request.AspectRatio))
                {
                    body["aspect_ratio"] = request.AspectRatio;
                }
                if (request.ImageToImage)
                    body["reference_image_urls"] = new List<object>(references);

                progress?.Invoke("Creating Meshy image task…", 0.05f);
                var taskId = CreateTask(endpoint, body, cancellationToken);
                result.TaskId = taskId;
                var taskJson = PollUntilDone(
                    endpoint, taskId, cancellationToken, ScaleProgress(progress, 0.1f, 0.75f));
                if (!TryReadSucceeded(taskJson, out var taskError))
                {
                    result.Error = $"Meshy image task failed: {taskError}";
                    return result;
                }

                result.ConsumedCredits = ReadDictInt(taskJson, "consumed_credits");
                var imageUrl = ReadFirstStringListEntry(taskJson, "image_urls");
                if (string.IsNullOrEmpty(imageUrl))
                {
                    result.Error = "Meshy image task succeeded but image_urls is empty.";
                    return result;
                }

                progress?.Invoke("Downloading image…", 0.92f);
                DownloadFile(imageUrl, outputImagePath, cancellationToken);
                result.Ok = true;
                result.ModelPath = outputImagePath;
                progress?.Invoke("Done", 1f);
                return result;
            }
            catch (OperationCanceledException)
            {
                result.Error = "Meshy request canceled.";
                return result;
            }
            catch (Exception ex)
            {
                result.Error = ex.GetBaseException().Message;
                return result;
            }
        }

        private static List<string> NormalizeFormats(List<string> formats)
        {
            var result = new List<string>();
            if (formats != null)
            {
                foreach (var raw in formats)
                {
                    if (string.IsNullOrWhiteSpace(raw))
                        continue;
                    var format = raw.Trim().ToLowerInvariant();
                    if (!result.Contains(format))
                        result.Add(format);
                }
            }
            if (result.Count == 0)
                result.Add(PcgMeshySaveFormats.Glb);
            return result;
        }

        private static GenerateResult Failure(string error) =>
            new()
            {
                Error = error,
                ModelPathsByFormat = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
            };

        /// <summary>Returns a failed result when the API key / output path / input is invalid.</summary>
        private static GenerateResult? ValidateBasic(string outputPrimaryPath, string inputError)
        {
            if (string.IsNullOrWhiteSpace(PcgMeshySettings.ApiKey))
                return Failure("Meshy API key is empty. Set it in PCG → Settings or Project Settings → PCG AI.");
            if (inputError != null)
                return Failure(inputError);
            if (string.IsNullOrWhiteSpace(outputPrimaryPath))
                return Failure("Meshy output path is empty.");
            return null;
        }

        private static Action<string, float> ScaleProgress(
            Action<string, float> progress, float windowBase, float windowSpan)
        {
            if (progress == null)
                return null;
            return (message, t) => progress(message, windowBase + Mathf.Clamp01(t) * windowSpan);
        }

        /// <summary>Create → poll → verify SUCCEEDED → download all requested model formats.</summary>
        private static GenerateResult RunTaskToModel(
            string endpointPath,
            Dictionary<string, object> body,
            List<string> formats,
            string outputPrimaryPath,
            CancellationToken ct,
            Action<string, float> progress)
        {
            try
            {
                progress?.Invoke("Creating Meshy task…", 0.05f);
                var taskId = CreateTask(endpointPath, body, ct);
                var taskJson = PollUntilDone(
                    endpointPath, taskId, ct, ScaleProgress(progress, 0.1f, 0.75f));
                if (!TryReadSucceeded(taskJson, out var taskError))
                    return Failure($"Meshy task failed: {taskError}");

                var result = new GenerateResult
                {
                    TaskId = taskId,
                    ConsumedCredits = ReadDictInt(taskJson, "consumed_credits"),
                };
                return DownloadFormats(
                    result, taskJson, formats, outputPrimaryPath, ct,
                    ScaleProgress(progress, 0.88f, 0.12f));
            }
            catch (OperationCanceledException)
            {
                return Failure("Meshy request canceled.");
            }
            catch (Exception ex)
            {
                return Failure(ex.GetBaseException().Message);
            }
        }

        private static bool TryReadSucceeded(Dictionary<string, object> taskJson, out string error)
        {
            error = null;
            var status = ReadDictString(taskJson, "status");
            if (string.Equals(status, "SUCCEEDED", StringComparison.OrdinalIgnoreCase))
                return true;
            error = ReadDictString(taskJson, "task_error")
                    ?? ReadNestedString(taskJson, "task_error", "message")
                    ?? status
                    ?? "FAILED";
            return false;
        }

        /// <summary>Downloads each requested format next to <paramref name="outputPrimaryPath"/>.</summary>
        private static GenerateResult DownloadFormats(
            GenerateResult result,
            Dictionary<string, object> taskJson,
            List<string> formats,
            string outputPrimaryPath,
            CancellationToken ct,
            Action<string, float> progress)
        {
            result.ModelPathsByFormat ??=
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            var dir = Path.GetDirectoryName(outputPrimaryPath);
            var baseName = Path.GetFileNameWithoutExtension(outputPrimaryPath);
            if (string.IsNullOrEmpty(baseName))
            {
                result.Error = "Meshy output path has no file name.";
                return result;
            }

            for (var i = 0; i < formats.Count; i++)
            {
                var format = formats[i];
                var modelUrl = ReadNestedString(taskJson, "model_urls", format);
                if (string.IsNullOrEmpty(modelUrl))
                {
                    result.Error =
                        $"Meshy task succeeded but model_urls.{format} is missing " +
                        $"(requested formats: {string.Join(", ", formats)}).";
                    return result;
                }

                var dest = Path.Combine(
                    dir ?? "",
                    baseName + "." + format.ToLowerInvariant());
                progress?.Invoke(
                    $"Downloading {format.ToUpperInvariant()}…",
                    formats.Count == 1 ? 0.5f : (float)i / (formats.Count - 1));
                DownloadFile(modelUrl, dest, ct);
                result.ModelPathsByFormat[format.ToLowerInvariant()] = dest;
            }

            var primary = PcgMeshySaveFormats.Primary(formats);
            if (!result.ModelPathsByFormat.TryGetValue(primary, out var primaryPath))
            {
                foreach (var kvp in result.ModelPathsByFormat)
                {
                    primaryPath = kvp.Value;
                    break;
                }
            }

            result.Ok = true;
            result.ModelPath = primaryPath;
            progress?.Invoke("Done", 1f);
            return result;
        }

        private static string CreateTask(
            string endpointPath, Dictionary<string, object> body, CancellationToken ct)
        {
            using var content = new StringContent(
                PcgMiniJson.Serialize(body), Encoding.UTF8, "application/json");
            using var msg = new HttpRequestMessage(HttpMethod.Post, DefaultBaseUrl + endpointPath);
            msg.Headers.Authorization = new AuthenticationHeaderValue("Bearer", PcgMeshySettings.ApiKey);
            msg.Content = content;

            using var response = Http.SendAsync(msg, ct).GetAwaiter().GetResult();
            var responseBody = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            if (!response.IsSuccessStatusCode)
            {
                if ((int)response.StatusCode == 404 && endpointPath == UvUnwrapPath)
                {
                    throw new InvalidOperationException(
                        "Meshy UV Unwrap is not enabled for this account (HTTP 404). " +
                        "It is a gray-release feature — contact Meshy support to enable it.");
                }
                throw new InvalidOperationException(
                    $"Meshy create {endpointPath} HTTP {(int)response.StatusCode}: {Truncate(responseBody, 400)}");
            }

            var parsed = PcgMiniJson.Deserialize(responseBody) as Dictionary<string, object>;
            if (parsed == null)
                throw new InvalidOperationException("Meshy create response is not a JSON object.");

            if (parsed.TryGetValue("result", out var resultObj) && resultObj != null)
                return resultObj.ToString();
            if (parsed.TryGetValue("id", out var idObj) && idObj != null)
                return idObj.ToString();
            throw new InvalidOperationException(
                "Meshy create response missing result/id: " + Truncate(responseBody, 400));
        }

        private static Dictionary<string, object> PollUntilDone(
            string endpointPath,
            string taskId,
            CancellationToken ct,
            Action<string, float> progress)
        {
            var url = $"{DefaultBaseUrl}{endpointPath}/{taskId}";
            while (true)
            {
                ct.ThrowIfCancellationRequested();
                using var msg = new HttpRequestMessage(HttpMethod.Get, url);
                msg.Headers.Authorization =
                    new AuthenticationHeaderValue("Bearer", PcgMeshySettings.ApiKey);
                using var response = Http.SendAsync(msg, ct).GetAwaiter().GetResult();
                var body = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                if (!response.IsSuccessStatusCode)
                    throw new InvalidOperationException(
                        $"Meshy poll {endpointPath} HTTP {(int)response.StatusCode}: {Truncate(body, 400)}");

                var parsed = PcgMiniJson.Deserialize(body) as Dictionary<string, object>
                             ?? throw new InvalidOperationException("Meshy poll response is not a JSON object.");
                var status = ReadDictString(parsed, "status") ?? "";
                var prog = ReadDictFloat(parsed, "progress");
                progress?.Invoke($"Meshy {status} ({prog:0}%)", Mathf.Clamp01(prog / 100f));

                if (string.Equals(status, "SUCCEEDED", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(status, "FAILED", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(status, "CANCELED", StringComparison.OrdinalIgnoreCase))
                {
                    return parsed;
                }

                Thread.Sleep(5000);
            }
        }

        private static void DownloadFile(string url, string outputPath, CancellationToken ct)
        {
            using var response = Http.GetAsync(url, ct).GetAwaiter().GetResult();
            response.EnsureSuccessStatusCode();
            var bytes = response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult();
            var dir = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrEmpty(dir))
                Directory.CreateDirectory(dir);
            File.WriteAllBytes(outputPath, bytes);
        }

        private static string ReadDictString(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return null;
            return value.ToString();
        }

        private static int ReadDictInt(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return 0;
            if (value is int i)
                return i;
            if (value is long l)
                return (int)l;
            if (value is double d)
                return (int)d;
            return int.TryParse(value.ToString(), out var parsed) ? parsed : 0;
        }

        private static float ReadDictFloat(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return 0f;
            if (value is float f)
                return f;
            if (value is double d)
                return (float)d;
            if (value is int i)
                return i;
            if (value is long l)
                return l;
            return float.TryParse(value.ToString(), out var parsed) ? parsed : 0f;
        }

        private static string ReadNestedString(
            Dictionary<string, object> dict, string objectKey, string fieldKey)
        {
            if (dict == null || !dict.TryGetValue(objectKey, out var nested) || nested == null)
                return null;
            if (nested is Dictionary<string, object> nestedDict)
                return ReadDictString(nestedDict, fieldKey);
            return null;
        }

        private static string ReadFirstStringListEntry(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return null;
            if (value is List<object> list && list.Count > 0 && list[0] != null)
                return list[0].ToString();
            if (value is System.Collections.IEnumerable enumerable && value is not string)
            {
                foreach (var entry in enumerable)
                    return entry?.ToString();
            }
            return null;
        }

        private static string Truncate(string value, int max)
        {
            if (string.IsNullOrEmpty(value) || value.Length <= max)
                return value ?? "";
            return value.Substring(0, max) + "…";
        }
    }

    /// <summary>
    /// Thin Tripo Image-to-3D HTTP client (upload → create → poll → download GLB).
    /// </summary>
    public static class PcgTripoClient
    {
        public const string DefaultBaseUrl = PcgTripoSettings.BaseUrlGlobal;
        public const string FilesPath = "/files";
        public const string ImageToModelPath = "/generation/image-to-model";
        public const string TasksPath = "/tasks";

        private static HttpClient Http =>
            PcgThirdPartyHttpSettings.GetHttpClient(TimeSpan.FromMinutes(15));

        public struct GenerateRequest
        {
            public string ImageUrl;
            public byte[] ImageBytes;
            public string ImageExtension;
            public string ModelVersion;
            public bool Texture;
            public bool Pbr;
            public int FaceLimit;
            /// <summary>Captured on main thread before background HTTP work.</summary>
            public string ApiKey;
        }

        public struct GenerateResult
        {
            public bool Ok;
            public string TaskId;
            public string ModelPath;
            public string Error;
            public int ConsumedCredits;
        }

        public static GenerateResult GenerateAndDownload(
            GenerateRequest request,
            string outputPath,
            CancellationToken cancellationToken = default,
            Action<string, float> progress = null)
        {
            var result = new GenerateResult();
            var apiKey = request.ApiKey?.Trim() ?? "";
            if (string.IsNullOrWhiteSpace(apiKey))
                apiKey = PcgTripoSettings.ApiKey?.Trim() ?? "";
            if (string.IsNullOrWhiteSpace(apiKey))
            {
                result.Error =
                    "Tripo API key is empty. Set it in PCG → Settings or Project Settings → PCG AI (Tripo section).";
                return result;
            }

            if (string.IsNullOrWhiteSpace(outputPath))
            {
                result.Error = "Tripo output path is empty.";
                return result;
            }

            var hasUrl = !string.IsNullOrWhiteSpace(request.ImageUrl);
            var hasBytes = request.ImageBytes != null && request.ImageBytes.Length > 0;
            if (!hasUrl && !hasBytes)
            {
                result.Error = "Tripo image is empty.";
                return result;
            }

            try
            {
                var baseUrl = PcgTripoSettings.BaseUrl;
                string fileToken = null;
                if (!hasUrl)
                {
                    progress?.Invoke("Uploading image to Tripo…", 0.05f);
                    fileToken = UploadImageWithRegionFallback(
                        request.ImageBytes, request.ImageExtension, apiKey, ref baseUrl, progress, cancellationToken);
                    if (string.IsNullOrEmpty(fileToken))
                    {
                        result.Error = "Tripo file upload returned no file_token.";
                        return result;
                    }
                }

                progress?.Invoke("Creating Tripo task…", 0.1f);
                var taskId = hasUrl
                    ? CreateImageToModelTaskWithRegionFallback(
                        request, fileToken, apiKey, ref baseUrl, progress, cancellationToken)
                    : CreateImageToModelTask(request, fileToken, apiKey, baseUrl, cancellationToken);
                if (string.IsNullOrEmpty(taskId))
                {
                    result.Error = "Tripo create task returned no task_id.";
                    return result;
                }

                result.TaskId = taskId;
                progress?.Invoke($"Tripo task {taskId}…", 0.15f);

                var taskData = PollUntilDone(taskId, apiKey, baseUrl, cancellationToken, progress);
                var status = ReadDictString(taskData, "status") ?? "";
                if (!string.Equals(status, "success", StringComparison.OrdinalIgnoreCase))
                {
                    var taskError = ReadDictString(taskData, "message")
                                    ?? status
                                    ?? "failed";
                    result.Error = $"Tripo task failed: {taskError}";
                    return result;
                }

                result.ConsumedCredits = ReadDictInt(taskData, "credits_consumed");
                var output = ReadDictObject(taskData, "output");
                var modelUrl = PickModelUrl(output, request.Pbr);
                if (string.IsNullOrEmpty(modelUrl))
                {
                    result.Error = "Tripo task succeeded but output.model_url is missing.";
                    return result;
                }

                progress?.Invoke("Downloading GLB…", 0.92f);
                DownloadFile(modelUrl, outputPath, cancellationToken);

                result.Ok = true;
                result.ModelPath = outputPath;
                progress?.Invoke("Done", 1f);
                return result;
            }
            catch (OperationCanceledException)
            {
                result.Error = "Tripo request canceled.";
                return result;
            }
            catch (Exception ex)
            {
                result.Error = ex.GetBaseException().Message;
                return result;
            }
        }

        private static string UploadImageWithRegionFallback(
            byte[] bytes,
            string extension,
            string apiKey,
            ref string baseUrl,
            Action<string, float> progress,
            CancellationToken ct)
        {
            try
            {
                return UploadImage(bytes, extension, apiKey, baseUrl, ct);
            }
            catch (InvalidOperationException ex) when (IsTripoUnauthorized(ex))
            {
                var alternate = baseUrl == PcgTripoSettings.BaseUrlChina
                    ? PcgTripoSettings.BaseUrlGlobal
                    : PcgTripoSettings.BaseUrlChina;
                if (string.Equals(baseUrl, alternate, StringComparison.OrdinalIgnoreCase))
                    throw;

                progress?.Invoke("Tripo upload 401 — retrying alternate API region…", 0.06f);
                baseUrl = alternate;
                return UploadImage(bytes, extension, apiKey, baseUrl, ct);
            }
        }

        private static string CreateImageToModelTaskWithRegionFallback(
            GenerateRequest request,
            string fileToken,
            string apiKey,
            ref string baseUrl,
            Action<string, float> progress,
            CancellationToken ct)
        {
            try
            {
                return CreateImageToModelTask(request, fileToken, apiKey, baseUrl, ct);
            }
            catch (InvalidOperationException ex) when (IsTripoUnauthorized(ex))
            {
                var alternate = baseUrl == PcgTripoSettings.BaseUrlChina
                    ? PcgTripoSettings.BaseUrlGlobal
                    : PcgTripoSettings.BaseUrlChina;
                if (string.Equals(baseUrl, alternate, StringComparison.OrdinalIgnoreCase))
                    throw;

                progress?.Invoke("Tripo create 401 — retrying alternate API region…", 0.12f);
                baseUrl = alternate;
                return CreateImageToModelTask(request, fileToken, apiKey, baseUrl, ct);
            }
        }

        private static bool IsTripoUnauthorized(InvalidOperationException ex) =>
            ex.Message != null && ex.Message.IndexOf("HTTP 401", StringComparison.OrdinalIgnoreCase) >= 0;

        private static string UploadImage(
            byte[] bytes, string extension, string apiKey, string baseUrl, CancellationToken ct)
        {
            var ext = NormalizeExtension(extension);
            using var content = new MultipartFormDataContent();
            content.Add(new ByteArrayContent(bytes), "file", "image." + ext);

            using var msg = new HttpRequestMessage(HttpMethod.Post, baseUrl + FilesPath);
            msg.Headers.Authorization = new AuthenticationHeaderValue("Bearer", apiKey);
            msg.Content = content;

            using var response = Http.SendAsync(msg, ct).GetAwaiter().GetResult();
            var body = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            if (!response.IsSuccessStatusCode)
            {
                LogTripoHttpFailure("upload", baseUrl + FilesPath, apiKey, response.StatusCode, body);
                throw new InvalidOperationException(FormatTripoHttpError("upload", response.StatusCode, body));
            }

            var data = ParseDataObject(body, "Tripo upload");
            return ReadDictString(data, "file_token");
        }

        private static string CreateImageToModelTask(
            GenerateRequest request, string fileToken, string apiKey, string baseUrl, CancellationToken ct)
        {
            var body = new Dictionary<string, object>
            {
                ["type"] = "image_to_model",
                ["model_version"] = string.IsNullOrWhiteSpace(request.ModelVersion)
                    ? "v3.1-20260211"
                    : request.ModelVersion.Trim(),
                ["texture"] = request.Texture,
                ["pbr"] = request.Pbr,
            };
            if (request.FaceLimit > 0)
                body["face_limit"] = request.FaceLimit;

            var fileType = NormalizeExtension(request.ImageExtension);
            if (!string.IsNullOrEmpty(fileToken))
                body["file"] = new Dictionary<string, object>
                {
                    ["type"] = fileType,
                    ["file_token"] = fileToken,
                };
            else
                body["file"] = new Dictionary<string, object>
                {
                    ["type"] = fileType,
                    ["url"] = request.ImageUrl,
                };

            using var content = new StringContent(
                PcgMiniJson.Serialize(body), Encoding.UTF8, "application/json");
            using var msg = new HttpRequestMessage(HttpMethod.Post, baseUrl + ImageToModelPath);
            msg.Headers.Authorization = new AuthenticationHeaderValue("Bearer", apiKey);
            msg.Content = content;

            using var response = Http.SendAsync(msg, ct).GetAwaiter().GetResult();
            var responseBody = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            if (!response.IsSuccessStatusCode)
            {
                LogTripoHttpFailure(
                    "create", baseUrl + ImageToModelPath, apiKey, response.StatusCode, responseBody);
                throw new InvalidOperationException(
                    FormatTripoHttpError("create", response.StatusCode, responseBody));
            }

            var data = ParseDataObject(responseBody, "Tripo create");
            return ReadDictString(data, "task_id");
        }

        private static Dictionary<string, object> PollUntilDone(
            string taskId,
            string apiKey,
            string baseUrl,
            CancellationToken ct,
            Action<string, float> progress)
        {
            var url = baseUrl + TasksPath + "/" + taskId;
            while (true)
            {
                ct.ThrowIfCancellationRequested();
                using var msg = new HttpRequestMessage(HttpMethod.Get, url);
                msg.Headers.Authorization =
                    new AuthenticationHeaderValue("Bearer", apiKey);
                using var response = Http.SendAsync(msg, ct).GetAwaiter().GetResult();
                var body = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                if (!response.IsSuccessStatusCode)
                {
                    LogTripoHttpFailure("poll", url, apiKey, response.StatusCode, body);
                    throw new InvalidOperationException(
                        FormatTripoHttpError("poll", response.StatusCode, body));
                }

                var data = ParseDataObject(body, "Tripo poll");
                var status = ReadDictString(data, "status") ?? "";
                var prog = ReadDictInt(data, "progress");
                progress?.Invoke(
                    $"Tripo {status} ({prog}%)",
                    Mathf.Clamp01(0.15f + prog * 0.007f));

                if (string.Equals(status, "success", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(status, "failed", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(status, "cancelled", StringComparison.OrdinalIgnoreCase))
                    return data;

                Thread.Sleep(2000);
            }
        }

        private static string PickModelUrl(Dictionary<string, object> output, bool preferPbr)
        {
            if (output == null)
                return null;
            if (preferPbr)
            {
                var pbr = ReadDictString(output, "pbr_model");
                if (!string.IsNullOrEmpty(pbr))
                    return pbr;
            }
            return ReadDictString(output, "model_url")
                   ?? ReadDictString(output, "model")
                   ?? ReadDictString(output, "base_model");
        }

        private static void DownloadFile(string url, string outputPath, CancellationToken ct)
        {
            using var response = Http.GetAsync(url, ct).GetAwaiter().GetResult();
            response.EnsureSuccessStatusCode();
            var bytes = response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult();
            var dir = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrEmpty(dir))
                Directory.CreateDirectory(dir);
            File.WriteAllBytes(outputPath, bytes);
        }

        private static Dictionary<string, object> ParseDataObject(string body, string context)
        {
            var parsed = PcgMiniJson.Deserialize(body) as Dictionary<string, object>
                         ?? throw new InvalidOperationException($"{context} response is not a JSON object.");
            var code = ReadDictInt(parsed, "code");
            if (code != 0 && parsed.ContainsKey("code"))
            {
                var message = ReadDictString(parsed, "message") ?? body;
                throw new InvalidOperationException($"{context} API error (code={code}): {Truncate(message, 400)}");
            }
            var data = ReadDictObject(parsed, "data");
            if (data == null)
                throw new InvalidOperationException($"{context} response missing data: {Truncate(body, 400)}");
            return data;
        }

        private static string NormalizeExtension(string extension)
        {
            if (string.IsNullOrWhiteSpace(extension))
                return "png";
            extension = extension.Trim().TrimStart('.').ToLowerInvariant();
            return extension == "jpeg" ? "jpg" : extension;
        }

        private static Dictionary<string, object> ReadDictObject(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return null;
            return value as Dictionary<string, object>;
        }

        private static string ReadDictString(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return null;
            return value.ToString();
        }

        private static int ReadDictInt(Dictionary<string, object> dict, string key)
        {
            if (dict == null || !dict.TryGetValue(key, out var value) || value == null)
                return 0;
            if (value is int i)
                return i;
            if (value is long l)
                return (int)l;
            if (value is double d)
                return (int)d;
            return int.TryParse(value.ToString(), out var parsed) ? parsed : 0;
        }

        private static string FormatTripoHttpError(
            string step, System.Net.HttpStatusCode status, string body)
        {
            var detail = Truncate(body, 400);
            if (status == System.Net.HttpStatusCode.Unauthorized)
                return
                    $"Tripo {step} HTTP 401: API key rejected. " +
                    "Use a Tripo key from platform.tripo3d.ai (PCG → Settings → Tripo section). " +
                    "Tripo keys are separate from Meshy. " + detail;
            return $"Tripo {step} HTTP {(int)status}: {detail}";
        }

        private static void LogTripoHttpFailure(
            string step,
            string requestUrl,
            string apiKey,
            System.Net.HttpStatusCode status,
            string body)
        {
            var proxy = PcgThirdPartyHttpSettings.UseHttpProxy
                ? PcgThirdPartyHttpSettings.ProxyUrl
                : "direct";
            var region = requestUrl != null &&
                         requestUrl.IndexOf("tripo3d.com", StringComparison.OrdinalIgnoreCase) >= 0
                ? PcgTripoSettings.ApiRegionChina
                : PcgTripoSettings.ApiRegionGlobal;
            var log =
                "[PCG] Tripo HTTP failure — copy from Console:\n" +
                $"step: {step}\n" +
                $"url: {requestUrl}\n" +
                $"status: {(int)status} {status}\n" +
                $"region: {region}\n" +
                $"proxy: {proxy}\n" +
                $"api_key: {MaskApiKey(apiKey)}\n" +
                "response_body:\n" +
                (body ?? "");
            Debug.LogError(log);
        }

        private static string MaskApiKey(string apiKey)
        {
            if (string.IsNullOrWhiteSpace(apiKey))
                return "(empty)";
            apiKey = apiKey.Trim();
            if (apiKey.Length <= 8)
                return apiKey.Substring(0, Math.Min(2, apiKey.Length)) + "…";
            return apiKey.Substring(0, 8) + "… (" + apiKey.Length + " chars)";
        }

        private static string Truncate(string value, int max)
        {
            if (string.IsNullOrEmpty(value) || value.Length <= max)
                return value ?? "";
            return value.Substring(0, max) + "…";
        }
    }
}
