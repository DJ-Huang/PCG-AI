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

        private static readonly HttpClient s_Http = CreateClient();

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

        private static HttpClient CreateClient()
        {
            // Force Meshy through Clash mixed-port. System DIRECT cannot reach
            // api.meshy.ai from this network; browser works only via proxy.
            var handler = new HttpClientHandler
            {
                UseProxy = true,
                Proxy = new System.Net.WebProxy("http://127.0.0.1:7897"),
            };
            var client = new HttpClient(handler);
            client.Timeout = TimeSpan.FromMinutes(15);
            return client;
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

            using var response = s_Http.SendAsync(msg, ct).GetAwaiter().GetResult();
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
                using var response = s_Http.SendAsync(msg, ct).GetAwaiter().GetResult();
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
            using var response = s_Http.GetAsync(url, ct).GetAwaiter().GetResult();
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
}
