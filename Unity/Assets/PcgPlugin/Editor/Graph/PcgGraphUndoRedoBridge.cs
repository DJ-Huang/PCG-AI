#if UNITY_EDITOR
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Ensures graph undo/redo restores while Scene View or other windows have focus.
    /// </summary>
    [InitializeOnLoad]
    internal static class PcgGraphUndoRedoBridge
    {
        static PcgGraphUndoRedoBridge()
        {
            Undo.undoRedoPerformed += OnUndoRedoPerformed;
        }

        private static void OnUndoRedoPerformed()
        {
            foreach (var window in Resources.FindObjectsOfTypeAll<PcgGraphEditorWindow>())
            {
                if (window == null || !window.HasLoadedGraph || window.GraphView?.State == null)
                    continue;

                if (!window.GraphView.State.WasUndoRedoPerformed)
                    continue;

                window.GraphView.RestoreFromUndoState();
            }
        }
    }
}
#endif
