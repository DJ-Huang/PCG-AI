using System.Collections.Generic;
using System.Linq;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Read-only floating Node Info panel (Houdini-style).
    /// Shows: node type/category, output groups, upstream groups, properties with current values.
    /// </summary>
    public sealed class PcgNodeInfoPanel : VisualElement
    {
        private readonly PcgGraphView m_GraphView;

        public PcgNodeInfoPanel(PcgGraphView graphView)
        {
            m_GraphView = graphView;
            style.position = Position.Absolute;
            style.width = 280;
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
            pickingMode = PickingMode.Ignore; // let clicks pass through to graph
        }

        public void Show(PcgGraphNodeBase node)
        {
            Clear();

            if (node is not PcgManifestNodeView mnode)
            {
                // Non-manifest node: minimal info
                Add(MakeHeader(node.GetDisplayTitle(), node.NodeType, ""));
                Add(MakeSection("Type", new[]
                {
                    ("Node Type", node.NodeType),
                }));
                style.display = DisplayStyle.Flex;
                PositionNear(node);
                return;
            }

            var def = mnode.NodeDef;
            var data = mnode.NodeData;

            // Header
            var categoryColor = PcgNodeManifest.GetCategoryColor(def.category);
            Add(MakeHeader(def.displayName ?? def.type, def.type, def.category, categoryColor));

            // Output Groups
            var outputGroups = CollectOutputGroups(def, data);
            if (outputGroups.Count > 0)
            {
                var rows = outputGroups.Select(g =>
                    (g.name, $"{g.domain}{(g.condition != null ? "  [if " + g.condition + "]" : "")}"));
                Add(MakeSection($"Output Groups ({outputGroups.Count})", rows));
            }

            // Upstream Groups
            var inspector = m_GraphView.Inspector;
            if (inspector != null)
            {
                var upstream = inspector.ResolveUpstreamGroups(node.NodeId);
                if (upstream.Count > 0)
                {
                    var rows = upstream.Select(g =>
                        ($"{g.name} ({g.domain})", g.sourceNodeType));
                    Add(MakeSection($"Upstream Groups ({upstream.Count})", rows));
                }
            }

            // Properties
            var propRows = new List<(string, string)>();
            foreach (var kv in def.properties)
            {
                var key = kv.Key;
                var prop = kv.Value;
                var rawVal = data.GetRaw(key);
                string valStr = rawVal == null ? "—" :
                    prop.type == "boolean" ? (rawVal is bool b && b ? "true" : "false") :
                    rawVal.ToString();
                propRows.Add((key, valStr));
            }
            if (propRows.Count > 0)
                Add(MakeSection($"Properties ({propRows.Count})", propRows));

            // Pins
            var pinRows = new List<(string, string)>();
            foreach (var pin in def.inputs)
                pinRows.Add(("→ " + pin.label, pin.pinType));
            foreach (var pin in def.outputs)
                pinRows.Add(("← " + pin.label, pin.pinType));
            if (pinRows.Count > 0)
                Add(MakeSection("Pins", pinRows));

            style.display = DisplayStyle.Flex;
            PositionNear(node);
        }

        public void Hide()
        {
            style.display = DisplayStyle.None;
        }

        private void PositionNear(PcgGraphNodeBase node)
        {
            var rect = node.GetPosition();
            var left = rect.x + rect.width + 14;
            var top = rect.y;

            // If panel would go off right edge, place on left side
            var graphView = m_GraphView;
            if (graphView != null)
            {
                var gvRect = graphView.contentRect;
                // Estimate panel height (~40px header + 80px per section, rough)
                var estHeight = 40 + childCount * 80;
                if (top + estHeight > gvRect.height)
                    top = Mathf.Max(0, gvRect.height - estHeight);
            }

            style.left = left;
            style.top = top;
        }

        // ── Builders ──────────────────────────────────────

        private static VisualElement MakeHeader(string displayName, string typeName, string category, Color? color = null)
        {
            var container = new VisualElement();
            var bgColor = color ?? new Color(0.35f, 0.35f, 0.45f);

            var titleBar = new Label(displayName)
            {
                style =
                {
                    fontSize = 13,
                    unityFontStyleAndWeight = FontStyle.Bold,
                    color = Color.white,
                    backgroundColor = bgColor,
                    paddingTop = 6,
                    paddingBottom = 4,
                    paddingLeft = 10,
                    paddingRight = 10,
                    borderTopLeftRadius = 7,
                    borderTopRightRadius = 7,
                },
            };
            container.Add(titleBar);

            var subLabel = new Label($"{typeName}{(string.IsNullOrEmpty(category) ? "" : " · " + category)}")
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
            };
            container.Add(subLabel);

            return container;
        }

        private static VisualElement MakeSection(string title, IEnumerable<(string key, string value)> rows)
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

            var titleLabel = new Label(title)
            {
                style =
                {
                    fontSize = 8,
                    color = new Color(0.4f, 0.4f, 0.45f),
                    unityTextAlign = TextAnchor.UpperLeft,
                    marginBottom = 4,
                },
            };
            // Uppercase via text
            titleLabel.text = title.ToUpperInvariant();
            container.Add(titleLabel);

            foreach (var (key, value) in rows)
            {
                var row = new VisualElement
                {
                    style =
                    {
                        flexDirection = FlexDirection.Row,
                        marginBottom = 1,
                    },
                };
                var keyLabel = new Label(key)
                {
                    style =
                    {
                        fontSize = 10,
                        color = new Color(0.65f, 0.7f, 0.8f),
                        flexShrink = 0,
                        flexGrow = 1,
                    },
                };
                var valLabel = new Label(value)
                {
                    style =
                    {
                        fontSize = 10,
                        color = new Color(0.85f, 0.85f, 0.85f),
                        unityTextAlign = TextAnchor.MiddleRight,
                    },
                };
                row.Add(keyLabel);
                row.Add(valLabel);
                container.Add(row);
            }

            return container;
        }

        // ── Group collection ───────────────────────────────

        private static List<(string name, string domain, string condition)> CollectOutputGroups(
            ManifestNodeDef def, PcgNodeData data)
        {
            var result = new List<(string, string, string)>();

            foreach (var og in def.outputGroups)
            {
                string condition = null;
                if (!string.IsNullOrEmpty(og.condition))
                {
                    var condVal = data.GetRaw(og.condition);
                    if (condVal is bool b && !b)
                    {
                        condition = og.condition;
                        // Still list it, but marked as conditional/inactive
                    }
                    else
                    {
                        // condition met
                    }
                }

                string name = og.name;
                if (og.dynamic)
                {
                    var val = data.GetRaw(og.name)?.ToString();
                    if (string.IsNullOrWhiteSpace(val))
                        continue;
                    name = val;
                }

                result.Add((name, og.domain, condition));
            }

            // Also check properties with isGroupOutput (dynamic groups without manifest outputGroups)
            if (def.outputGroups.Count == 0)
            {
                foreach (var kv in def.properties)
                {
                    if (!kv.Value.isGroupOutput)
                        continue;
                    var groupName = data.GetRaw(kv.Key)?.ToString();
                    if (string.IsNullOrWhiteSpace(groupName))
                        continue;
                    var domain = kv.Value.groupDomain ?? "edge";
                    result.Add((groupName, domain, null));
                }
            }

            return result;
        }
    }
}
