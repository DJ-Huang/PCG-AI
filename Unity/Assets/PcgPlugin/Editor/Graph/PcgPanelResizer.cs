using System;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Edge drag handle that resizes a docked panel. Rendered as an absolutely
    /// positioned overlay strip inside the panel edge, so it does not affect
    /// layout and hides together with the panel. Double-click resets the panel
    /// to its default size.
    /// </summary>
    internal sealed class PcgPanelResizer : VisualElement
    {
        public enum Edge
        {
            Left,
            Right,
            Bottom,
        }

        private const float Thickness = 6f;
        private static readonly Color IdleColor = new Color(1f, 1f, 1f, 0.05f);
        private static readonly Color ActiveColor = new Color(0.35f, 0.6f, 0.95f, 0.55f);

        private readonly VisualElement m_Target;
        private readonly Edge m_Edge;
        private readonly float m_MinSize;
        private readonly Func<float> m_MaxSize;
        private readonly Action m_OnBeforeApply;
        private readonly Action m_OnReset;

        private bool m_Dragging;
        private bool m_Hovered;
        private Vector2 m_StartMouse;
        private float m_StartSize;

        public PcgPanelResizer(
            VisualElement target,
            Edge edge,
            float minSize,
            Func<float> maxSize,
            Action onBeforeApply,
            Action onReset)
        {
            m_Target = target;
            m_Edge = edge;
            m_MinSize = minSize;
            m_MaxSize = maxSize;
            m_OnBeforeApply = onBeforeApply;
            m_OnReset = onReset;

            style.position = Position.Absolute;
            style.backgroundColor = IdleColor;
            tooltip = "Drag to resize; double-click to reset size";

            switch (edge)
            {
                case Edge.Left:
                    style.left = 0;
                    style.top = 0;
                    style.bottom = 0;
                    style.width = Thickness;
                    break;
                case Edge.Right:
                    style.right = 0;
                    style.top = 0;
                    style.bottom = 0;
                    style.width = Thickness;
                    break;
                case Edge.Bottom:
                    style.left = 0;
                    style.right = 0;
                    style.bottom = 0;
                    style.height = Thickness;
                    break;
            }

            RegisterCallback<MouseDownEvent>(OnMouseDown);
            RegisterCallback<MouseMoveEvent>(OnMouseMove);
            RegisterCallback<MouseUpEvent>(OnMouseUp);
            RegisterCallback<MouseEnterEvent>(_ =>
            {
                m_Hovered = true;
                RefreshHighlight();
            });
            RegisterCallback<MouseLeaveEvent>(_ =>
            {
                m_Hovered = false;
                RefreshHighlight();
            });
            RegisterCallback<MouseCaptureOutEvent>(_ =>
            {
                m_Dragging = false;
                RefreshHighlight();
            });
        }

        /// <summary>Resolved width of the panel's parent row, or fallback before layout.</summary>
        internal static float GetRowWidth(VisualElement panel, float fallback)
        {
            var row = panel?.parent;
            var w = row != null ? row.resolvedStyle.width : float.NaN;
            return float.IsNaN(w) || w <= 0f ? fallback : w;
        }

        /// <summary>Resolved height of the panel's parent row, or fallback before layout.</summary>
        internal static float GetRowHeight(VisualElement panel, float fallback)
        {
            var row = panel?.parent;
            var h = row != null ? row.resolvedStyle.height : float.NaN;
            return float.IsNaN(h) || h <= 0f ? fallback : h;
        }

        private void OnMouseDown(MouseDownEvent evt)
        {
            if (evt.button != 0)
                return;

            if (evt.clickCount == 2)
            {
                m_OnReset?.Invoke();
                evt.StopPropagation();
                return;
            }

            m_Dragging = true;
            m_StartMouse = ToStableSpace(evt.localMousePosition);
            m_StartSize = m_Edge == Edge.Bottom
                ? m_Target.resolvedStyle.height
                : m_Target.resolvedStyle.width;
            this.CaptureMouse();
            RefreshHighlight();
            evt.StopPropagation();
        }

        private void OnMouseMove(MouseMoveEvent evt)
        {
            if (!m_Dragging)
                return;

            var current = ToStableSpace(evt.localMousePosition);
            var delta = m_Edge == Edge.Bottom
                ? current.y - m_StartMouse.y
                : current.x - m_StartMouse.x;
            if (m_Edge == Edge.Left)
                delta = -delta;

            var max = m_MaxSize != null ? m_MaxSize() : 2000f;
            if (float.IsNaN(max) || max < m_MinSize)
                max = m_MinSize;
            var size = Mathf.Clamp(m_StartSize + delta, m_MinSize, max);

            m_OnBeforeApply?.Invoke();
            if (m_Edge == Edge.Bottom)
                m_Target.style.height = size;
            else
                m_Target.style.width = size;

            evt.StopPropagation();
        }

        private void OnMouseUp(MouseUpEvent evt)
        {
            if (!m_Dragging || evt.button != 0)
                return;

            m_Dragging = false;
            this.ReleaseMouse();
            RefreshHighlight();
            evt.StopPropagation();
        }

        private Vector2 ToStableSpace(Vector2 localPosition)
        {
            // Convert into the parent row's space: the row does not move while the
            // panel resizes, so deltas stay free of feedback from our own edits.
            var space = m_Target.parent;
            return space != null
                ? this.ChangeCoordinatesTo(space, localPosition)
                : localPosition;
        }

        private void RefreshHighlight()
        {
            style.backgroundColor = m_Dragging || m_Hovered ? ActiveColor : IdleColor;
        }
    }
}
