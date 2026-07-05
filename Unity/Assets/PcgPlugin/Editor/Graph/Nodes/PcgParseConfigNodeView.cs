using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgParseConfigNodeView : PcgGraphNodeBase
    {
        private IntegerField _seedField;
        private FloatField _densityField;

        public override string NodeType => PcgNodeTypes.ParseConfig;

        public PcgParseConfigNodeView()
        {
            title = "ParseConfig";
            var body = new VisualElement();
            _seedField = new IntegerField("Seed") { value = 42 };
            _densityField = new FloatField("Density") { value = 0.5f };
            body.Add(_seedField);
            body.Add(_densityField);
            extensionContainer.Add(body);
        }

        public override void ApplyData(PcgNodeData data)
        {
            _seedField.SetValueWithoutNotify(data.seed);
            _densityField.SetValueWithoutNotify(data.density);
        }

        public override PcgNodeData CollectData()
        {
            return new PcgNodeData
            {
                seed = _seedField.value,
                density = _densityField.value,
            };
        }
    }
}
