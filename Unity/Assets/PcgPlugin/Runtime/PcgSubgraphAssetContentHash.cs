using System;
using System.Security.Cryptography;
using System.Text;

namespace DJTechRuntime.PCG
{
    public static class PcgSubgraphAssetContentHash
    {
        public static string Compute(PcgSubgraphAssetDocument document)
        {
            if (document == null)
                return "";

            var clone = document.Clone();
            clone.contentHash = "";
            var json = PcgSubgraphAssetSerializer.ToCanonicalJson(clone, pretty: false);
            using var sha = SHA256.Create();
            var bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(json));
            return BitConverter.ToString(bytes).Replace("-", "").ToLowerInvariant();
        }

        public static string ComputeFromJson(string json)
        {
            if (string.IsNullOrWhiteSpace(json))
                return "";
            if (!PcgSubgraphAssetSerializer.TryFromJson(json, out var document, out _))
                return "";
            return Compute(document);
        }
    }
}
