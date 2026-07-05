using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Inspector for PcgGraphAsset — shows "Open in Graph Editor" button
    /// and renders custom icon preview via RenderStaticPreview.
    /// </summary>
    [CustomEditor(typeof(PcgGraphAsset))]
    public sealed class PcgGraphAssetEditor : Editor
    {
        private PcgGraphAsset m_Asset;

        private void OnEnable()
        {
            m_Asset = target as PcgGraphAsset;
        }

        public override void OnInspectorGUI()
        {
            EditorGUILayout.Space();

            if (GUILayout.Button("Open in Graph Editor", GUILayout.Height(30)))
            {
                var path = AssetDatabase.GetAssetPath(m_Asset);
                PcgGraphEditorWindow.OpenWithFile(path);
            }

            EditorGUILayout.Space();

            DrawDefaultInspector();
        }
    }
}
