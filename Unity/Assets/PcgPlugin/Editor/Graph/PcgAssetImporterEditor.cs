using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    [CustomEditor(typeof(PcgAssetImporter))]
    public sealed class PcgAssetImporterEditor : ScriptedImporterEditor
    {
        public override void OnInspectorGUI()
        {
            if (GUILayout.Button("Open in Graph Editor"))
            {
                var path = AssetDatabase.GetAssetPath(((ScriptedImporter)target));
                PcgGraphEditorWindow.ShowGraphEditWindow(path);
            }

            EditorGUILayout.Space();

            ApplyRevertGUI();
        }
    }
}
