using System;
using System.Linq;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public abstract class PcgGraphNodeBase : Node
    {
        public string NodeId { get; private set; }
        public abstract string NodeType { get; }

        protected Port InputPort { get; set; }
        protected Port OutputPort { get; set; }
        private Label m_RightTitleLabel;
        private TextField m_TitleEditor;
        private VisualElement m_RadialMenu;
        private Button m_PreviewBtn;
        private string m_UserTitle = "";
        private bool m_RadialMenuOpen;
        private bool m_OverlayAttached;
        private VisualElement m_InputRowPlaceholder;
        private VisualElement m_OutputRowPlaceholder;

        internal const float NodeWidth = 92f;
        internal const float NodeHeight = 46f;
        internal const float RadialMenuSize = 120f;
        internal const float RadialMenuRadius = RadialMenuSize * 0.5f;
        internal const float RadialMenuOffsetX = -3f;
        internal const float RadialMenuOffsetY = -5f;

        protected PcgGraphNodeBase()
        {
            style.minWidth = NodeWidth;
            style.width = NodeWidth;
            style.maxWidth = NodeWidth;
            style.minHeight = NodeHeight;
            ConfigureHoudiniPortLayout();
            BuildRightTitleUI();
            BuildHoverRadialMenu();
            RegisterCallback<GeometryChangedEvent>(_ => UpdateOverlayPlacement());
            RegisterCallback<MouseEnterEvent>(_ => ShowRadialMenu());
        }

        public void Initialize(string id, Vector2 position)
        {
            NodeId = id;
            expanded = true;
            SetPosition(new Rect(position, Vector2.zero));
            BuildPorts();
            EnsureFixedPortRows();
            RefreshExpandedState();
            RefreshPorts();
            EnsureFixedPortRows();
            UpdateDisplayedTitle();
        }

        public Port GetInputPort() => InputPort;
        public Port GetOutputPort() => OutputPort;

        public virtual Port FindInputPort(string handle = "in") => InputPort;
        public virtual Port FindOutputPort(string handle = "out") => OutputPort;

        protected virtual void BuildPorts() { }

        protected Port CreatePort(Direction direction, string portName)
        {
            var capacity = direction == Direction.Input ? Port.Capacity.Single : Port.Capacity.Multi;
            var port = PcgPort.Create(Orientation.Vertical, direction, capacity, typeof(float));
            port.portName = "";
            port.tooltip = portName;
            StyleHoudiniPin(port, direction);
            return port;
        }

        public string GetUserTitle() => m_UserTitle;

        public string GetDisplayTitle() => m_RightTitleLabel?.text ?? title;

        public void SetUserTitle(string userTitle)
        {
            m_UserTitle = userTitle?.Trim() ?? "";
            UpdateDisplayedTitle();
        }

        protected virtual string GetDefaultTitle() => title;

        private void ConfigureHoudiniPortLayout()
        {
            style.paddingLeft = 0;
            style.paddingRight = 0;
            style.paddingTop = 0;
            style.paddingBottom = 0;

            topContainer.style.flexDirection = FlexDirection.Column;
            topContainer.style.alignItems = Align.Stretch;
            topContainer.style.width = Length.Percent(100);
            topContainer.style.paddingLeft = 0;
            topContainer.style.paddingRight = 0;
            topContainer.style.marginLeft = 0;
            topContainer.style.marginRight = 0;

            titleContainer.style.alignSelf = Align.Center;

            inputContainer.style.flexDirection = FlexDirection.Row;
            inputContainer.style.justifyContent = Justify.Center;
            inputContainer.style.alignSelf = Align.Stretch;
            inputContainer.style.flexWrap = Wrap.Wrap;
            inputContainer.style.minHeight = 10;
            inputContainer.style.height = 10;
            inputContainer.style.marginTop = 0;
            inputContainer.style.marginBottom = 0;

            outputContainer.style.flexDirection = FlexDirection.Row;
            outputContainer.style.justifyContent = Justify.Center;
            outputContainer.style.alignSelf = Align.Stretch;
            outputContainer.style.flexWrap = Wrap.Wrap;
            outputContainer.style.minHeight = 10;
            outputContainer.style.height = 10;
            outputContainer.style.marginTop = 0;
            outputContainer.style.marginBottom = 0;

            topContainer.Remove(inputContainer);
            topContainer.Remove(outputContainer);
            topContainer.Insert(0, inputContainer);
            topContainer.Add(outputContainer);

            titleContainer.style.display = DisplayStyle.None;
            mainContainer.style.overflow = Overflow.Visible;
        }

        private void EnsureFixedPortRows()
        {
            inputContainer.style.display = DisplayStyle.Flex;
            outputContainer.style.display = DisplayStyle.Flex;

            if (m_InputRowPlaceholder == null)
            {
                m_InputRowPlaceholder = new VisualElement();
                m_InputRowPlaceholder.style.width = 1;
                m_InputRowPlaceholder.style.height = 1;
                m_InputRowPlaceholder.style.opacity = 0;
            }

            if (m_OutputRowPlaceholder == null)
            {
                m_OutputRowPlaceholder = new VisualElement();
                m_OutputRowPlaceholder.style.width = 1;
                m_OutputRowPlaceholder.style.height = 1;
                m_OutputRowPlaceholder.style.opacity = 0;
            }

            if (inputContainer.childCount == 0)
                inputContainer.Add(m_InputRowPlaceholder);
            else if (m_InputRowPlaceholder.parent == inputContainer && inputContainer.childCount > 1)
                m_InputRowPlaceholder.RemoveFromHierarchy();

            if (outputContainer.childCount == 0)
                outputContainer.Add(m_OutputRowPlaceholder);
            else if (m_OutputRowPlaceholder.parent == outputContainer && outputContainer.childCount > 1)
                m_OutputRowPlaceholder.RemoveFromHierarchy();
        }

        protected static void StyleHoudiniPin(Port port, Direction direction)
        {
            port.style.minWidth = 8;
            port.style.width = 8;
            port.style.height = 8;
            port.style.maxWidth = 8;
            port.style.maxHeight = 8;
            port.style.marginLeft = 3;
            port.style.marginRight = 3;
            port.style.marginTop = 0;
            port.style.marginBottom = 0;
            port.style.paddingLeft = 0;
            port.style.paddingRight = 0;
            port.style.paddingTop = 0;
            port.style.paddingBottom = 0;

            if (direction == Direction.Input)
            {
                port.style.alignSelf = Align.FlexStart;
                port.style.translate = new Translate(0, -5, 0);
            }
            else
            {
                port.style.alignSelf = Align.FlexEnd;
                port.style.translate = new Translate(0, 5, 0);
            }
        }

        private void BuildRightTitleUI()
        {
            m_RightTitleLabel = new Label
            {
                style =
                {
                    position = Position.Absolute,
                    right = -132,
                    top = 10,
                    minWidth = 120,
                    maxWidth = 220,
                    unityTextAlign = TextAnchor.MiddleLeft,
                    color = new Color(0.9f, 0.9f, 0.9f),
                    unityFontStyleAndWeight = FontStyle.Bold,
                    whiteSpace = WhiteSpace.Normal,
                    overflow = Overflow.Visible,
                },
            };
            m_RightTitleLabel.pickingMode = PickingMode.Position;
            m_RightTitleLabel.RegisterCallback<MouseDownEvent>(evt =>
            {
                if (evt.clickCount >= 2)
                {
                    BeginRename();
                    evt.StopPropagation();
                }
            });
            m_RightTitleLabel.pickingMode = PickingMode.Position;
        }

        private void BeginRename()
        {
            if (m_TitleEditor != null)
                return;

            m_TitleEditor = new TextField { value = GetDisplayTitle() };
            m_TitleEditor.style.position = Position.Absolute;
            m_TitleEditor.style.right = -132;
            m_TitleEditor.style.top = 8;
            m_TitleEditor.style.width = 180;
            m_TitleEditor.style.height = 20;
            m_TitleEditor.RegisterCallback<BlurEvent>(_ => CommitRename());
            m_TitleEditor.RegisterCallback<KeyDownEvent>(evt =>
            {
                if (evt.keyCode == KeyCode.Return || evt.keyCode == KeyCode.KeypadEnter)
                {
                    CommitRename();
                    evt.StopPropagation();
                }
                else if (evt.keyCode == KeyCode.Escape)
                {
                    EndRename();
                    evt.StopPropagation();
                }
            });
            EnsureOverlayAttached();
            GetOverlayParent()?.Add(m_TitleEditor);
            UpdateOverlayPlacement();
            m_TitleEditor.Focus();
            m_TitleEditor.SelectAll();
        }

        private void CommitRename()
        {
            if (m_TitleEditor == null)
                return;

            var graphView = GetFirstAncestorOfType<PcgGraphView>();
            var newTitle = m_TitleEditor.value?.Trim() ?? "";
            if (graphView != null)
            {
                graphView.WithUndo("Rename Node", () => SetUserTitle(newTitle));
                graphView.NotifyDocumentChanged();
                graphView.Inspector?.OnSelectionChanged();
            }
            else
            {
                SetUserTitle(newTitle);
            }

            EndRename();
        }

        private void EndRename()
        {
            if (m_TitleEditor == null)
                return;

            m_TitleEditor.RemoveFromHierarchy();
            m_TitleEditor = null;
        }

        private void UpdateDisplayedTitle()
        {
            var finalTitle = string.IsNullOrEmpty(m_UserTitle) ? GetDefaultTitle() : m_UserTitle;
            if (string.IsNullOrEmpty(finalTitle))
                finalTitle = NodeType;
            if (m_RightTitleLabel != null)
                m_RightTitleLabel.text = finalTitle;
        }

        private void BuildHoverRadialMenu()
        {
            m_RadialMenu = new VisualElement
            {
                style =
                {
                    position = Position.Absolute,
                    left = -16,
                    top = -42,
                    width = RadialMenuSize,
                    height = RadialMenuSize,
                    borderTopLeftRadius = RadialMenuRadius,
                    borderTopRightRadius = RadialMenuRadius,
                    borderBottomLeftRadius = RadialMenuRadius,
                    borderBottomRightRadius = RadialMenuRadius,
                    borderTopWidth = 1,
                    borderRightWidth = 1,
                    borderBottomWidth = 1,
                    borderLeftWidth = 1,
                    borderTopColor = new Color(0.55f, 0.55f, 0.55f, 0.5f),
                    borderRightColor = new Color(0.55f, 0.55f, 0.55f, 0.5f),
                    borderBottomColor = new Color(0.55f, 0.55f, 0.55f, 0.5f),
                    borderLeftColor = new Color(0.55f, 0.55f, 0.55f, 0.5f),
                    backgroundColor = new Color(0f, 0f, 0f, 0f),
                    display = DisplayStyle.None,
                },
            };
            // Important: the ring itself must not block node dragging.
            m_RadialMenu.pickingMode = PickingMode.Ignore;

            var infoBtn = CreateCircleButton("i", () =>
            {
                var graphView = GetFirstAncestorOfType<PcgGraphView>();
                if (graphView == null)
                    return;
                graphView.ClearSelection();
                graphView.AddToSelection(this);
                if (graphView.Inspector.style.display.value != DisplayStyle.Flex)
                    graphView.Inspector.ToggleVisible();
                graphView.Inspector.OnSelectionChanged();
            });
            infoBtn.tooltip = "Node info";
            m_RadialMenu.Add(infoBtn);

            var paramsBtn = CreateCircleButton("P", () =>
            {
                var graphView = GetFirstAncestorOfType<PcgGraphView>();
                if (graphView?.Blackboard == null)
                    return;
                if (graphView.Blackboard.style.display.value != DisplayStyle.Flex)
                    graphView.Blackboard.ToggleVisible();
            });
            paramsBtn.tooltip = "Parameters";
            m_RadialMenu.Add(paramsBtn);

            m_PreviewBtn = CreateCircleButton("◎", () =>
            {
                var graphView = GetFirstAncestorOfType<PcgGraphView>();
                graphView?.ToggleNodePreview(this);
            });
            m_PreviewBtn.tooltip = "Preview in Scene (toggle)";
            m_RadialMenu.Add(m_PreviewBtn);

            EnsureOverlayAttached();
        }

        internal bool IsRadialMenuOpen => m_RadialMenuOpen;

        internal bool ContainsGraphPointer(Vector2 graphPointerPos)
        {
            return Vector2.Distance(GetRadialMenuCenter(), graphPointerPos) <= RadialMenuRadius;
        }

        internal void ShowRadialMenu()
        {
            if (m_RadialMenuOpen)
                return;

            m_RadialMenuOpen = true;
            SetRadialMenuVisible(true);

            var graphView = GetFirstAncestorOfType<PcgGraphView>();
            graphView?.NotifyRadialMenuOpened(this);
        }

        internal void DismissRadialMenu()
        {
            if (!m_RadialMenuOpen)
                return;

            m_RadialMenuOpen = false;
            SetRadialMenuVisible(false);

            var graphView = GetFirstAncestorOfType<PcgGraphView>();
            graphView?.NotifyRadialMenuClosed(this);
        }

        private Vector2 GetRadialMenuCenter()
        {
            var rect = GetPosition();
            return new Vector2(
                rect.x + rect.width * 0.5f + RadialMenuOffsetX,
                rect.y + rect.height * 0.5f + RadialMenuOffsetY);
        }

        private Vector2 GetRadialMenuTopLeft()
        {
            var center = GetRadialMenuCenter();
            return new Vector2(center.x - RadialMenuRadius, center.y - RadialMenuRadius);
        }

        public void SetNodePreviewState(bool isActive)
        {
            SetPreviewHighlighted(isActive);
            if (m_PreviewBtn == null)
                return;

            m_PreviewBtn.style.backgroundColor = isActive
                ? new Color(0.15f, 0.55f, 0.95f, 0.95f)
                : new Color(0.2f, 0.2f, 0.2f, 0.85f);
            m_PreviewBtn.style.color = isActive
                ? Color.white
                : new Color(0.85f, 0.9f, 0.95f);
        }

        private void SetPreviewHighlighted(bool highlighted)
        {
            const float normalBorder = 1f;
            const float previewBorder = 2f;
            var width = highlighted ? previewBorder : normalBorder;
            var color = highlighted
                ? new Color(0.25f, 0.75f, 1f, 1f)
                : new Color(0.35f, 0.35f, 0.35f, 1f);

            style.borderTopWidth = width;
            style.borderRightWidth = width;
            style.borderBottomWidth = width;
            style.borderLeftWidth = width;
            style.borderTopColor = color;
            style.borderRightColor = color;
            style.borderBottomColor = color;
            style.borderLeftColor = color;
        }

        private static Button CreateCircleButton(string text, Action onClick)
        {
            var button = new Button(onClick)
            {
                text = text,
                style =
                {
                    position = Position.Absolute,
                    width = 30,
                    height = 30,
                    borderTopLeftRadius = 15,
                    borderTopRightRadius = 15,
                    borderBottomLeftRadius = 15,
                    borderBottomRightRadius = 15,
                    unityTextAlign = TextAnchor.MiddleCenter,
                    fontSize = 16,
                    paddingLeft = 0,
                    paddingRight = 0,
                    paddingTop = 0,
                    paddingBottom = 0,
                },
            };
            return button;
        }

        private void SetRadialMenuVisible(bool isVisible)
        {
            EnsureOverlayAttached();
            if (m_RadialMenu != null)
                m_RadialMenu.style.display = isVisible ? DisplayStyle.Flex : DisplayStyle.None;
        }

        private VisualElement GetOverlayParent()
        {
            var graphView = GetFirstAncestorOfType<PcgGraphView>();
            return graphView?.contentViewContainer;
        }

        private void EnsureOverlayAttached()
        {
            if (m_OverlayAttached)
                return;

            var overlayParent = GetOverlayParent();
            if (overlayParent == null)
                return;

            if (m_RightTitleLabel != null && m_RightTitleLabel.parent != overlayParent)
                overlayParent.Add(m_RightTitleLabel);
            if (m_RadialMenu != null && m_RadialMenu.parent != overlayParent)
                overlayParent.Add(m_RadialMenu);
            m_OverlayAttached = true;
            UpdateOverlayPlacement();
        }

        private void UpdateOverlayPlacement()
        {
            EnsureOverlayAttached();
            var rect = GetPosition();

            if (m_RightTitleLabel != null)
            {
                m_RightTitleLabel.style.left = rect.x + rect.width + 14;
                m_RightTitleLabel.style.top = rect.y + 10;
            }

            if (m_TitleEditor != null)
            {
                m_TitleEditor.style.left = rect.x + rect.width + 14;
                m_TitleEditor.style.top = rect.y + 8;
            }

            if (m_RadialMenu != null)
            {
                var topLeft = GetRadialMenuTopLeft();
                m_RadialMenu.style.left = topLeft.x;
                m_RadialMenu.style.top = topLeft.y;
                LayoutRadialButtons();
            }
        }

        private void LayoutRadialButtons()
        {
            if (m_RadialMenu == null)
                return;

            var buttons = m_RadialMenu.Query<Button>().ToList();
            if (buttons.Count == 0)
                return;

            const float diameter = 30f;
            const float buttonOrbitRadius = NodeWidth * 0.5f + 14f;
            const float center = RadialMenuRadius;
            const float startDeg = -156f;
            const float stepDeg = 24f;

            for (var i = 0; i < buttons.Count; i++)
            {
                var angle = (startDeg + stepDeg * i) * Mathf.Deg2Rad;
                var x = center + Mathf.Cos(angle) * buttonOrbitRadius - diameter * 0.5f;
                var y = center + Mathf.Sin(angle) * buttonOrbitRadius - diameter * 0.5f;
                buttons[i].style.left = x;
                buttons[i].style.top = y;
            }
        }

        protected static void AddField<T>(VisualElement parent, string label, T initialValue, Action<T> onChanged)
            where T : struct
        {
            if (typeof(T) == typeof(int))
            {
                var field = new IntegerField(label) { value = Convert.ToInt32(initialValue) };
                field.RegisterValueChangedCallback(evt => onChanged((T)(object)evt.newValue));
                parent.Add(field);
                return;
            }

            if (typeof(T) == typeof(float))
            {
                var field = new FloatField(label) { value = Convert.ToSingle(initialValue) };
                field.RegisterValueChangedCallback(evt => onChanged((T)(object)evt.newValue));
                parent.Add(field);
            }
        }

        protected static void AddTextField(VisualElement parent, string label, string initialValue, Action<string> onChanged)
        {
            var field = new TextField(label) { value = initialValue ?? "" };
            field.RegisterValueChangedCallback(evt => onChanged(evt.newValue));
            parent.Add(field);
        }

        public abstract void ApplyData(PcgNodeData data);
        public abstract PcgNodeData CollectData();

        /// <summary>Set a single property value on this node (used by Inspector).</summary>
        public virtual void SetPropertyValue(string key, object value)
        {
            // Default: no-op. Override in subclasses.
        }
    }
}
