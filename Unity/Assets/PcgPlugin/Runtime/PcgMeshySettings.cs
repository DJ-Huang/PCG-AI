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

    /// <summary>
    /// Machine-local Tripo API credentials. Stored in EditorPrefs (not project assets)
    /// so keys are never committed to git.
    /// </summary>
    public static class PcgTripoSettings
    {
        public const string PrefKeyApiKey = "PCG.Tripo.ApiKey";
        public const string PrefKeyApiRegion = "PCG.Tripo.ApiRegion";
        public const string ApiRegionGlobal = "global";
        public const string ApiRegionChina = "china";
        public const string BaseUrlGlobal = "https://openapi.tripo3d.ai/v3";
        public const string BaseUrlChina = "https://openapi.tripo3d.com/v3";

        private static string s_CachedApiKey = "";
        private static string s_CachedApiRegion = "";

#if UNITY_EDITOR
        [UnityEditor.InitializeOnLoadMethod]
        private static void EditorLoadApiKey()
        {
            s_CachedApiKey = UnityEditor.EditorPrefs.GetString(PrefKeyApiKey, "") ?? "";
            s_CachedApiRegion = UnityEditor.EditorPrefs.GetString(PrefKeyApiRegion, ApiRegionGlobal) ?? ApiRegionGlobal;
        }
#endif

        /// <summary>global → openapi.tripo3d.ai; china → openapi.tripo3d.com</summary>
        public static string ApiRegion
        {
            get
            {
#if UNITY_EDITOR
                if (string.IsNullOrEmpty(s_CachedApiRegion))
                    s_CachedApiRegion = UnityEditor.EditorPrefs.GetString(PrefKeyApiRegion, ApiRegionGlobal) ?? ApiRegionGlobal;
                return s_CachedApiRegion;
#else
                return ApiRegionGlobal;
#endif
            }
            set
            {
#if UNITY_EDITOR
                var normalized = string.Equals(value, ApiRegionChina, System.StringComparison.OrdinalIgnoreCase)
                    ? ApiRegionChina
                    : ApiRegionGlobal;
                s_CachedApiRegion = normalized;
                UnityEditor.EditorPrefs.SetString(PrefKeyApiRegion, normalized);
#endif
            }
        }

        public static string BaseUrl =>
            ApiRegion == ApiRegionChina ? BaseUrlChina : BaseUrlGlobal;

        public static string AlternateBaseUrl =>
            ApiRegion == ApiRegionChina ? BaseUrlGlobal : BaseUrlChina;

        public static string ApiKey
        {
            get
            {
#if UNITY_EDITOR
                if (string.IsNullOrEmpty(s_CachedApiKey))
                    s_CachedApiKey = UnityEditor.EditorPrefs.GetString(PrefKeyApiKey, "") ?? "";
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
