using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgSpawnPointsNodeView : PcgGraphNodeBase
    {
        private PcgNodeData _data = new();

        public override string NodeType => PcgNodeTypes.SpawnPoints;

        public PcgSpawnPointsNodeView()
        {
            title = "SpawnPoints";
        }

        public override void ApplyData(PcgNodeData data)
        {
            _data = data;
        }

        public override PcgNodeData CollectData()
        {
            return _data;
        }

        public override void SetPropertyValue(string key, object value)
        {
            _data.SetRaw(key, value);
        }
    }
}
