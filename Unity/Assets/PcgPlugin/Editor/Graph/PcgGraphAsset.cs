using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    [CreateAssetMenu(fileName = "PcgGraph", menuName = "PCG/Graph Asset", order = 1)]
    public sealed class PcgGraphAsset : ScriptableObject
    {
        [TextArea(8, 24)]
        [SerializeField]
        private string graphJson = "";

        public string GraphJson => graphJson;

        public void SetGraphJson(string json)
        {
            graphJson = json ?? "";
        }
    }
}
