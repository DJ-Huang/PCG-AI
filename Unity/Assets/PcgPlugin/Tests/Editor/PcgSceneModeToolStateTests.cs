using System.Reflection;
using DJTechEditor.PCG.Graph;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Tests
{
    public class PcgSceneModeToolStateTests
    {
        [Test]
        public void ObjectMode_RestoresNativeTransformTool()
        {
            var tool = PcgCreateSplineSceneHandles.GetEditorToolForSceneContext(
                PcgSceneEditContext.ObjectMode,
                Tool.Rotate);

            Assert.AreEqual(Tool.Rotate, tool);
        }

        [Test]
        public void ObjectMode_RepairsLeakedNoneTool()
        {
            var tool = PcgCreateSplineSceneHandles.GetEditorToolForSceneContext(
                PcgSceneEditContext.ObjectMode,
                Tool.None);

            Assert.AreEqual(Tool.Move, tool);
        }

        [Test]
        public void ComponentMode_HidesNativeTransformTool()
        {
            var context = new PcgSceneEditContext(
                SceneEditLevel.Component,
                SceneEditDomain.SplineControlPoint,
                "node",
                new[] { SceneEditDomain.SplineControlPoint });

            var tool = PcgCreateSplineSceneHandles.GetEditorToolForSceneContext(
                context,
                Tool.Move);

            Assert.AreEqual(Tool.None, tool);
        }

        [Test]
        public void ExitingPcgMode_ClearsStampDragStateWithoutCooking()
        {
            var type = typeof(PcgStampOverlaySceneHandles);
            var flags = BindingFlags.Static | BindingFlags.NonPublic;
            var dragging = type.GetField("s_Dragging", flags);
            var hadHotControl = type.GetField("s_DragHadHotControl", flags);
            var transformNodeId = type.GetField("s_DragTransformNodeId", flags);

            Assert.NotNull(dragging);
            Assert.NotNull(hadHotControl);
            Assert.NotNull(transformNodeId);

            dragging.SetValue(null, true);
            hadHotControl.SetValue(null, true);
            transformNodeId.SetValue(null, "stamp-transform");

            PcgStampOverlaySceneHandles.ForceClearInteractionState(requestCook: false);

            Assert.IsFalse((bool)dragging.GetValue(null));
            Assert.IsFalse((bool)hadHotControl.GetValue(null));
            Assert.IsNull(transformNodeId.GetValue(null));
        }

        [TestCase(false)]
        [TestCase(true)]
        public void PcgMode_SelectionChange_RestoresLockedObjectWithoutExiting(bool selectOtherObject)
        {
            var type = typeof(PcgCreateSplineSceneHandles);
            var flags = BindingFlags.Static | BindingFlags.NonPublic;
            var modeActive = type.GetField("s_PcgModeActive", flags);
            var lockedSelection = type.GetField("s_LockedSelection", flags);
            var selectionGuard = type.GetField("s_SelectionGuard", flags);
            var onSelectionChanged = type.GetMethod("OnSelectionChanged", flags);
            var originalSelection = Selection.objects;
            var originalModeActive = modeActive.GetValue(null);
            var originalLockedSelection = lockedSelection.GetValue(null);
            var originalSelectionGuard = selectionGuard.GetValue(null);
            var locked = new GameObject("PCG Mode Locked Selection Test");
            var other = new GameObject("PCG Mode Other Selection Test");

            try
            {
                modeActive.SetValue(null, true);
                lockedSelection.SetValue(null, locked);
                selectionGuard.SetValue(null, false);

                Selection.activeGameObject = selectOtherObject ? other : null;
                onSelectionChanged.Invoke(null, null);

                Assert.IsTrue((bool)modeActive.GetValue(null));
                Assert.AreEqual(locked, Selection.activeGameObject);
            }
            finally
            {
                modeActive.SetValue(null, originalModeActive);
                lockedSelection.SetValue(null, originalLockedSelection);
                selectionGuard.SetValue(null, originalSelectionGuard);
                Selection.objects = originalSelection;
                Object.DestroyImmediate(locked);
                Object.DestroyImmediate(other);
            }
        }
    }
}
