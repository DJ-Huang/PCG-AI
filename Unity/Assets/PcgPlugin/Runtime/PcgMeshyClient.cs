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
    /// Thin Meshy Image-to-3D HTTP client (create → poll → download selected formats).
    /// </summary>
    public static class PcgMeshyClient
    {
        public const string DefaultBaseUrl = "https://api.meshy.ai";
        public const string ImageTo3dPath = "/openapi/v1/image-to-3d";

        private static HttpClient Http =>
            PcgThirdPartyHttpSettings.GetHttpClient(TimeSpan.FromMinutes(15));

        public struct GenerateRequest
        {
            public string ImageDataUri;
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
            var result = new GenerateResult
            {
                ModelPathsByFormat = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
            };
            if (string.IsNullOrWhiteSpace(PcgMeshySettings.ApiKey))
            {
                result.Error = "Meshy API key is empty. Set it in PCG → Settings or Project Settings → PCG AI.";
                return result;
            }

            if (string.IsNullOrWhiteSpace(request.ImageDataUri))
            {
                result.Error = "Meshy image is empty.";
                return result;
            }

            if (string.IsNullOrWhiteSpace(outputPrimaryPath))
            {
                result.Error = "Meshy output path is empty.";
                return result;
            }

            var formats = NormalizeFormats(request.TargetFormats);
            try
            {
                progress?.Invoke("Creating Meshy task…", 0.05f);
                var taskId = CreateImageTo3dTask(request, formats, cancellationToken);
                if (string.IsNullOrEmpty(taskId))
                {
                    result.Error = "Meshy create task returned no task id.";
                    return result;
                }

                result.TaskId = taskId;
                progress?.Invoke($"Meshy task {taskId}…", 0.1f);

                var taskJson = PollUntilDone(taskId, cancellationToken, progress);
                var status = ReadDictString(taskJson, "status");
                if (!string.Equals(status, "SUCCEEDED", StringComparison.OrdinalIgnoreCase))
                {
                    var taskError = ReadDictString(taskJson, "task_error")
                                    ?? ReadNestedString(taskJson, "task_error", "message")
                                    ?? status
                                    ?? "FAILED";
                    result.Error = $"Meshy task failed: {taskError}";
                    return result;
                }

                result.ConsumedCredits = ReadDictInt(taskJson, "consumed_credits");
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
                        0.9f + 0.08f * ((i + 1f) / formats.Count));
                    DownloadFile(modelUrl, dest, cancellationToken);
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

        private static string CreateImageTo3dTask(
            GenerateRequest request, List<string> formats, CancellationToken ct)
        {
            var body = new Dictionary<string, object>
            {
                ["image_url"] = request.ImageDataUri,
                ["ai_model"] = string.IsNullOrWhiteSpace(request.AiModel) ? "latest" : request.AiModel,
                ["enable_pbr"] = request.EnablePbr,
                ["should_texture"] = request.ShouldTexture,
                ["should_remesh"] = request.ShouldRemesh,
                ["target_formats"] = new List<object>(formats),
            };
            if (request.ShouldRemesh && request.TargetPolycount > 0)
                body["target_polycount"] = request.TargetPolycount;

            using var content = new StringContent(
                PcgMiniJson.Serialize(body), Encoding.UTF8, "application/json");
            using var msg = new HttpRequestMessage(HttpMethod.Post, DefaultBaseUrl + ImageTo3dPath);
            msg.Headers.Authorization = new AuthenticationHeaderValue("Bearer", PcgMeshySettings.ApiKey);
            msg.Content = content;

            using var response = Http.SendAsync(msg, ct).GetAwaiter().GetResult();
            var responseBody = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            if (!response.IsSuccessStatusCode)
                throw new InvalidOperationException(
                    $"Meshy create HTTP {(int)response.StatusCode}: {Truncate(responseBody, 400)}");

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
            string taskId,
            CancellationToken ct,
            Action<string, float> progress)
        {
            var url = $"{DefaultBaseUrl}{ImageTo3dPath}/{taskId}";
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
                        $"Meshy poll HTTP {(int)response.StatusCode}: {Truncate(body, 400)}");

                var parsed = PcgMiniJson.Deserialize(body) as Dictionary<string, object>
                             ?? throw new InvalidOperationException("Meshy poll response is not a JSON object.");
                var status = ReadDictString(parsed, "status") ?? "";
                var prog = ReadDictFloat(parsed, "progress");
                progress?.Invoke($"Meshy {status} ({prog:0}%)", Mathf.Clamp01(0.1f + prog * 0.008f));

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
