namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Machine-local Meshy API credentials. Stored in EditorPrefs (not project assets)
    /// so keys are never committed to git.
    /// </summary>
    public static class PcgMeshySettings
    {
        public const string PrefKeyApiKey = "PCG.Meshy.ApiKey";

        private static string s_CachedApiKey = "";

#if UNITY_EDITOR
        [UnityEditor.InitializeOnLoadMethod]
        private static void EditorLoadApiKey()
        {
            s_CachedApiKey = UnityEditor.EditorPrefs.GetString(PrefKeyApiKey, "") ?? "";
        }
#endif

        /// <summary>Meshy API key (msy_…). Empty when unset.</summary>
        public static string ApiKey
        {
            get
            {
#if UNITY_EDITOR
                return s_CachedApiKey ?? "";
#else
                return "";
#endif
            }
            set
            {
#if UNITY_EDITOR
                var normalized = value?.Trim() ?? "";
                s_CachedApiKey = normalized;
                UnityEditor.EditorPrefs.SetString(PrefKeyApiKey, normalized);
#endif
            }
        }

        public static bool HasApiKey => !string.IsNullOrWhiteSpace(ApiKey);

        public static void ClearApiKey()
        {
            ApiKey = "";
        }
    }
}
