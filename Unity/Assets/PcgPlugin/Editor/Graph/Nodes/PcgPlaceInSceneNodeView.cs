using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgPlaceInSceneNodeView : PcgGraphNodeBase
    {
        private TextField _prefabField;
        private FloatField _scaleField;

        public override string NodeType => PcgNodeTypes.PlaceInScene;

        public PcgPlaceInSceneNodeView()
        {
            title = "PlaceInScene";
            var body = new VisualElement();
            _prefabField = new TextField("Prefab") { value = "" };
            _scaleField = new FloatField("Scale") { value = 1f };
            body.Add(_prefabField);
            body.Add(_scaleField);
            extensionContainer.Add(body);
        }

        public override void ApplyData(PcgNodeData data)
        {
            _prefabField.SetValueWithoutNotify(data.prefab ?? "");
            _scaleField.SetValueWithoutNotify(data.scale);
        }

        public override PcgNodeData CollectData()
        {
            return new PcgNodeData
            {
                prefab = _prefabField.value,
                scale = _scaleField.value,
            };
        }
    }
}
