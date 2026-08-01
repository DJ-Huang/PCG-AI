using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Threading;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// HTTP client for the localhost pcg-server cook backend.
    /// Request: multipart/form-data. Response: application/x-pcg-cook-result-v1.
    /// </summary>
    public static class PcgCookClient
    {
        public const string DefaultBaseUrl = "http://127.0.0.1:17890";
        public const string ResultContentType = "application/x-pcg-cook-result-v1";
        private const uint ResultMagic = 0x52474350u; // 'PCGR'
        private const uint ResultVersion = 1u;

        private static readonly HttpClient s_Http = CreateClient();

#if UNITY_EDITOR
        // EditorPrefs is main-thread-only; async preview cooks read BaseUrl from Task.Run.
        private static string s_CachedBaseUrl = DefaultBaseUrl;

        [UnityEditor.InitializeOnLoadMethod]
        private static void EditorLoadBaseUrl()
        {
            s_CachedBaseUrl = UnityEditor.EditorPrefs.GetString("PCG.CookServer.BaseUrl", DefaultBaseUrl);
        }
#endif

        private static HttpClient CreateClient()
        {
            var client = new HttpClient();
            client.Timeout = TimeSpan.FromMinutes(10);
            return client;
        }

        public static string BaseUrl
        {
            get
            {
#if UNITY_EDITOR
                return s_CachedBaseUrl;
#else
                return DefaultBaseUrl;
#endif
            }
            set
            {
#if UNITY_EDITOR
                var normalized = string.IsNullOrWhiteSpace(value) ? DefaultBaseUrl : value.TrimEnd('/');
                s_CachedBaseUrl = normalized;
                UnityEditor.EditorPrefs.SetString("PCG.CookServer.BaseUrl", normalized);
#endif
            }
        }

        public static (bool ok, string version, string error) TryHealthCheck(int timeoutMs = 2000)
        {
            try
            {
                using var cts = new CancellationTokenSource(timeoutMs);
                var task = s_Http.GetAsync(BaseUrl + "/v1/health", cts.Token);
                task.Wait(cts.Token);
                var response = task.Result;
                var body = response.Content.ReadAsStringAsync().Result;
                if (!response.IsSuccessStatusCode)
                    return (false, null, $"HTTP {(int)response.StatusCode}: {body}");

                var version = ExtractJsonString(body, "version") ?? body;
                return (true, version, null);
            }
            catch (Exception ex)
            {
                return (false, null, ex.GetBaseException().Message);
            }
        }

        public static string GetVersion()
        {
            var (ok, version, error) = TryHealthCheck();
            return ok ? version : $"unavailable ({error})";
        }

        public static (PcgResultCode code, string error) ValidateGraph(string json)
        {
            try
            {
                using var content = new StringContent(json ?? string.Empty, Encoding.UTF8, "application/json");
                var response = s_Http.PostAsync(BaseUrl + "/v1/validate", content).Result;
                var body = response.Content.ReadAsStringAsync().Result;
                if (!response.IsSuccessStatusCode)
                    return (PcgResultCode.Execution, $"HTTP {(int)response.StatusCode}: {body}");

                var ok = body.IndexOf("\"ok\":true", StringComparison.Ordinal) >= 0 ||
                         body.IndexOf("\"ok\": true", StringComparison.Ordinal) >= 0;
                var err = ExtractJsonString(body, "error") ?? string.Empty;
                if (ok)
                    return (PcgResultCode.Ok, err);

                var codeText = ExtractJsonNumber(body, "code");
                var code = int.TryParse(codeText, out var c) ? (PcgResultCode)c : PcgResultCode.InvalidJson;
                return (code, string.IsNullOrEmpty(err) ? body : err);
            }
            catch (Exception ex)
            {
                return (PcgResultCode.Execution, ex.GetBaseException().Message);
            }
        }

        public static void ClearCookCache()
        {
            try
            {
                using var cts = new CancellationTokenSource(5000);
                var task = s_Http.PostAsync(
                    BaseUrl + "/v1/cache/clear",
                    new ByteArrayContent(Array.Empty<byte>()),
                    cts.Token);
                task.Wait(cts.Token);
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[PCG] HTTP cache clear failed: {ex.GetBaseException().Message}");
            }
        }

        public static bool TryExportFbx(
            byte[] geometryBinary,
            float scale,
            bool generateNormals,
            out byte[] fbxBytes,
            out string fbxVersion,
            out string error)
        {
            fbxBytes = null;
            fbxVersion = null;
            error = null;
            if (geometryBinary == null || geometryBinary.Length == 0)
            {
                error = "Cook did not return polygon geometry.";
                return false;
            }

            try
            {
                using var form = new MultipartFormDataContent();
                var meta =
                    $"{{\"scale\":{FormatDouble(scale)},\"generate_normals\":{(generateNormals ? "true" : "false")}}}";
                form.Add(new StringContent(meta, Encoding.UTF8, "application/json"), "meta");
                form.Add(new ByteArrayContent(geometryBinary), "geometry", "geometry.bin");

                var response = s_Http.PostAsync(BaseUrl + "/v1/export-fbx", form).Result;
                var body = response.Content.ReadAsByteArrayAsync().Result;
                if (!response.IsSuccessStatusCode)
                {
                    error = Encoding.UTF8.GetString(body);
                    return false;
                }

                if (response.Headers.TryGetValues("X-Pcg-Fbx-Version", out var values))
                {
                    foreach (var v in values)
                    {
                        fbxVersion = v;
                        break;
                    }
                }

                fbxBytes = body;
                return true;
            }
            catch (Exception ex)
            {
                error = $"pcg-server FBX export failed: {ex.GetBaseException().Message}";
                return false;
            }
        }

        public static void RequestCancel()
        {
            try
            {
                using var cts = new CancellationTokenSource(2000);
                var task = s_Http.PostAsync(
                    BaseUrl + "/v1/cancel",
                    new ByteArrayContent(Array.Empty<byte>()),
                    cts.Token);
                task.Wait(cts.Token);
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[PCG] HTTP cancel failed: {ex.GetBaseException().Message}");
            }
        }

        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines,
            IReadOnlyList<PcgHeightFieldUpload> heightfields)
        {
            try
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                using var content = BuildMultipart(json, seed, textures, meshes, splines, heightfields);
                var responseTask = s_Http.PostAsync(BaseUrl + "/v1/cook", content);
                responseTask.Wait();
                var response = responseTask.Result;
                var bytesTask = response.Content.ReadAsByteArrayAsync();
                bytesTask.Wait();
                var body = bytesTask.Result;
                sw.Stop();

                if (!response.IsSuccessStatusCode)
                {
                    var text = Encoding.UTF8.GetString(body);
                    return (PcgResultCode.Execution, new PcgGraphExecuteResult
                    {
                        Error = $"HTTP {(int)response.StatusCode}: {text}",
                    });
                }

                var mediaType = response.Content.Headers.ContentType?.MediaType;
                if (!string.Equals(mediaType, ResultContentType, StringComparison.OrdinalIgnoreCase) &&
                    body.Length >= 4 &&
                    BitConverter.ToUInt32(body, 0) != ResultMagic)
                {
                    return (PcgResultCode.Execution, new PcgGraphExecuteResult
                    {
                        Error = $"Unexpected response Content-Type '{mediaType}'",
                    });
                }

                return DecodeResult(body, sw.Elapsed.TotalMilliseconds);
            }
            catch (Exception ex)
            {
                return (PcgResultCode.Execution, new PcgGraphExecuteResult
                {
                    Error = $"pcg-server request failed: {ex.GetBaseException().Message}. " +
                            "Start the backend with scripts/run-pcg-server.sh (or .ps1).",
                });
            }
        }

        private static MultipartFormDataContent BuildMultipart(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines,
            IReadOnlyList<PcgHeightFieldUpload> heightfields)
        {
            var form = new MultipartFormDataContent();
            var meta = $"{{\"seed\":{seed},\"api_version\":1,\"job_id\":\"{Guid.NewGuid():N}\"}}";
            form.Add(new StringContent(meta, Encoding.UTF8, "application/json"), "meta");
            form.Add(new StringContent(json ?? string.Empty, Encoding.UTF8, "application/json"), "graph");

            if (textures != null)
            {
                for (var i = 0; i < textures.Count; i++)
                {
                    var t = textures[i];
                    if (t == null || t.Rgba == null)
                        continue;
                    var metaJson =
                        $"{{\"slot_id\":{JsonString(t.SlotId)},\"width\":{t.Width},\"height\":{t.Height}}}";
                    form.Add(new StringContent(metaJson, Encoding.UTF8, "application/json"), $"tex_meta_{i}");
                    form.Add(ToBinaryContent(t.Rgba), $"tex_data_{i}", $"tex_data_{i}.bin");
                }
            }

            if (meshes != null)
            {
                for (var i = 0; i < meshes.Count; i++)
                {
                    var m = meshes[i];
                    if (m == null || m.Positions == null || m.Indices == null)
                        continue;
                    var metaJson =
                        $"{{\"slot_id\":{JsonString(m.SlotId)},\"vertex_count\":{m.VertexCount},\"index_count\":{m.IndexCount}}}";
                    form.Add(new StringContent(metaJson, Encoding.UTF8, "application/json"), $"mesh_meta_{i}");
                    form.Add(ToBinaryContent(m.Positions), $"mesh_pos_{i}", $"mesh_pos_{i}.bin");
                    form.Add(ToBinaryContent(m.Indices), $"mesh_idx_{i}", $"mesh_idx_{i}.bin");
                }
            }

            if (splines != null)
            {
                for (var i = 0; i < splines.Count; i++)
                {
                    var s = splines[i];
                    if (s == null || s.Positions == null || s.SplinePointCounts == null || s.Closed == null)
                        continue;
                    var metaJson =
                        $"{{\"slot_id\":{JsonString(s.SlotId)},\"spline_count\":{s.SplineCount}}}";
                    form.Add(new StringContent(metaJson, Encoding.UTF8, "application/json"), $"spline_meta_{i}");
                    form.Add(ToBinaryContent(s.SplinePointCounts), $"spline_counts_{i}", $"spline_counts_{i}.bin");
                    form.Add(ToBinaryContent(s.Positions), $"spline_pos_{i}", $"spline_pos_{i}.bin");
                    form.Add(new ByteArrayContent(s.Closed), $"spline_closed_{i}", $"spline_closed_{i}.bin");
                }
            }

            if (heightfields != null)
            {
                for (var i = 0; i < heightfields.Count; i++)
                {
                    var h = heightfields[i];
                    if (h == null || h.Heights == null)
                        continue;
                    var metaJson =
                        $"{{\"slot_id\":{JsonString(h.SlotId)},\"resolution_x\":{h.ResolutionX}," +
                        $"\"resolution_z\":{h.ResolutionZ},\"size_x\":{FormatDouble(h.SizeX)}," +
                        $"\"size_z\":{FormatDouble(h.SizeZ)},\"center_x\":{FormatDouble(h.CenterX)}," +
                        $"\"center_y\":{FormatDouble(h.CenterY)},\"center_z\":{FormatDouble(h.CenterZ)}," +
                        $"\"sampling\":{h.Sampling},\"orientation\":{h.Orientation}}}";
                    form.Add(new StringContent(metaJson, Encoding.UTF8, "application/json"), $"hf_meta_{i}");
                    form.Add(ToBinaryContent(h.Heights), $"hf_height_{i}", $"hf_height_{i}.bin");
                    if (h.Mask != null && h.Mask.Length > 0)
                        form.Add(ToBinaryContent(h.Mask), $"hf_mask_{i}", $"hf_mask_{i}.bin");
                }
            }

            return form;
        }

        private static (PcgResultCode code, PcgGraphExecuteResult result) DecodeResult(
            byte[] body,
            double roundTripMs)
        {
            if (body == null || body.Length < 48)
            {
                return (PcgResultCode.Execution, new PcgGraphExecuteResult
                {
                    Error = "Cook result too short",
                });
            }

            var offset = 0;
            var magic = ReadU32(body, ref offset);
            var version = ReadU32(body, ref offset);
            if (magic != ResultMagic || version != ResultVersion)
            {
                return (PcgResultCode.Execution, new PcgGraphExecuteResult
                {
                    Error = $"Bad cook result header magic=0x{magic:X8} version={version}",
                });
            }

            var code = (PcgResultCode)ReadI32(body, ref offset);
            var kind = (PcgExecuteKind)ReadU32(body, ref offset);
            var nodesExecuted = ReadI32(body, ref offset);
            var nodesSkipped = ReadI32(body, ref offset);
            var graphExecuteMs = ReadF64(body, ref offset);
            var binaryWriteMs = ReadF64(body, ref offset);
            var pointCount = (int)ReadU32(body, ref offset);
            var pointAttrFlags = ReadU32(body, ref offset);
            var vertexCount = ReadI32(body, ref offset);
            var indexCount = ReadI32(body, ref offset);

            var error = ReadBlobUtf8(body, ref offset);
            var json = ReadBlobUtf8(body, ref offset);
            var mesh = ReadBlobBytes(body, ref offset);
            var points = ReadBlobBytes(body, ref offset);
            var geometry = ReadBlobBytes(body, ref offset);
            var heightfield = ReadBlobBytes(body, ref offset);
            var perfJson = ReadBlobUtf8(body, ref offset);

            var perf = new PcgCookPerfReport
            {
                NativeCallMs = roundTripMs,
                GraphExecuteMs = graphExecuteMs,
                BinaryWriteMs = binaryWriteMs,
                CookNodesExecuted = nodesExecuted,
                CookNodesSkipped = nodesSkipped,
                NodeEntries = PcgCookPerfJson.TryParse(perfJson),
            };

            if (code != PcgResultCode.Ok)
            {
                return (code, new PcgGraphExecuteResult
                {
                    Error = string.IsNullOrEmpty(error) ? code.ToString() : error,
                    Perf = perf,
                    CookNodesExecuted = nodesExecuted,
                    CookNodesSkipped = nodesSkipped,
                });
            }

            return (code, new PcgGraphExecuteResult
            {
                Kind = kind,
                Json = json,
                MeshBinary = mesh != null && mesh.Length > 0 ? mesh : null,
                PointBinary = points != null && points.Length > 0 ? points : null,
                GeometryBinary = geometry != null && geometry.Length > 0 ? geometry : null,
                HeightFieldBinary = heightfield != null && heightfield.Length > 0 ? heightfield : null,
                PointCount = pointCount,
                PointAttrFlags = pointAttrFlags,
                VertexCount = vertexCount,
                IndexCount = indexCount,
                CookNodesExecuted = nodesExecuted,
                CookNodesSkipped = nodesSkipped,
                Perf = perf,
            });
        }

        private static ByteArrayContent ToBinaryContent(float[] values)
        {
            var bytes = new byte[values.Length * sizeof(float)];
            Buffer.BlockCopy(values, 0, bytes, 0, bytes.Length);
            return new ByteArrayContent(bytes);
        }

        private static ByteArrayContent ToBinaryContent(int[] values)
        {
            var bytes = new byte[values.Length * sizeof(int)];
            Buffer.BlockCopy(values, 0, bytes, 0, bytes.Length);
            return new ByteArrayContent(bytes);
        }

        private static string JsonString(string value)
        {
            if (value == null)
                return "\"\"";
            var escaped = value.Replace("\\", "\\\\").Replace("\"", "\\\"");
            return $"\"{escaped}\"";
        }

        private static string FormatDouble(double value) =>
            value.ToString("G17", CultureInfo.InvariantCulture);

        private static string ExtractJsonString(string json, string key)
        {
            if (string.IsNullOrEmpty(json))
                return null;
            var token = $"\"{key}\"";
            var idx = json.IndexOf(token, StringComparison.Ordinal);
            if (idx < 0)
                return null;
            idx = json.IndexOf(':', idx + token.Length);
            if (idx < 0)
                return null;
            idx = json.IndexOf('"', idx + 1);
            if (idx < 0)
                return null;
            var end = json.IndexOf('"', idx + 1);
            if (end < 0)
                return null;
            return json.Substring(idx + 1, end - idx - 1);
        }

        private static string ExtractJsonNumber(string json, string key)
        {
            if (string.IsNullOrEmpty(json))
                return null;
            var token = $"\"{key}\"";
            var idx = json.IndexOf(token, StringComparison.Ordinal);
            if (idx < 0)
                return null;
            idx = json.IndexOf(':', idx + token.Length);
            if (idx < 0)
                return null;
            idx++;
            while (idx < json.Length && char.IsWhiteSpace(json[idx]))
                idx++;
            var end = idx;
            while (end < json.Length && (char.IsDigit(json[end]) || json[end] == '-'))
                end++;
            return end > idx ? json.Substring(idx, end - idx) : null;
        }

        private static uint ReadU32(byte[] data, ref int offset)
        {
            var v = BitConverter.ToUInt32(data, offset);
            offset += 4;
            return v;
        }

        private static int ReadI32(byte[] data, ref int offset) => (int)ReadU32(data, ref offset);

        private static double ReadF64(byte[] data, ref int offset)
        {
            var v = BitConverter.ToDouble(data, offset);
            offset += 8;
            return v;
        }

        private static byte[] ReadBlobBytes(byte[] data, ref int offset)
        {
            if (offset + 4 > data.Length)
                throw new InvalidDataException("Truncated blob length");
            var len = (int)ReadU32(data, ref offset);
            if (len < 0 || offset + len > data.Length)
                throw new InvalidDataException("Truncated blob payload");
            if (len == 0)
                return Array.Empty<byte>();
            var slice = new byte[len];
            Buffer.BlockCopy(data, offset, slice, 0, len);
            offset += len;
            return slice;
        }

        private static string ReadBlobUtf8(byte[] data, ref int offset)
        {
            var bytes = ReadBlobBytes(data, ref offset);
            return bytes.Length == 0 ? string.Empty : Encoding.UTF8.GetString(bytes);
        }
    }
}
