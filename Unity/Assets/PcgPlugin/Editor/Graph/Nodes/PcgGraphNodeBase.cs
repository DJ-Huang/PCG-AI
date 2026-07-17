using System;
using System.Collections.Generic;
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
        private VisualElement m_NodeFrame;
        private bool m_IsPreviewActive;

        internal const float NodeWidth = 92f * 2f / 3f;
        internal const float NodeHeight = 23f;
        internal const float NodeCornerRadius = 12f;
        internal const float PinSize = 14f * 2f / 3f;
        internal const float PinMargin = 4f * 2f / 3f;
        internal const float PinBorderWidth = 2f * 2f / 3f;
        internal const float RadialMenuSize = 120f;
        internal const float RadialMenuRadius = RadialMenuSize * 0.5f;
        internal const float RadialMenuOffsetX = -3f;
        internal const float RadialMenuOffsetY = -5f;

        protected PcgGraphNodeBase()
        {
            // The Node root itself is the drag hit target (SelectionDragger checks
            // evt.target as Node). Children that are purely visual use PickingMode.Ignore
            // so they don't intercept clicks meant for the node body.
            pickingMode = PickingMode.Position;
            style.minWidth = NodeWidth;
            style.width = NodeWidth;
            style.maxWidth = NodeWidth;
            style.minHeight = NodeHeight;
            style.height = NodeHeight;
            style.maxHeight = NodeHeight;
            ConfigureHoudiniPortLayout();
            BuildRightTitleUI();
            BuildHoverRadialMenu();
            RegisterCallback<GeometryChangedEvent>(_ =>
            {
                EnsureNodeFrame();
                RefreshSelectionVisual();
                if (inputContainer.parent != this || outputContainer.parent != this)
                    EnsureHoudiniPortContainers();
                UpdateOverlayPlacement();
            });
            RegisterCallback<MouseEnterEvent>(_ => ShowRadialMenu());
            RegisterCallback<MouseDownEvent>(evt =>
            {
                if (evt.clickCount >= 2)
                {
                    if (!HandleDoubleClick())
                        BeginRename();
                    evt.StopPropagation();
                }
            });
        }

        public void Initialize(string id, Vector2 position)
        {
            NodeId = id;
            expanded = true;
            SetPosition(new Rect(position, new Vector2(NodeWidth, NodeHeight)));
            BuildPorts();
            EnsureFixedPortRows();
            RefreshExpandedState();
            RefreshPorts();
            EnsureNodeFrame();
            EnsureHoudiniPortContainers();
            EnsureFixedPortRows();
            UpdateDisplayedTitle();
        }

        public Port GetInputPort() => InputPort;
        public Port GetOutputPort() => OutputPort;

        public virtual Port FindInputPort(string handle = "in") => InputPort;
        public virtual Port FindOutputPort(string handle = "out") => OutputPort;

        protected virtual void BuildPorts() { }

        protected Port CreatePort(Direction direction, string portName, string pinType = null)
        {
            var capacity = direction == Direction.Input ? Port.Capacity.Single : Port.Capacity.Multi;
            var port = PcgPort.Create(Orientation.Vertical, direction, capacity, typeof(float));
            port.portName = "";
            port.tooltip = portName;
            port.userData = portName;
            StyleHoudiniPin(port, direction, pinType);
            return port;
        }

        public string GetUserTitle() => m_UserTitle;

        public string GetDisplayTitle() => m_RightTitleLabel?.text ?? title;

        protected virtual bool HandleDoubleClick() => false;

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
            style.overflow = Overflow.Visible;
            style.flexDirection = FlexDirection.Column;
            style.borderTopWidth = 0;
            style.borderRightWidth = 0;
            style.borderBottomWidth = 0;
            style.borderLeftWidth = 0;
            style.backgroundColor = Color.clear;
            style.borderTopLeftRadius = NodeCornerRadius;
            style.borderTopRightRadius = NodeCornerRadius;
            style.borderBottomLeftRadius = NodeCornerRadius;
            style.borderBottomRightRadius = NodeCornerRadius;

            EnsureNodeFrame();

            // Default GraphView chrome is unused — one custom frame + overlay ports.
            titleContainer.style.display = DisplayStyle.None;
            topContainer.style.display = DisplayStyle.None;

            // Keep an invisible body so GraphView's layout has content, but don't
            // intercept hits — the Node root itself is the drag target.
            mainContainer.style.display = DisplayStyle.Flex;
            mainContainer.style.flexGrow = 1;
            mainContainer.style.minWidth = NodeWidth;
            mainContainer.style.width = NodeWidth;
            mainContainer.style.minHeight = NodeHeight;
            mainContainer.style.height = NodeHeight;
            mainContainer.style.opacity = 0;
            mainContainer.style.overflow = Overflow.Visible;
            mainContainer.style.borderTopWidth = 0;
            mainContainer.style.borderRightWidth = 0;
            mainContainer.style.borderBottomWidth = 0;
            mainContainer.style.borderLeftWidth = 0;
            mainContainer.style.backgroundColor = Color.clear;
            mainContainer.style.borderTopLeftRadius = NodeCornerRadius;
            mainContainer.style.borderTopRightRadius = NodeCornerRadius;
            mainContainer.style.borderBottomLeftRadius = NodeCornerRadius;
            mainContainer.style.borderBottomRightRadius = NodeCornerRadius;
            mainContainer.pickingMode = PickingMode.Ignore;
            extensionContainer.pickingMode = PickingMode.Ignore;

            StylePortRow(inputContainer);
            StylePortRow(outputContainer);
            EnsureHoudiniPortContainers();
        }

        private void EnsureNodeFrame()
        {
            if (m_NodeFrame == null)
            {
                m_NodeFrame = new VisualElement { name = "pcg-node-frame" };
                m_NodeFrame.pickingMode = PickingMode.Ignore;
                m_NodeFrame.style.position = Position.Absolute;
                m_NodeFrame.style.left = 0;
                m_NodeFrame.style.top = 0;
                m_NodeFrame.style.right = 0;
                m_NodeFrame.style.bottom = 0;
                hierarchy.Insert(0, m_NodeFrame);
            }
            else if (m_NodeFrame.parent != this)
            {
                m_NodeFrame.RemoveFromHierarchy();
                hierarchy.Insert(0, m_NodeFrame);
            }
            else if (hierarchy.IndexOf(m_NodeFrame) != 0)
            {
                m_NodeFrame.SendToBack();
            }

            m_NodeFrame.style.backgroundColor = new Color(0.17f, 0.17f, 0.17f, 1f);
            m_NodeFrame.style.borderTopLeftRadius = NodeCornerRadius;
            m_NodeFrame.style.borderTopRightRadius = NodeCornerRadius;
            m_NodeFrame.style.borderBottomLeftRadius = NodeCornerRadius;
            m_NodeFrame.style.borderBottomRightRadius = NodeCornerRadius;
            ApplyNodeFrameBorder(highlighted: false);
        }

        /// <summary>
        /// Called by PcgGraphView after any selection change. Reads the
        /// actual USS "selected" class state and updates the frame border
        /// accordingly.
        /// </summary>
        internal void RefreshSelectionVisual()
        {
            SuppressBuiltinSelectionStyle();
            UpdateNodeFrameBorder();
        }

        /// <summary>
        /// Suppress Unity's built-in GraphView selection styling (USS .selected)
        /// on the Node root and mainContainer — border AND background.
        /// </summary>
        private void SuppressBuiltinSelectionStyle()
        {
            style.borderTopWidth = 0;
            style.borderRightWidth = 0;
            style.borderBottomWidth = 0;
            style.borderLeftWidth = 0;
            style.backgroundColor = Color.clear;
            style.borderTopLeftRadius = NodeCornerRadius;
            style.borderTopRightRadius = NodeCornerRadius;
            style.borderBottomLeftRadius = NodeCornerRadius;
            style.borderBottomRightRadius = NodeCornerRadius;

            mainContainer.style.borderTopWidth = 0;
            mainContainer.style.borderRightWidth = 0;
            mainContainer.style.borderBottomWidth = 0;
            mainContainer.style.borderLeftWidth = 0;
            mainContainer.style.backgroundColor = Color.clear;
            mainContainer.style.borderTopLeftRadius = NodeCornerRadius;
            mainContainer.style.borderTopRightRadius = NodeCornerRadius;
            mainContainer.style.borderBottomLeftRadius = NodeCornerRadius;
            mainContainer.style.borderBottomRightRadius = NodeCornerRadius;
        }

        private void UpdateNodeFrameBorder()
        {
            if (m_NodeFrame == null)
                return;

            // Re-assert opaque background — USS .selected may have overridden it
            m_NodeFrame.style.backgroundColor = new Color(0.17f, 0.17f, 0.17f, 1f);

            var highlighted = ClassListContains("selected") || m_IsPreviewActive;
            const float normalBorder = 1f;
            const float highlightBorder = 2f;
            var width = highlighted ? highlightBorder : normalBorder;
            var color = highlighted
                ? new Color(0.25f, 0.75f, 1f, 1f)
                : new Color(0.35f, 0.35f, 0.35f, 1f);

            m_NodeFrame.style.borderTopWidth = width;
            m_NodeFrame.style.borderRightWidth = width;
            m_NodeFrame.style.borderBottomWidth = width;
            m_NodeFrame.style.borderLeftWidth = width;
            m_NodeFrame.style.borderTopColor = color;
            m_NodeFrame.style.borderRightColor = color;
            m_NodeFrame.style.borderBottomColor = color;
            m_NodeFrame.style.borderLeftColor = color;
        }

        private void ApplyNodeFrameBorder(bool highlighted)
        {
            UpdateNodeFrameBorder();
        }

        private static void StylePortRow(VisualElement row)
        {
            row.style.flexDirection = FlexDirection.Row;
            row.style.justifyContent = Justify.Center;
            row.style.alignItems = Align.Center;
            row.style.flexWrap = Wrap.Wrap;
            row.style.minHeight = PinSize * 0.6f;
            row.style.height = PinSize * 0.6f;
            row.style.marginTop = 0;
            row.style.marginBottom = 0;
            row.style.marginLeft = 0;
            row.style.marginRight = 0;
            row.style.paddingLeft = 0;
            row.style.paddingRight = 0;
            row.style.paddingTop = 0;
            row.style.paddingBottom = 0;
            row.style.overflow = Overflow.Visible;
            row.style.borderTopWidth = 0;
            row.style.borderRightWidth = 0;
            row.style.borderBottomWidth = 0;
            row.style.borderLeftWidth = 0;
            row.style.backgroundColor = Color.clear;
            row.style.position = Position.Absolute;
            row.pickingMode = PickingMode.Ignore;
            row.style.left = 0;
            row.style.right = 0;
            row.style.width = Length.Percent(100);
        }

        /// <summary>
        /// Mount input/output rows on the Node root (not contentContainer/mainContainer).
        /// Node.Add() targets contentContainer, so ports would stay under bordered parents;
        /// hierarchy.Add attaches to the Node element itself. Absolute positioning then
        /// paints after Relative siblings (topContainer border), keeping dots on top.
        /// </summary>
        private void EnsureHoudiniPortContainers()
        {
            StylePortRow(inputContainer);
            StylePortRow(outputContainer);

            inputContainer.style.top = 0;
            inputContainer.style.bottom = StyleKeyword.Auto;

            outputContainer.style.top = StyleKeyword.Auto;
            outputContainer.style.bottom = 0;

            if (inputContainer.parent != this)
            {
                inputContainer.RemoveFromHierarchy();
                hierarchy.Add(inputContainer);
            }
            else
            {
                inputContainer.BringToFront();
            }

            if (outputContainer.parent != this)
            {
                outputContainer.RemoveFromHierarchy();
                hierarchy.Add(outputContainer);
            }
            else
            {
                outputContainer.BringToFront();
            }

            // Absolute Absolute: later sibling paints above earlier — outputs last.
            outputContainer.BringToFront();
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

        private static readonly Dictionary<string, Color> s_PinTypeColors = new()
        {
            { "SpatialPoint", new Color(0f, 0.8f, 1f) },       // cyan
            { "SpatialSpline", new Color(0.3f, 0.9f, 0.4f) },    // green
            { "SpatialMesh", new Color(1f, 0.6f, 0f) },          // orange
            { "Param", new Color(1f, 0.85f, 0f) },               // gold
            { "Texture", new Color(0.7f, 0.3f, 0.9f) },          // purple
            { "Any", new Color(0.65f, 0.65f, 0.65f) },           // gray
        };

        private static Color GetPinTypeColor(string pinType)
        {
            return !string.IsNullOrEmpty(pinType) && s_PinTypeColors.TryGetValue(pinType, out var c)
                ? c
                : new Color(0.65f, 0.65f, 0.65f);
        }

        protected static void StyleHoudiniPin(Port port, Direction direction, string pinType = null)
        {
            var pinSize = PinSize;
            var pinColor = GetPinTypeColor(pinType);

            // Style the port element itself as the visible dot
            port.style.minWidth = pinSize;
            port.style.width = pinSize;
            port.style.height = pinSize;
            port.style.maxWidth = pinSize;
            port.style.maxHeight = pinSize;
            port.style.marginLeft = PinMargin;
            port.style.marginRight = PinMargin;
            port.style.marginTop = 0;
            port.style.marginBottom = 0;
            port.style.paddingLeft = 0;
            port.style.paddingRight = 0;
            port.style.paddingTop = 0;
            port.style.paddingBottom = 0;
            port.style.backgroundColor = pinColor;
            port.style.borderTopLeftRadius = pinSize * 0.5f;
            port.style.borderTopRightRadius = pinSize * 0.5f;
            port.style.borderBottomLeftRadius = pinSize * 0.5f;
            port.style.borderBottomRightRadius = pinSize * 0.5f;
            port.style.borderTopWidth = PinBorderWidth;
            port.style.borderRightWidth = PinBorderWidth;
            port.style.borderBottomWidth = PinBorderWidth;
            port.style.borderLeftWidth = PinBorderWidth;
            var borderColor = new Color(0.08f, 0.08f, 0.08f, 0.85f);
            port.style.borderTopColor = borderColor;
            port.style.borderRightColor = borderColor;
            port.style.borderBottomColor = borderColor;
            port.style.borderLeftColor = borderColor;

            // Hide the internal connector — the port itself is the dot now
            var connector = port.Q<VisualElement>("connector");
            if (connector != null)
                connector.style.display = DisplayStyle.None;

            // Also try by USS class name (Tuanjie may use different internal name)
            var connectorByClass = port.Query<VisualElement>(className: "connector").First();
            if (connectorByClass != null)
                connectorByClass.style.display = DisplayStyle.None;

            // Hide port label (we show labels via tooltips / right-side title)
            var portLabel = port.Q<Label>();
            if (portLabel != null)
                portLabel.style.display = DisplayStyle.None;

            // Straddle the node edge: inputs half above top, outputs half below bottom.
            // Port rows are Absolute on the node root, so translate is relative to that edge.
            port.style.position = Position.Relative;
            if (direction == Direction.Input)
            {
                port.style.alignSelf = Align.Center;
                port.style.translate = new Translate(0, -pinSize * 0.5f, 0);
            }
            else
            {
                port.style.alignSelf = Align.Center;
                port.style.translate = new Translate(0, pinSize * 0.5f, 0);
            }
        }

        private static float RightTitleTop => Mathf.Max(0f, (NodeHeight - 14f) * 0.5f);

        private void BuildRightTitleUI()
        {
            m_RightTitleLabel = new Label
            {
                style =
                {
                    position = Position.Absolute,
                    right = -132,
                    top = RightTitleTop,
                    minWidth = 120,
                    maxWidth = 220,
                    unityTextAlign = TextAnchor.MiddleLeft,
                    color = new Color(0.9f, 0.9f, 0.9f),
                    unityFontStyleAndWeight = FontStyle.Bold,
                    whiteSpace = WhiteSpace.Normal,
                    overflow = Overflow.Visible,
                },
            };
            // The title label is a display-only overlay in contentViewContainer.
            // It must NOT intercept hits — otherwise it can overlap a neighbouring
            // node's body and block SelectionDragger from starting a drag.
            m_RightTitleLabel.pickingMode = PickingMode.Ignore;
        }

        private void BeginRename()
        {
            if (m_TitleEditor != null)
                return;

            m_TitleEditor = new TextField { value = GetDisplayTitle() };
            m_TitleEditor.style.position = Position.Absolute;
            m_TitleEditor.style.right = -132;
            m_TitleEditor.style.top = RightTitleTop - 2f;
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
                graphView.ShowNodeInfoPanel(this);
            });
            infoBtn.tooltip = "Node info";
            m_RadialMenu.Add(infoBtn);

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
            m_IsPreviewActive = isActive;
            UpdateNodeFrameBorder();
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
            m_IsPreviewActive = highlighted;
            UpdateNodeFrameBorder();
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

        /// <summary>
        /// Remove title/radial overlays from the graph content container.
        /// Required before <see cref="GraphView.DeleteElements"/> — overlays live
        /// outside the node hierarchy and would otherwise accumulate on reload.
        /// </summary>
        internal void DetachOverlays()
        {
            m_RightTitleLabel?.RemoveFromHierarchy();
            m_RadialMenu?.RemoveFromHierarchy();
            m_TitleEditor?.RemoveFromHierarchy();
            m_TitleEditor = null;
            m_OverlayAttached = false;
            m_RadialMenuOpen = false;
        }

        private void UpdateOverlayPlacement()
        {
            EnsureOverlayAttached();
            var rect = GetPosition();

            if (m_RightTitleLabel != null)
            {
                m_RightTitleLabel.style.left = rect.x + rect.width + 14;
                m_RightTitleLabel.style.top = rect.y + RightTitleTop;
            }

            if (m_TitleEditor != null)
            {
                m_TitleEditor.style.left = rect.x + rect.width + 14;
                m_TitleEditor.style.top = rect.y + RightTitleTop - 2f;
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
            const float stepDeg = 48f;

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
