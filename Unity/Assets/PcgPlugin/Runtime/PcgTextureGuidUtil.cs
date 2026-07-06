using System;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Unity asset GUID helpers for ImageTexture nodes.
    /// </summary>
    public static class PcgTextureGuidUtil
    {
        /// <summary>Unity null / missing serialized reference pattern.</summary>
        public const string NullReferenceGuid = "0000000000000000f000000000000000";

        public static bool IsValidAssetGuid(string guid)
        {
            if (string.IsNullOrWhiteSpace(guid))
                return false;

            if (guid.Length != 32)
                return false;

            if (string.Equals(guid, NullReferenceGuid, StringComparison.OrdinalIgnoreCase))
                return false;

            if (guid.Trim('0').Length == 0)
                return false;

            foreach (var c in guid)
            {
                var isHex = (c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F');
                if (!isHex)
                    return false;
            }

            return true;
        }

        public static string NormalizeStoredGuid(string guid)
        {
            return IsValidAssetGuid(guid) ? guid : "";
        }
    }
}
