using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG
{
    [CustomEditor(typeof(PcgRuntimeRunner))]
    public sealed class PcgRuntimeRunnerEditor : Editor
    {
        private PcgRuntimeRunner m_Target;
        private List<PcgGraphParameter> m_GraphParameters;

        private void OnEnable()
        {
            m_Target = target as PcgRuntimeRunner;
            RefreshParameters();
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.PropertyField(serializedObject.FindProperty("graphFileName"));
            EditorGUILayout.PropertyField(serializedObject.FindProperty("seed"));
            EditorGUILayout.PropertyField(serializedObject.FindProperty("runOnStart"));

            if (GUILayout.Button("Refresh Parameters"))
                RefreshParameters();

            if (m_GraphParameters != null && m_GraphParameters.Count > 0)
            {
                EditorGUILayout.Space();
                EditorGUILayout.LabelField("Exposed Parameters", EditorStyles.boldLabel);

                var overridesProp = serializedObject.FindProperty("m_ParameterOverrides");
                EnsureOverrideCount(overridesProp);

                for (var i = 0; i < m_GraphParameters.Count; i++)
                {
                    var param = m_GraphParameters[i];
                    if (!param.exposed)
                        continue;

                    if (i >= overridesProp.arraySize)
                        break;

                    var element = overridesProp.GetArrayElementAtIndex(i);
                    DrawOverrideField(param, element);
                }
            }
            else
            {
                EditorGUILayout.Space();
                EditorGUILayout.HelpBox(
                    m_GraphParameters == null
                        ? "Graph file not found or has no parameters."
                        : "This graph has no parameters.",
                    MessageType.Info);
            }

            serializedObject.ApplyModifiedProperties();
        }

        private void RefreshParameters()
        {
            var graphFileName = serializedObject.FindProperty("graphFileName").stringValue;
            var path = Path.Combine(Application.streamingAssetsPath, "pcg", graphFileName);
            if (!File.Exists(path))
            {
                m_GraphParameters = null;
                return;
            }

            var json = File.ReadAllText(path);
            if (PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                m_GraphParameters = doc.parameters;
            else
                m_GraphParameters = null;
        }

        private void EnsureOverrideCount(SerializedProperty overridesProp)
        {
            if (m_GraphParameters == null)
                return;

            var previous = new Dictionary<string, (float f, int i, bool b, string s)>();
            for (var i = 0; i < overridesProp.arraySize; i++)
            {
                var element = overridesProp.GetArrayElementAtIndex(i);
                var id = element.FindPropertyRelative("parameterId").stringValue;
                if (string.IsNullOrEmpty(id) || previous.ContainsKey(id))
                    continue;
                previous[id] = (
                    element.FindPropertyRelative("floatValue").floatValue,
                    element.FindPropertyRelative("intValue").intValue,
                    element.FindPropertyRelative("boolValue").boolValue,
                    element.FindPropertyRelative("stringValue").stringValue);
            }

            var needed = m_GraphParameters.Count;
            while (overridesProp.arraySize < needed)
                overridesProp.InsertArrayElementAtIndex(overridesProp.arraySize);
            while (overridesProp.arraySize > needed)
                overridesProp.DeleteArrayElementAtIndex(overridesProp.arraySize - 1);

            for (var i = 0; i < m_GraphParameters.Count; i++)
            {
                var param = m_GraphParameters[i];
                var element = overridesProp.GetArrayElementAtIndex(i);
                var idProp = element.FindPropertyRelative("parameterId");
                var nameProp = element.FindPropertyRelative("name");
                var typeProp = element.FindPropertyRelative("type");

                idProp.stringValue = param.id;
                nameProp.stringValue = param.name;
                typeProp.stringValue = param.type;

                var fProp = element.FindPropertyRelative("floatValue");
                var iProp = element.FindPropertyRelative("intValue");
                var bProp = element.FindPropertyRelative("boolValue");
                var sProp = element.FindPropertyRelative("stringValue");

                if (previous.TryGetValue(param.id, out var kept))
                {
                    fProp.floatValue = kept.f;
                    iProp.intValue = kept.i;
                    bProp.boolValue = kept.b;
                    sProp.stringValue = kept.s;
                }
                else
                {
                    var overrideVal = PcgParameterOverride.FromParameter(param);
                    fProp.floatValue = overrideVal.floatValue;
                    iProp.intValue = overrideVal.intValue;
                    bProp.boolValue = overrideVal.boolValue;
                    sProp.stringValue = overrideVal.stringValue;
                }
            }
        }

        private static void DrawOverrideField(PcgGraphParameter param, SerializedProperty element)
        {
            var nameProp = element.FindPropertyRelative("name");
            var typeProp = element.FindPropertyRelative("type");

            EditorGUILayout.LabelField(nameProp.stringValue, typeProp.stringValue);

            switch (typeProp.stringValue)
            {
                case "integer":
                    var iProp = element.FindPropertyRelative("intValue");
                    iProp.intValue = EditorGUILayout.IntField("  Value", iProp.intValue);
                    break;
                case "number":
                    var fProp = element.FindPropertyRelative("floatValue");
                    fProp.floatValue = EditorGUILayout.FloatField("  Value", fProp.floatValue);
                    break;
                case "boolean":
                    var bProp = element.FindPropertyRelative("boolValue");
                    bProp.boolValue = EditorGUILayout.Toggle("  Value", bProp.boolValue);
                    break;
                default:
                    var sProp = element.FindPropertyRelative("stringValue");
                    sProp.stringValue = EditorGUILayout.TextField("  Value", sProp.stringValue);
                    break;
            }
        }
    }
}
