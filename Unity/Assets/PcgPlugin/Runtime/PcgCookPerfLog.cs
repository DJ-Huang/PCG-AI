using System;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>Formats and logs end-to-end cook performance metrics.</summary>
    public static class PcgCookPerfLog
    {
        public static void LogCacheHit(PcgCookPerfReport report, PcgPreviewQuality quality)
        {
            Write(report, quality, "[whole-graph cache hit, re-applied last cook]");
        }

        public static void Log(PcgCookPerfReport report, PcgPreviewQuality quality)
        {
            Write(report, quality, null);
        }

        private static void Write(PcgCookPerfReport report, PcgPreviewQuality quality, string suffix)
        {
            if (report == null)
            {
                Debug.Log($"[PCG] Cook perf: whole-graph cache hit ({quality}), no perf snapshot.");
                return;
            }

            var tag = string.IsNullOrEmpty(suffix)
                ? $"{Fmt(report.TotalMs)} total"
                : suffix;
            var sb = new StringBuilder(512);
            sb.AppendLine($"[PCG] Cook perf ({tag}, {quality}):");
            sb.AppendLine(
                $"  Managed: assets={Fmt(report.AssetResolveMs)} validate={Fmt(report.ValidateMs)} buffer_copy={Fmt(report.BufferCopyMs)}");
            sb.AppendLine(
                $"  Native: graph={Fmt(report.GraphExecuteMs)} binary_write={Fmt(report.BinaryWriteMs)} p/invoke={Fmt(report.NativeCallMs)}");
            sb.AppendLine(
                $"  Cache: executed={report.CookNodesExecuted} skipped={report.CookNodesSkipped}");

            if (report.NodeEntries != null && report.NodeEntries.Length > 0)
            {
                sb.AppendLine("  Nodes (slowest first):");
                var nodes = (PcgNodePerfEntry[])report.NodeEntries.Clone();
                Array.Sort(nodes, (a, b) => b.Ms.CompareTo(a.Ms));
                foreach (var node in nodes)
                {
                    var cached = node.Cached ? " [cached]" : string.Empty;
                    sb.AppendLine(
                        $"    {node.Type}/{node.Id}: {Fmt(node.Ms)}{cached}");
                }
            }

            Debug.Log(sb.ToString());
        }

        private static string Fmt(double ms) =>
            ms.ToString("0.###", CultureInfo.InvariantCulture) + "ms";
    }

    public sealed class PcgCookPerfReport
    {
        public double AssetResolveMs;
        public double ValidateMs;
        public double NativeCallMs;
        public double GraphExecuteMs;
        public double BinaryWriteMs;
        public double BufferCopyMs;
        public int CookNodesExecuted;
        public int CookNodesSkipped;
        public PcgNodePerfEntry[] NodeEntries;

        public double TotalMs =>
            AssetResolveMs + ValidateMs + NativeCallMs + BufferCopyMs;
    }

    public struct PcgNodePerfEntry
    {
        public string Id;
        public string Type;
        public double Ms;
        public bool Cached;
    }
}
