using System;
using System.Net.Http;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Machine-local HTTP proxy for third-party cloud APIs (Meshy, Tripo, …).
    /// Stored in EditorPrefs so users can route through Clash or connect directly.
    /// </summary>
    public static class PcgThirdPartyHttpSettings
    {
        public const string PrefKeyUseHttpProxy = "PCG.ThirdPartyApi.UseHttpProxy";
        public const string PrefKeyProxyUrl = "PCG.ThirdPartyApi.ProxyUrl";
        public const string DefaultProxyUrl = "http://127.0.0.1:7897";

        private static bool s_CachedUseHttpProxy = true;
        private static string s_CachedProxyUrl = DefaultProxyUrl;
        private static HttpClient s_CachedClient;
        private static string s_CachedClientSignature;

#if UNITY_EDITOR
        [UnityEditor.InitializeOnLoadMethod]
        private static void EditorLoad()
        {
            s_CachedUseHttpProxy = UnityEditor.EditorPrefs.GetBool(PrefKeyUseHttpProxy, true);
            s_CachedProxyUrl = UnityEditor.EditorPrefs.GetString(PrefKeyProxyUrl, DefaultProxyUrl) ?? DefaultProxyUrl;
        }
#endif

        /// <summary>When false, Meshy/Tripo HTTP clients connect directly (no proxy).</summary>
        public static bool UseHttpProxy
        {
            get
            {
#if UNITY_EDITOR
                return s_CachedUseHttpProxy;
#else
                return false;
#endif
            }
            set
            {
#if UNITY_EDITOR
                s_CachedUseHttpProxy = value;
                UnityEditor.EditorPrefs.SetBool(PrefKeyUseHttpProxy, value);
                InvalidateHttpClient();
#endif
            }
        }

        /// <summary>Proxy URL (e.g. http://127.0.0.1:7897 for Clash mixed port).</summary>
        public static string ProxyUrl
        {
            get
            {
#if UNITY_EDITOR
                return s_CachedProxyUrl ?? DefaultProxyUrl;
#else
                return "";
#endif
            }
            set
            {
#if UNITY_EDITOR
                s_CachedProxyUrl = value?.Trim() ?? "";
                UnityEditor.EditorPrefs.SetString(PrefKeyProxyUrl, s_CachedProxyUrl);
                InvalidateHttpClient();
#endif
            }
        }

        public static HttpClientHandler CreateHttpClientHandler()
        {
            var handler = new HttpClientHandler();
            if (UseHttpProxy && !string.IsNullOrWhiteSpace(ProxyUrl))
            {
                handler.UseProxy = true;
                handler.Proxy = new System.Net.WebProxy(ProxyUrl.Trim());
            }
            return handler;
        }

        /// <summary>Cached HttpClient; recreated when proxy settings or timeout change.</summary>
        public static HttpClient GetHttpClient(TimeSpan timeout)
        {
            var signature = BuildClientSignature(timeout);
            if (s_CachedClient == null || s_CachedClientSignature != signature)
            {
                InvalidateHttpClient();
                var handler = CreateHttpClientHandler();
                s_CachedClient = new HttpClient(handler, disposeHandler: true) { Timeout = timeout };
                s_CachedClientSignature = signature;
            }
            return s_CachedClient;
        }

        private static string BuildClientSignature(TimeSpan timeout) =>
            $"{UseHttpProxy}|{ProxyUrl?.Trim()}|{timeout.Ticks}";

        private static void InvalidateHttpClient()
        {
            if (s_CachedClient != null)
            {
                s_CachedClient.Dispose();
                s_CachedClient = null;
            }
            s_CachedClientSignature = null;
        }
    }
}
