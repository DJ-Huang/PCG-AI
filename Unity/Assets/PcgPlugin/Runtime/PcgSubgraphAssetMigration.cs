using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    public static class PcgSubgraphAssetMigration
    {
        public const string Version10 = "1.0";
        public const string Version20 = "2.0";
        public const string CurrentVersion = Version20;

        public static bool TryMigrateRoot(Dictionary<string, object> root, out string error)
        {
            error = null;
            if (root == null)
            {
                error = "Root is null.";
                return false;
            }

            var version = root.TryGetValue("version", out var versionObj)
                ? versionObj?.ToString()
                : Version10;
            if (version == Version20)
                return true;
            if (version != Version10)
            {
                error = $"Unsupported subgraph asset version '{version}'.";
                return false;
            }

            root["version"] = Version20;
            if (!root.ContainsKey("parameters"))
                root["parameters"] = new List<object>();
            if (!root.ContainsKey("contentHash"))
                root["contentHash"] = "";
            return true;
        }
    }
}
