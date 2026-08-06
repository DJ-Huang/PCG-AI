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
}
