using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    // The default inspector renders graphJson through a multiline UIToolkit TextField.
    // A single TextElement allocates 4 vertices per glyph, so any graph larger than
    // ~16k chars overflows UIR's 65535-vertex-per-element limit ("A VisualElement must
    // not allocate more than 65535 vertices") whenever the inspector paints the field.
    // This editor keeps everything in IMGUI and only shows a truncated preview.
    [CustomEditor(typeof(PcgGraphAsset))]
    public sealed class PcgGraphAssetEditor : Editor
    {
        private const int PreviewCharLimit = 2000;

        private SerializedProperty m_GraphJsonProp;
        private SerializedProperty m_ImportErrorProp;
        private SerializedProperty m_ImportSucceededProp;
        private SerializedProperty m_DependencyGuidsProp;
        private Vector2 m_PreviewScroll;

        private void OnEnable()
        {
            m_GraphJsonProp = serializedObject.FindProperty("graphJson");
            m_ImportErrorProp = serializedObject.FindProperty("importError");
            m_ImportSucceededProp = serializedObject.FindProperty("importSucceeded");
            m_DependencyGuidsProp = serializedObject.FindProperty("dependencyGuids");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.PropertyField(m_ImportSucceededProp, new GUIContent("Import Succeeded"));

            if (!m_ImportSucceededProp.boolValue)
            {
                EditorGUILayout.PropertyField(m_ImportErrorProp, new GUIContent("Import Error"));
            }

            var json = m_GraphJsonProp.stringValue ?? "";
            EditorGUILayout.LabelField(
                "Graph JSON Size",
                $"{json.Length:N0} chars ({json.Length / 1024f:F1} KB)");
            EditorGUILayout.LabelField(
                "Dependencies",
                m_DependencyGuidsProp.arraySize.ToString());

            var assetPath = AssetDatabase.GetAssetPath(target);
            using (new EditorGUI.DisabledScope(string.IsNullOrEmpty(assetPath)))
            {
                if (GUILayout.Button("Open in Graph Editor"))
                {
                    Graph.PcgGraphEditorWindow.OpenWithFile(assetPath);
                }
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("JSON Preview (truncated)", EditorStyles.boldLabel);
            var preview = json.Length <= PreviewCharLimit
                ? json
                : json.Substring(0, PreviewCharLimit) +
                  $"\n… [{json.Length - PreviewCharLimit:N0} more chars not shown]";
            m_PreviewScroll = EditorGUILayout.BeginScrollView(
                m_PreviewScroll, GUILayout.MaxHeight(220));
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.TextArea(preview, GUILayout.ExpandHeight(true));
            }
            EditorGUILayout.EndScrollView();

            serializedObject.ApplyModifiedProperties();
        }
    }
}
