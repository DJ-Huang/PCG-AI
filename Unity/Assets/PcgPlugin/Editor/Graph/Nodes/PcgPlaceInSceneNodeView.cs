using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgPlaceInSceneNodeView : PcgGraphNodeBase
    {
        private PcgNodeData _data = new();

        public override string NodeType => PcgNodeTypes.PlaceInScene;

        public PcgPlaceInSceneNodeView()
        {
            title = "PlaceInScene";
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
