using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgSpawnPointsNodeView : PcgGraphNodeBase
    {
        private IntegerField _countField;
        private FloatField _radiusField;

        public override string NodeType => PcgNodeTypes.SpawnPoints;

        public PcgSpawnPointsNodeView()
        {
            title = "SpawnPoints";
            var body = new VisualElement();
            _countField = new IntegerField("Count") { value = 100 };
            _radiusField = new FloatField("Radius") { value = 10f };
            body.Add(_countField);
            body.Add(_radiusField);
            extensionContainer.Add(body);
        }

        public override void ApplyData(PcgNodeData data)
        {
            _countField.SetValueWithoutNotify(data.count);
            _radiusField.SetValueWithoutNotify(data.radius);
        }

        public override PcgNodeData CollectData()
        {
            return new PcgNodeData
            {
                count = _countField.value,
                radius = _radiusField.value,
            };
        }
    }
}
