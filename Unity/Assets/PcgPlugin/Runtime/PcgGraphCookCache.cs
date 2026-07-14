using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;

namespace DJTechRuntime.PCG
{
    /// <summary>Whole-graph cook result cache (MVP dirty/hash cache for 4.4e T3).</summary>
    public static class PcgGraphCookCache
    {
        private static readonly Dictionary<string, PcgGraphExecuteResult> s_Cache = new();
        private const int MaxEntries = 32;

        public static string BuildKey(string json, int seed)
        {
            if (string.IsNullOrEmpty(json))
                return string.Empty;

            using var sha = SHA256.Create();
            var bytes = Encoding.UTF8.GetBytes(json + "|" + seed);
            var hash = sha.ComputeHash(bytes);
            var sb = new StringBuilder(hash.Length * 2);
            foreach (var b in hash)
                sb.Append(b.ToString("x2"));
            return sb.ToString();
        }

        public static bool TryGet(string key, out PcgGraphExecuteResult result)
        {
            result = null;
            if (string.IsNullOrEmpty(key))
                return false;

            if (!s_Cache.TryGetValue(key, out var cached) || cached == null)
                return false;

            result = Clone(cached);
            return true;
        }

        public static void Store(string key, PcgGraphExecuteResult result)
        {
            if (string.IsNullOrEmpty(key) || result == null)
                return;

            if (s_Cache.Count >= MaxEntries)
                s_Cache.Clear();

            s_Cache[key] = Clone(result);
        }

        public static void Clear() => s_Cache.Clear();

        private static PcgGraphExecuteResult Clone(PcgGraphExecuteResult source)
        {
            return new PcgGraphExecuteResult
            {
                Kind = source.Kind,
                Json = source.Json,
                VertexCount = source.VertexCount,
                IndexCount = source.IndexCount,
                PointCount = source.PointCount,
                PointAttrFlags = source.PointAttrFlags,
                Error = source.Error,
                CookNodesExecuted = source.CookNodesExecuted,
                CookNodesSkipped = source.CookNodesSkipped,
                MeshBinary = source.MeshBinary != null ? (byte[])source.MeshBinary.Clone() : null,
                GeometryBinary = source.GeometryBinary != null ? (byte[])source.GeometryBinary.Clone() : null,
                PointBinary = source.PointBinary != null ? (byte[])source.PointBinary.Clone() : null,
                Perf = ClonePerf(source.Perf),
            };
        }

        private static PcgCookPerfReport ClonePerf(PcgCookPerfReport source)
        {
            if (source == null)
                return null;

            return new PcgCookPerfReport
            {
                AssetResolveMs = source.AssetResolveMs,
                ValidateMs = source.ValidateMs,
                NativeCallMs = source.NativeCallMs,
                GraphExecuteMs = source.GraphExecuteMs,
                BinaryWriteMs = source.BinaryWriteMs,
                BufferCopyMs = source.BufferCopyMs,
                CookNodesExecuted = source.CookNodesExecuted,
                CookNodesSkipped = source.CookNodesSkipped,
                NodeEntries = source.NodeEntries != null
                    ? (PcgNodePerfEntry[])source.NodeEntries.Clone()
                    : null,
            };
        }
    }
}
