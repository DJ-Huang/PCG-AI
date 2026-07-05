using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    [CustomEditor(typeof(PcgGraphComponent))]
    public sealed class PcgGraphComponentEditor : Editor
    {
        private PcgGraphComponent m_Target;
        private SerializedProperty m_GraphAssetProp;
        private SerializedProperty m_SeedProp;
        private SerializedProperty m_RunOnStartProp;
        private SerializedProperty m_OverridesProp;

        private void OnEnable()
        {
            m_Target = target as PcgGraphComponent;
            m_GraphAssetProp = serializedObject.FindProperty("graphAsset");
            m_SeedProp = serializedObject.FindProperty("seed");
            m_RunOnStartProp = serializedObject.FindProperty("runOnStart");
            m_OverridesProp = serializedObject.FindProperty("m_ParameterOverrides");

            m_Target.RefreshDocument();
            serializedObject.Update();
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUI.BeginChangeCheck();
            EditorGUILayout.PropertyField(m_GraphAssetProp);
            if (EditorGUI.EndChangeCheck())
            {
                serializedObject.ApplyModifiedProperties();
                m_Target.RefreshDocument();
                serializedObject.Update();
            }

            EditorGUILayout.PropertyField(m_SeedProp);
            EditorGUILayout.PropertyField(m_RunOnStartProp);

            EditorGUILayout.Space();

            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Run", GUILayout.Height(28)))
                {
                    serializedObject.ApplyModifiedProperties();
                    m_Target.Run();
                    serializedObject.Update();
                }

                if (GUILayout.Button("Refresh", GUILayout.Height(28)))
                {
                    if (m_GraphAssetProp.objectReferenceValue != null)
                    {
                        var assetPath = AssetDatabase.GetAssetPath(m_GraphAssetProp.objectReferenceValue);
                        if (!string.IsNullOrEmpty(assetPath))
                            AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceUpdate);
                    }
                    serializedObject.ApplyModifiedProperties();
                    m_Target.RefreshDocument();
                    serializedObject.Update();
                }
            }

            DrawParameters();

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawParameters()
        {
            var graphParams = m_Target.GraphParameters;
            if (graphParams == null || graphParams.Count == 0)
            {
                if (m_GraphAssetProp.objectReferenceValue != null)
                {
                    EditorGUILayout.Space();
                    EditorGUILayout.HelpBox(
                        "This graph has no exposed parameters.",
                        MessageType.Info);
                }
                return;
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Parameters", EditorStyles.boldLabel);

            EnsureOverrideCount(graphParams);

            for (var i = 0; i < graphParams.Count; i++)
            {
                var param = graphParams[i];
                if (!param.exposed)
                    continue;

                if (i >= m_OverridesProp.arraySize)
                    break;

                DrawOverrideField(param, m_OverridesProp.GetArrayElementAtIndex(i));
            }
        }

        private void EnsureOverrideCount(List<PcgGraphParameter> parameters)
        {
            var needed = parameters.Count;
            while (m_OverridesProp.arraySize < needed)
                m_OverridesProp.InsertArrayElementAtIndex(m_OverridesProp.arraySize);
            while (m_OverridesProp.arraySize > needed)
                m_OverridesProp.DeleteArrayElementAtIndex(m_OverridesProp.arraySize - 1);

            for (var i = 0; i < parameters.Count; i++)
            {
                var param = parameters[i];
                var element = m_OverridesProp.GetArrayElementAtIndex(i);
                var idProp = element.FindPropertyRelative("parameterId");

                if (idProp.stringValue != param.id)
                {
                    idProp.stringValue = param.id;
                    element.FindPropertyRelative("name").stringValue = param.name;
                    element.FindPropertyRelative("type").stringValue = param.type;

                    var ov = PcgParameterOverride.FromParameter(param);
                    element.FindPropertyRelative("floatValue").floatValue = ov.floatValue;
                    element.FindPropertyRelative("intValue").intValue = ov.intValue;
                    element.FindPropertyRelative("boolValue").boolValue = ov.boolValue;
                    element.FindPropertyRelative("stringValue").stringValue = ov.stringValue;
                }
            }
        }

        private static void DrawOverrideField(PcgGraphParameter param, SerializedProperty element)
        {
            var typeProp = element.FindPropertyRelative("type");

            switch (typeProp.stringValue)
            {
                case "integer":
                    var iProp = element.FindPropertyRelative("intValue");
                    iProp.intValue = EditorGUILayout.IntField(param.name, iProp.intValue);
                    break;
                case "number":
                    var fProp = element.FindPropertyRelative("floatValue");
                    fProp.floatValue = EditorGUILayout.FloatField(param.name, fProp.floatValue);
                    break;
                case "boolean":
                    var bProp = element.FindPropertyRelative("boolValue");
                    bProp.boolValue = EditorGUILayout.Toggle(param.name, bProp.boolValue);
                    break;
                default:
                    var sProp = element.FindPropertyRelative("stringValue");
                    sProp.stringValue = EditorGUILayout.TextField(param.name, sProp.stringValue);
                    break;
            }
        }
    }
}
