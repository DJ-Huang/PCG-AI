using DJTechRuntime.PCG;
using UnityEditor.Experimental.GraphView;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// References a node in the immediate parent scope (Houdini Object Merge style).
    /// Resolved at flatten / preview by grafting parent upstream.
    /// </summary>
    public sealed class PcgSubgraphParentRefNodeView : PcgGraphNodeBase
    {
        private PcgNodeData m_Data = new();

        public override string NodeType => PcgStructuralNodeTypes.SubgraphParentRef;

        protected override void BuildPorts()
        {
            OutputPort = CreatePort(Direction.Output, "out", "Any");
            outputContainer.Add(OutputPort);
        }

        public override void ApplyData(PcgNodeData data)
        {
            m_Data = data ?? new PcgNodeData();
            var parentId = m_Data.GetRaw("parentNodeId")?.ToString() ?? "";
            var parentHandle = m_Data.GetRaw("parentHandle")?.ToString() ?? "out";
            title = string.IsNullOrEmpty(parentId)
                ? "Parent Ref"
                : $"Parent: {parentId}.{parentHandle}";
            SetUserTitle(m_Data.GetRaw("__nodeTitle")?.ToString() ?? "");
        }

        public override PcgNodeData CollectData() => m_Data.Clone();

        public string ParentNodeId => m_Data.GetRaw("parentNodeId")?.ToString() ?? "";

        public string ParentHandle
        {
            get
            {
                var handle = m_Data.GetRaw("parentHandle")?.ToString();
                return string.IsNullOrEmpty(handle) ? "out" : handle;
            }
        }

        internal void SetParentReference(string parentNodeId, string parentHandle)
        {
            m_Data.SetRaw("parentNodeId", parentNodeId ?? "");
            m_Data.SetRaw("parentHandle", string.IsNullOrEmpty(parentHandle) ? "out" : parentHandle);
            ApplyData(m_Data);
        }
    }
}
