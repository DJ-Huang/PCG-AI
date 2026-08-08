using System;
using DJTechRuntime.PCG;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    public enum PcgNodeActionStatusTone
    {
        Muted,
        Success,
    }

    /// <summary>Idle (not running) presentation of an async node action section.</summary>
    public struct PcgNodeActionIdleView
    {
        public string ButtonLabel;
        public string ButtonTooltip;
        public string StatusText;
        public PcgNodeActionStatusTone StatusTone;
    }

    /// <summary>
    /// Describes a node-type-specific async action rendered as a bottom section in the
    /// node inspector (button + progress bar + status line, cancel while running).
    /// All Unity-API access must stay in <see cref="GetIdleView"/> / <see cref="Prepare"/>
    /// / <see cref="Apply"/> (main thread); the work delegate returned by Prepare must
    /// be thread-safe (pure HTTP / file IO).
    /// </summary>
    public sealed class PcgAsyncNodeActionDef
    {
        /// <summary>Stable id; controller state is keyed by (nodeId, ActionId).</summary>
        public string ActionId;

        /// <summary>Undo label for the success write-back.</summary>
        public string UndoLabel;

        /// <summary>Small hint line under the button (optional).</summary>
        public string Caption;

        /// <summary>
        /// Idle presentation; called on section rebuild and after completion (main thread).
        /// Errors from controller state are rendered by the section itself (red).
        /// </summary>
        public Func<PcgManifestNodeView, PcgNodeAsyncActionController.State, PcgNodeActionIdleView> GetIdleView;

        /// <summary>Main-thread prep; return Failure to abort with an error status line.</summary>
        public Func<string, PcgNodeData, PcgNodeAsyncActionController.PrepareResult> Prepare;

        /// <summary>
        /// Optional main-thread prep with editor context (window + node view). When set,
        /// the inspector prefers it over <see cref="Prepare"/> — needed by actions that
        /// must cook the node's upstream input before going background (Meshy mesh ops).
        /// </summary>
        public Func<PcgManifestNodeView, PcgGraphEditorWindow, PcgNodeAsyncActionController.PrepareResult> PrepareWithWindow;

        /// <summary>Main-thread success write-back; runs inside the inspector's undo scope.</summary>
        public Action<PcgManifestNodeView, string> Apply;

        /// <summary>
        /// Optional UI inserted above the action button (e.g. Meshy save-format toggles).
        /// <paramref name="notifyChanged"/> marks the graph dirty without forcing a rebuild.
        /// </summary>
        public Func<PcgManifestNodeView, Action, VisualElement> BuildExtraUi;
    }
}
