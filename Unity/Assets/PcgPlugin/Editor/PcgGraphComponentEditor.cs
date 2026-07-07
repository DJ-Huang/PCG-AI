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
        private SerializedProperty m_CookModeProp;
        private SerializedProperty m_OverridesProp;
        private SerializedProperty m_MeshBindingsProp;
        private bool m_SliderReleasedThisFrame;

        private void OnEnable()
        {
            m_Target = target as PcgGraphComponent;
            m_GraphAssetProp = serializedObject.FindProperty("graphAsset");
            m_SeedProp = serializedObject.FindProperty("seed");
            m_CookModeProp = serializedObject.FindProperty("cookMode");
            m_OverridesProp = serializedObject.FindProperty("m_ParameterOverrides");
            m_MeshBindingsProp = serializedObject.FindProperty("m_MeshBindings");

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

            EditorGUI.BeginChangeCheck();
            EditorGUILayout.PropertyField(m_SeedProp);
            EditorGUILayout.PropertyField(m_CookModeProp);
            var cookMode = (PcgCookMode)m_CookModeProp.enumValueIndex;
            var intervalProp = serializedObject.FindProperty("editModeCookInterval");
            if (cookMode == PcgCookMode.OnParameterChange)
                EditorGUILayout.PropertyField(intervalProp);
            else if (cookMode == PcgCookMode.EveryFrame && !Application.isPlaying)
                EditorGUILayout.PropertyField(
                    intervalProp,
                    new GUIContent(
                        "Edit Mode Debounce",
                        "EveryFrame is downgraded in Edit Mode; debounce interval for parameter preview."));
            if (EditorGUI.EndChangeCheck())
            {
                serializedObject.ApplyModifiedProperties();
                if (m_Target.SupportsEditModePreview())
                    m_Target.RequestPreviewCook(immediate: true);
                serializedObject.Update();
            }

            DrawCookModeHelp();

            EditorGUILayout.Space();

            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Run (Full)", GUILayout.Height(28)))
                {
                    serializedObject.ApplyModifiedProperties();
                    PcgNative.ClearCookCache();
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

            DrawMeshBindings();
            DrawParameters();

            if (m_SliderReleasedThisFrame && m_Target.SupportsEditModePreview())
            {
                serializedObject.ApplyModifiedProperties();
                m_Target.RequestPreviewCook(immediate: true);
                m_SliderReleasedThisFrame = false;
                serializedObject.Update();
            }

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawCookModeHelp()
        {
            if (Application.isPlaying)
                return;

            switch ((PcgCookMode)m_CookModeProp.enumValueIndex)
            {
                case PcgCookMode.EveryFrame:
                    EditorGUILayout.HelpBox(
                        Application.isPlaying
                            ? "Play Mode: cooks every frame."
                            : "EveryFrame applies in Play Mode only. Edit Mode preview is downgraded to On Parameter Change (debounced).",
                        Application.isPlaying ? MessageType.Info : MessageType.Warning);
                    break;
                case PcgCookMode.OnParameterChange:
                    EditorGUILayout.HelpBox(
                        "Debounced preview after parameter changes.",
                        MessageType.Info);
                    break;
                case PcgCookMode.Manual:
                    EditorGUILayout.HelpBox(
                        "Manual only — use Run (Full) or Graph Editor.",
                        MessageType.Info);
                    break;
            }
        }

        private void DrawMeshBindings()
        {
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Mesh Bindings", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(m_MeshBindingsProp, includeChildren: true);
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

            EditorGUI.BeginChangeCheck();
            for (var i = 0; i < graphParams.Count; i++)
            {
                var param = graphParams[i];
                if (!param.exposed)
                    continue;

                if (i >= m_OverridesProp.arraySize)
                    break;

                DrawOverrideField(param, m_OverridesProp.GetArrayElementAtIndex(i));
            }

            if (EditorGUI.EndChangeCheck())
            {
                serializedObject.ApplyModifiedProperties();
                if (m_Target.SupportsEditModePreview())
                    m_Target.RequestPreviewCook();
                serializedObject.Update();
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

        private void DrawOverrideField(PcgGraphParameter param, SerializedProperty element)
        {
            var typeProp = element.FindPropertyRelative("type");

            switch (typeProp.stringValue)
            {
                case "integer":
                    var iProp = element.FindPropertyRelative("intValue");
                    if (param.hasRange)
                    {
                        EditorGUILayout.BeginHorizontal();
                        EditorGUILayout.PrefixLabel(param.name);
                        iProp.intValue = Mathf.RoundToInt(GUILayout.HorizontalSlider(
                            iProp.intValue, param.minValue, param.maxValue));
                        iProp.intValue = EditorGUILayout.IntField(
                            iProp.intValue, GUILayout.Width(50));
                        EditorGUILayout.EndHorizontal();
                        if (IsSliderMouseUp())
                            m_SliderReleasedThisFrame = true;
                    }
                    else
                        iProp.intValue = EditorGUILayout.IntField(param.name, iProp.intValue);
                    break;
                case "number":
                    var fProp = element.FindPropertyRelative("floatValue");
                    if (param.hasRange)
                    {
                        EditorGUILayout.BeginHorizontal();
                        EditorGUILayout.PrefixLabel(param.name);
                        fProp.floatValue = GUILayout.HorizontalSlider(
                            fProp.floatValue, param.minValue, param.maxValue);
                        fProp.floatValue = EditorGUILayout.FloatField(
                            fProp.floatValue, GUILayout.Width(50));
                        EditorGUILayout.EndHorizontal();
                        if (IsSliderMouseUp())
                            m_SliderReleasedThisFrame = true;
                    }
                    else
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

        private static bool IsSliderMouseUp()
        {
            var evt = Event.current;
            return evt != null &&
                   (evt.type == EventType.MouseUp || evt.type == EventType.DragExited) &&
                   evt.button == 0;
        }
    }
}
