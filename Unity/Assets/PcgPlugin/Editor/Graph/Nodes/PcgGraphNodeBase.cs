using System;
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

        protected PcgGraphNodeBase()
        {
            style.minWidth = 100;
            style.width = 110;
            style.minHeight = 56;
            ConfigureHoudiniPortLayout();
            BuildRightTitleUI();
            BuildHoverRadialMenu();
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
            RefreshExpandedState();
            RefreshPorts();
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
            topContainer.style.flexDirection = FlexDirection.Column;
            topContainer.style.alignItems = Align.Stretch;

            titleContainer.style.alignSelf = Align.Center;

            inputContainer.style.flexDirection = FlexDirection.Row;
            inputContainer.style.justifyContent = Justify.Center;
            inputContainer.style.alignSelf = Align.Stretch;
            inputContainer.style.flexWrap = Wrap.Wrap;
            inputContainer.style.marginTop = -8;
            inputContainer.style.marginBottom = 2;

            outputContainer.style.flexDirection = FlexDirection.Row;
            outputContainer.style.justifyContent = Justify.Center;
            outputContainer.style.alignSelf = Align.Stretch;
            outputContainer.style.flexWrap = Wrap.Wrap;
            outputContainer.style.marginTop = 2;
            outputContainer.style.marginBottom = -8;

            topContainer.Remove(inputContainer);
            topContainer.Remove(outputContainer);
            topContainer.Insert(0, inputContainer);
            topContainer.Add(outputContainer);

            titleContainer.style.display = DisplayStyle.None;
            mainContainer.style.overflow = Overflow.Visible;
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
                port.style.alignSelf = Align.FlexStart;
            else
                port.style.alignSelf = Align.FlexEnd;
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
            mainContainer.Add(m_RightTitleLabel);
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
            mainContainer.Add(m_TitleEditor);
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

            mainContainer.Remove(m_TitleEditor);
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
                    width = 142,
                    height = 142,
                    borderTopLeftRadius = 71,
                    borderTopRightRadius = 71,
                    borderBottomLeftRadius = 71,
                    borderBottomRightRadius = 71,
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
            m_RadialMenu.pickingMode = PickingMode.Position;
            m_RadialMenu.RegisterCallback<MouseEnterEvent>(_ =>
            {
                m_IsPointerOnMenu = true;
                UpdateRadialMenuVisibility();
            });
            m_RadialMenu.RegisterCallback<MouseLeaveEvent>(_ =>
            {
                m_IsPointerOnMenu = false;
                schedule.Execute(UpdateRadialMenuVisibility).ExecuteLater(50);
            });

            var infoBtn = CreateCircleButton("i", 60, 10, () =>
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

            var paramsBtn = CreateCircleButton("P", 60, 108, () =>
            {
                var graphView = GetFirstAncestorOfType<PcgGraphView>();
                if (graphView?.Blackboard == null)
                    return;
                if (graphView.Blackboard.style.display.value != DisplayStyle.Flex)
                    graphView.Blackboard.ToggleVisible();
            });
            paramsBtn.tooltip = "Parameters";
            m_RadialMenu.Add(paramsBtn);

            mainContainer.Add(m_RadialMenu);
        }

        private static Button CreateCircleButton(string text, float left, float top, Action onClick)
        {
            var button = new Button(onClick)
            {
                text = text,
                style =
                {
                    position = Position.Absolute,
                    left = left,
                    top = top,
                    width = 22,
                    height = 22,
                    borderTopLeftRadius = 11,
                    borderTopRightRadius = 11,
                    borderBottomLeftRadius = 11,
                    borderBottomRightRadius = 11,
                    unityTextAlign = TextAnchor.MiddleCenter,
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
            if (m_RadialMenu != null)
                m_RadialMenu.style.display = isVisible ? DisplayStyle.Flex : DisplayStyle.None;
        }

        private void UpdateRadialMenuVisibility()
        {
            SetRadialMenuVisible(m_IsPointerOnNode || m_IsPointerOnMenu);
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
