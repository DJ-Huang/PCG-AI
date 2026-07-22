using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Read-only floating Node Info panel (Houdini-style).
    /// Shows geometry counts, bounding box, attributes and groups from the last cook —
    /// not node Inspector property values.
    /// </summary>
    public sealed class PcgNodeInfoPanel : VisualElement
    {
        private static readonly Color PointColor = new(0.35f, 0.85f, 0.45f);
        private static readonly Color PrimColor = new(0.95f, 0.65f, 0.25f);
        private static readonly Color VertexColor = new(0.85f, 0.4f, 0.85f);
        private static readonly Color PolyColor = new(0.4f, 0.7f, 0.95f);

        private readonly PcgGraphView m_GraphView;

        public PcgNodeInfoPanel(PcgGraphView graphView)
        {
            m_GraphView = graphView;
            style.position = Position.Absolute;
            style.width = 300;
            style.maxHeight = 480;
            style.backgroundColor = new Color(0.1f, 0.1f, 0.18f, 0.97f);
            style.borderTopLeftRadius = 8;
            style.borderTopRightRadius = 8;
            style.borderBottomLeftRadius = 8;
            style.borderBottomRightRadius = 8;
            style.borderTopWidth = 1;
            style.borderRightWidth = 1;
            style.borderBottomWidth = 1;
            style.borderLeftWidth = 1;
            style.borderTopColor = new Color(0.3f, 0.3f, 0.5f, 0.8f);
            style.borderRightColor = new Color(0.3f, 0.3f, 0.5f, 0.8f);
            style.borderBottomColor = new Color(0.3f, 0.3f, 0.5f, 0.8f);
            style.borderLeftColor = new Color(0.3f, 0.3f, 0.5f, 0.8f);
            style.display = DisplayStyle.None;
            pickingMode = PickingMode.Position;
        }

        public void Show(PcgGraphNodeBase node)
        {
            Clear();

            var displayName = node.GetDisplayTitle();
            var typeName = node.NodeType;
            var category = "";
            Color? categoryColor = null;
            if (node is PcgManifestNodeView mnode)
            {
                displayName = mnode.NodeDef.displayName ?? mnode.NodeDef.type;
                typeName = mnode.NodeDef.type;
                category = mnode.NodeDef.category ?? "";
                categoryColor = PcgNodeManifest.GetCategoryColor(category);
            }

            Add(MakeHeader(displayName, typeName, category, categoryColor));

            var scroll = new ScrollView(ScrollViewMode.Vertical)
            {
                style =
                {
                    flexGrow = 1,
                    maxHeight = 420,
                },
            };
            Add(scroll);

            if (m_GraphView == null ||
                !m_GraphView.TryGetNodeMeshStats(node.NodeId, out var stats))
            {
                scroll.Add(MakeHint("No cook data yet — run the graph to see geometry info."));
            }
            else
            {
                scroll.Add(MakeCountSection(stats));
                if (stats.hasBBox)
                    scroll.Add(MakeBBoxSection(stats));

                if (!m_GraphView.TryGetNodeAttrs(node.NodeId, out var attrs) || attrs == null)
                    attrs = new List<NodeAttrEntry>();
                AddAttrSection(scroll, "Point Attrs", attrs, "point");
                AddAttrSection(scroll, "Vertex Attrs", attrs, "vertex");
                AddAttrSection(scroll, "Prim Attrs", attrs, "primitive");
                AddAttrSection(scroll, "Detail Attrs", attrs, "detail");

                if (!m_GraphView.TryGetNodeGroups(node.NodeId, out var groups) || groups == null)
                    groups = new List<NodeGroupEntry>();
                AddGroupSection(scroll, "Point Groups", groups, "point");
                AddGroupSection(scroll, "Prim Groups", groups, "face");
                AddGroupSection(scroll, "Edge Groups", groups, "edge");
                AddGroupSection(scroll, "Vertex Groups", groups, "vertex");
            }

            WireCloseButton();
            style.display = DisplayStyle.Flex;
            PositionNear(node);
        }

        public void Hide()
        {
            style.display = DisplayStyle.None;
        }

        private void WireCloseButton()
        {
            var closeBtn = this.Q<Button>("info-close-btn");
            if (closeBtn != null)
                closeBtn.clicked += Hide;
        }

        private void PositionNear(PcgGraphNodeBase node)
        {
            var rect = node.GetPosition();
            const float panelWidth = 300f;
            const float gap = 14f;

            var left = rect.x - panelWidth - gap;
            var top = rect.y;
            if (left < 0)
                left = rect.x + rect.width + gap;

            if (m_GraphView != null)
            {
                var gvRect = m_GraphView.contentRect;
                var estHeight = 40 + childCount * 80;
                if (top + estHeight > gvRect.height)
                    top = Mathf.Max(0, gvRect.height - estHeight);
            }

            style.left = left;
            style.top = top;
        }

        private static VisualElement MakeHeader(
            string displayName, string typeName, string category, Color? color = null)
        {
            var container = new VisualElement();
            var bgColor = color ?? new Color(0.35f, 0.35f, 0.45f);

            var headerRow = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    backgroundColor = bgColor,
                    borderTopLeftRadius = 7,
                    borderTopRightRadius = 7,
                },
            };

            headerRow.Add(new Label(displayName)
            {
                style =
                {
                    fontSize = 13,
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = Color.white,
                    paddingTop = 6,
                    paddingBottom = 4,
                    paddingLeft = 10,
                    paddingRight = 4,
                    flexGrow = 1,
                },
            });

            var closeBtn = new Button { text = "×", name = "info-close-btn", tooltip = "Close" };
            closeBtn.style.fontSize = 14;
            closeBtn.style.color = new Color(0.7f, 0.7f, 0.75f);
            closeBtn.style.backgroundColor = new Color(0, 0, 0, 0);
            closeBtn.style.borderTopWidth = 0;
            closeBtn.style.borderRightWidth = 0;
            closeBtn.style.borderBottomWidth = 0;
            closeBtn.style.borderLeftWidth = 0;
            closeBtn.style.paddingTop = 4;
            closeBtn.style.paddingBottom = 4;
            closeBtn.style.paddingLeft = 6;
            closeBtn.style.paddingRight = 6;
            closeBtn.style.flexShrink = 0;
            headerRow.Add(closeBtn);
            container.Add(headerRow);

            container.Add(new Label($"{typeName}{(string.IsNullOrEmpty(category) ? "" : " · " + category)}")
            {
                style =
                {
                    fontSize = 9,
                    color = new Color(0.55f, 0.55f, 0.6f),
                    paddingLeft = 10,
                    paddingRight = 10,
                    paddingTop = 2,
                    paddingBottom = 4,
                    borderBottomWidth = 1,
                    borderBottomColor = new Color(0.2f, 0.2f, 0.3f, 0.5f),
                },
            });
            return container;
        }

        private static VisualElement MakeHint(string text)
        {
            return new Label(text)
            {
                style =
                {
                    fontSize = 10,
                    color = new Color(0.55f, 0.55f, 0.6f),
                    whiteSpace = WhiteSpace.Normal,
                    paddingLeft = 10,
                    paddingRight = 10,
                    paddingTop = 10,
                    paddingBottom = 10,
                },
            };
        }

        private static VisualElement MakeCountSection(PcgNodeMeshStats stats)
        {
            var container = MakeSectionContainer("Geometry");
            container.Add(MakeColoredCountRow("Points", stats.pointCount, PointColor));
            container.Add(MakeColoredCountRow("Primitives", stats.faceCount, PrimColor));
            container.Add(MakeColoredCountRow("Vertices", stats.vertexCount, VertexColor));
            container.Add(MakeColoredCountRow("Polygons", stats.faceCount, PolyColor));
            if (stats.triangleCount > 0 && stats.triangleCount != stats.faceCount)
                container.Add(MakeColoredCountRow("Triangles", stats.triangleCount, PolyColor));
            return container;
        }

        private static VisualElement MakeBBoxSection(PcgNodeMeshStats stats)
        {
            var min = stats.bboxMin;
            var max = stats.bboxMax;
            var size = max - min;
            var center = (min + max) * 0.5f;
            var container = MakeSectionContainer("Bounding Box");
            container.Add(MakeKvRow("Center", FormatVec3(center)));
            container.Add(MakeKvRow("Min", FormatVec3(min)));
            container.Add(MakeKvRow("Max", FormatVec3(max)));
            container.Add(MakeKvRow("Size", FormatVec3(size)));
            return container;
        }

        private static void AddAttrSection(
            VisualElement parent, string title, List<NodeAttrEntry> attrs, string owner)
        {
            var filtered = attrs
                .Where(a => string.Equals(a.owner, owner, System.StringComparison.OrdinalIgnoreCase))
                .OrderBy(a => a.name)
                .ToList();
            if (filtered.Count == 0)
                return;

            var container = MakeSectionContainer(title);
            foreach (var attr in filtered)
                container.Add(MakeKvRow(attr.name, FormatAttrType(attr)));
            parent.Add(container);
        }

        private static void AddGroupSection(
            VisualElement parent, string title, List<NodeGroupEntry> groups, string domain)
        {
            var filtered = groups
                .Where(g => string.Equals(g.domain, domain, System.StringComparison.OrdinalIgnoreCase))
                .OrderBy(g => g.name)
                .ToList();
            if (filtered.Count == 0)
                return;

            var container = MakeSectionContainer(title);
            foreach (var g in filtered)
                container.Add(MakeKvRow(g.name, g.count.ToString("N0", CultureInfo.InvariantCulture)));
            parent.Add(container);
        }

        private static VisualElement MakeSectionContainer(string title)
        {
            var container = new VisualElement
            {
                style =
                {
                    paddingLeft = 10,
                    paddingRight = 10,
                    paddingTop = 6,
                    paddingBottom = 6,
                    borderBottomWidth = 1,
                    borderBottomColor = new Color(0.17f, 0.17f, 0.24f, 0.5f),
                },
            };
            container.Add(new Label(title.ToUpperInvariant())
            {
                style =
                {
                    fontSize = 8,
                    color = new Color(0.4f, 0.4f, 0.45f),
                    unityTextAlign = TextAnchor.UpperLeft,
                    marginBottom = 4,
                },
            });
            return container;
        }

        private static VisualElement MakeColoredCountRow(string key, int value, Color valueColor)
        {
            var row = MakeRowShell();
            row.Add(MakeKeyLabel(key));
            row.Add(new Label(value.ToString("N0", CultureInfo.InvariantCulture))
            {
                style =
                {
                    fontSize = 11,
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = valueColor,
                    unityTextAlign = TextAnchor.MiddleRight,
                },
            });
            return row;
        }

        private static VisualElement MakeKvRow(string key, string value)
        {
            var row = MakeRowShell();
            row.Add(MakeKeyLabel(key));
            row.Add(new Label(value)
            {
                style =
                {
                    fontSize = 10,
                    color = new Color(0.85f, 0.85f, 0.85f),
                    unityTextAlign = TextAnchor.MiddleRight,
                    whiteSpace = WhiteSpace.Normal,
                    maxWidth = 170,
                },
            });
            return row;
        }

        private static VisualElement MakeRowShell() =>
            new()
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    marginBottom = 1,
                    alignItems = Align.Center,
                },
            };

        private static Label MakeKeyLabel(string key) =>
            new(key)
            {
                style =
                {
                    fontSize = 10,
                    color = new Color(0.65f, 0.7f, 0.8f),
                    flexShrink = 0,
                    flexGrow = 1,
                },
            };

        private static string FormatVec3(Vector3 v) =>
            string.Format(CultureInfo.InvariantCulture, "({0:0.###}, {1:0.###}, {2:0.###})",
                v.x, v.y, v.z);

        private static string FormatAttrType(NodeAttrEntry attr)
        {
            var type = (attr.type ?? "float").ToLowerInvariant();
            var suffix = type switch
            {
                "int" => "int",
                "string" => "str",
                _ => "flt",
            };
            var tuple = Mathf.Max(1, attr.tuple_size);
            return tuple == 1 ? $"1{suffix}" : $"{tuple}{suffix}";
        }
    }
}
