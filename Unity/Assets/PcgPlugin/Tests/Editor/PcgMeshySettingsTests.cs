using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEditor;

namespace DJTechEditor.PCG.Tests
{
    public class PcgMeshySettingsTests
    {
        private string m_PreviousKey;

        [SetUp]
        public void SetUp()
        {
            m_PreviousKey = EditorPrefs.GetString(PcgMeshySettings.PrefKeyApiKey, "");
        }

        [TearDown]
        public void TearDown()
        {
            EditorPrefs.SetString(PcgMeshySettings.PrefKeyApiKey, m_PreviousKey ?? "");
            // Refresh static cache used by ApiKey getter.
            PcgMeshySettings.ApiKey = m_PreviousKey ?? "";
        }

        [Test]
        public void ApiKey_PersistsInEditorPrefs()
        {
            PcgMeshySettings.ClearApiKey();
            Assert.That(PcgMeshySettings.HasApiKey, Is.False);

            PcgMeshySettings.ApiKey = "  msy_test_key  ";
            Assert.That(PcgMeshySettings.ApiKey, Is.EqualTo("msy_test_key"));
            Assert.That(EditorPrefs.GetString(PcgMeshySettings.PrefKeyApiKey, ""), Is.EqualTo("msy_test_key"));
            Assert.That(PcgMeshySettings.HasApiKey, Is.True);

            PcgMeshySettings.ClearApiKey();
            Assert.That(PcgMeshySettings.ApiKey, Is.EqualTo(""));
            Assert.That(EditorPrefs.GetString(PcgMeshySettings.PrefKeyApiKey, ""), Is.EqualTo(""));
        }
    }

    public class PcgThirdPartyHttpSettingsTests
    {
        private bool m_PreviousUseProxy;
        private string m_PreviousProxyUrl;

        [SetUp]
        public void SetUp()
        {
            m_PreviousUseProxy = EditorPrefs.GetBool(PcgThirdPartyHttpSettings.PrefKeyUseHttpProxy, true);
            m_PreviousProxyUrl = EditorPrefs.GetString(
                PcgThirdPartyHttpSettings.PrefKeyProxyUrl,
                PcgThirdPartyHttpSettings.DefaultProxyUrl);
        }

        [TearDown]
        public void TearDown()
        {
            EditorPrefs.SetBool(PcgThirdPartyHttpSettings.PrefKeyUseHttpProxy, m_PreviousUseProxy);
            EditorPrefs.SetString(PcgThirdPartyHttpSettings.PrefKeyProxyUrl, m_PreviousProxyUrl ?? "");
            PcgThirdPartyHttpSettings.UseHttpProxy = m_PreviousUseProxy;
            PcgThirdPartyHttpSettings.ProxyUrl = m_PreviousProxyUrl ?? "";
        }

        [Test]
        public void ProxySettings_PersistInEditorPrefs()
        {
            PcgThirdPartyHttpSettings.UseHttpProxy = false;
            PcgThirdPartyHttpSettings.ProxyUrl = "http://127.0.0.1:7890";
            Assert.That(PcgThirdPartyHttpSettings.UseHttpProxy, Is.False);
            Assert.That(PcgThirdPartyHttpSettings.ProxyUrl, Is.EqualTo("http://127.0.0.1:7890"));
            Assert.That(
                EditorPrefs.GetBool(PcgThirdPartyHttpSettings.PrefKeyUseHttpProxy, true),
                Is.False);
            Assert.That(
                EditorPrefs.GetString(PcgThirdPartyHttpSettings.PrefKeyProxyUrl, ""),
                Is.EqualTo("http://127.0.0.1:7890"));

            var handler = PcgThirdPartyHttpSettings.CreateHttpClientHandler();
            Assert.That(handler.UseProxy, Is.False);

            PcgThirdPartyHttpSettings.UseHttpProxy = true;
            PcgThirdPartyHttpSettings.ProxyUrl = "http://127.0.0.1:7897";
            handler = PcgThirdPartyHttpSettings.CreateHttpClientHandler();
            Assert.That(handler.UseProxy, Is.True);
            Assert.That(handler.Proxy?.GetProxy(new System.Uri("https://api.meshy.ai")).ToString(),
                Is.EqualTo("http://127.0.0.1:7897/"));
        }
    }
}
