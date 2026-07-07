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
        private string m_UserTitle = "";
        private bool m_IsPointerOnNode;
        private bool m_IsPointerOnMenu;
        private bool m_OverlayAttached;
        private VisualElement m_InputRowPlaceholder;
        private VisualElement m_OutputRowPlaceholder;

        protected PcgGraphNodeBase()
        {
            style.minWidth = 100;
            style.width = 110;
            style.maxWidth = 110;
            style.minHeight = 56;
            ConfigureHoudiniPortLayout();
            BuildRightTitleUI();
            BuildHoverRadialMenu();
            RegisterCallback<GeometryChangedEvent>(_ => UpdateOverlayPlacement());
            RegisterCallback<MouseEnterEvent>(_ =>
            {
                m_IsPointerOnNode = true;
                UpdateRadialMenuVisibility();
            });
            RegisterCallback<MouseLeaveEvent>(_ =>
            {
                m_IsPointerOnNode = false;
                schedule.Execute(UpdateRadialMenuVisibility).ExecuteLater(50);
            });
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
            inputContainer.style.minHeight = 12;
            inputContainer.style.height = 12;
            inputContainer.style.marginTop = 0;
            inputContainer.style.marginBottom = 0;

            outputContainer.style.flexDirection = FlexDirection.Row;
            outputContainer.style.justifyContent = Justify.Center;
            outputContainer.style.alignSelf = Align.Stretch;
            outputContainer.style.flexWrap = Wrap.Wrap;
            outputContainer.style.minHeight = 12;
            outputContainer.style.height = 12;
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
                port.style.translate = new Translate(0, -6, 0);
            }
            else
            {
                port.style.alignSelf = Align.FlexEnd;
                port.style.translate = new Translate(0, 6, 0);
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
                    width = 172,
                    height = 172,
                    borderTopLeftRadius = 86,
                    borderTopRightRadius = 86,
                    borderBottomLeftRadius = 86,
                    borderBottomRightRadius = 86,
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
            infoBtn.RegisterCallback<MouseEnterEvent>(_ =>
            {
                m_IsPointerOnMenu = true;
                UpdateRadialMenuVisibility();
            });
            infoBtn.RegisterCallback<MouseLeaveEvent>(_ =>
            {
                m_IsPointerOnMenu = false;
                schedule.Execute(UpdateRadialMenuVisibility).ExecuteLater(80);
            });
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
            paramsBtn.RegisterCallback<MouseEnterEvent>(_ =>
            {
                m_IsPointerOnMenu = true;
                UpdateRadialMenuVisibility();
            });
            paramsBtn.RegisterCallback<MouseLeaveEvent>(_ =>
            {
                m_IsPointerOnMenu = false;
                schedule.Execute(UpdateRadialMenuVisibility).ExecuteLater(80);
            });
            m_RadialMenu.Add(paramsBtn);

            EnsureOverlayAttached();
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

        private void UpdateRadialMenuVisibility()
        {
            SetRadialMenuVisible(m_IsPointerOnNode || m_IsPointerOnMenu);
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
                m_RadialMenu.style.left = rect.x + (rect.width - 172f) * 0.5f;
                m_RadialMenu.style.top = rect.y + (rect.height - 172f) * 0.5f;
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
            const float ringRadius = 64f;
            const float center = 86f;
            const float startDeg = -135f; // left-top
            const float stepDeg = 28f;    // clockwise

            for (var i = 0; i < buttons.Count; i++)
            {
                var angle = (startDeg + stepDeg * i) * Mathf.Deg2Rad;
                var x = center + Mathf.Cos(angle) * ringRadius - diameter * 0.5f;
                var y = center + Mathf.Sin(angle) * ringRadius - diameter * 0.5f;
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
