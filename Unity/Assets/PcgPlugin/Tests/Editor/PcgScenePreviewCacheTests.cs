using DJTechEditor.PCG.Rendering;
using DJTechRuntime.PCG;
using NUnit.Framework;
using UnityEngine;

namespace DJTechEditor.PCG.Tests
{
    public class PcgScenePreviewCacheTests
    {
        private GameObject m_Owner;
        private PcgPreview m_Preview;

        [SetUp]
        public void SetUp()
        {
            PcgScenePreviewRenderer.ResetForTests();
            m_Owner = new GameObject("PcgScenePreviewCacheTests");
            m_Preview = m_Owner.AddComponent<PcgPreview>();
        }

        [TearDown]
        public void TearDown()
        {
            PcgScenePreviewRenderer.ResetForTests();
            if (m_Owner != null)
                Object.DestroyImmediate(m_Owner);
        }

        [Test]
        public void PreviewRevision_ChangesOnlyRebuildOnGeometryChange()
        {
            m_Preview.SetPoints(new[] { Vector3.zero, Vector3.right });
            var firstRevision = m_Preview.Revision;

            PcgScenePreviewRenderer.PreparePreviewForTests(m_Preview);
            var first = PcgScenePreviewRenderer.GetCounters();
            PcgScenePreviewRenderer.PreparePreviewForTests(m_Preview);
            var steady = PcgScenePreviewRenderer.GetCounters();

            Assert.That(firstRevision, Is.EqualTo(1));
            Assert.That(first.RebuildCount, Is.EqualTo(1));
            Assert.That(first.UploadCount, Is.EqualTo(1));
            Assert.That(first.CacheEntryCount, Is.EqualTo(1));
            Assert.That(steady.RebuildCount, Is.EqualTo(first.RebuildCount));
            Assert.That(steady.UploadCount, Is.EqualTo(first.UploadCount));

            m_Preview.SetPoints(new[] { Vector3.zero, Vector3.up, Vector3.one });
            Assert.That(m_Preview.Revision, Is.EqualTo(2));
            PcgScenePreviewRenderer.PreparePreviewForTests(m_Preview);
            var changed = PcgScenePreviewRenderer.GetCounters();

            Assert.That(changed.RebuildCount, Is.EqualTo(2));
            Assert.That(changed.UploadCount, Is.EqualTo(2));
            Assert.That(changed.CacheEntryCount, Is.EqualTo(1));
        }

        [Test]
        public void ReleaseAll_IsIdempotentAndClearsCache()
        {
            m_Preview.SetPoints(new[] { Vector3.zero });
            PcgScenePreviewRenderer.PreparePreviewForTests(m_Preview);

            Assert.That(PcgScenePreviewRenderer.GetCounters().CacheEntryCount, Is.EqualTo(1));
            Assert.DoesNotThrow(PcgScenePreviewRenderer.ReleaseAll);
            Assert.DoesNotThrow(PcgScenePreviewRenderer.ReleaseAll);
            Assert.That(PcgScenePreviewRenderer.GetCounters().CacheEntryCount, Is.EqualTo(0));
        }
    }
}
