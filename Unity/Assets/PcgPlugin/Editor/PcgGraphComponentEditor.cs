using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using DJTechRuntime.PCG;
using Object = UnityEngine.Object;

namespace DJTechEditor.PCG
{
    [CustomEditor(typeof(PcgGraphComponent))]
    public sealed class PcgGraphComponentEditor : Editor
    {
        private PcgGraphComponent m_Target;
        private SerializedProperty m_GraphAssetProp;
        private SerializedProperty m_SeedProp;
        private SerializedProperty m_CookModeProp;
        private SerializedProperty m_HostOutputModeProp;
        private SerializedProperty m_OverridesProp;
        private SerializedProperty m_MeshBindingsProp;
        private SerializedProperty m_TerrainBindingsProp;
        private SerializedProperty m_MaterialBindingsProp;
        private SerializedProperty m_ScatterPointScaleProp;
        private SerializedProperty m_ScatterPointMeshProp;
        private SerializedProperty m_ScatterDisplayModeProp;
        private SerializedProperty m_EnableAsyncCookInEditorProp;
        private SerializedProperty m_PreferAssetJsonProp;
        private SerializedProperty m_ShowStampOverlaysProp;
        private SerializedProperty m_ShowMaskOverlayProp;
        private SerializedProperty m_MaskOverlayLayerProp;
        private SerializedProperty m_MaskOverlayOpacityProp;
        private SerializedProperty m_StampLiveCookModeProp;
        private bool m_SliderReleasedThisFrame;

        private void OnEnable()
        {
            m_Target = target as PcgGraphComponent;
            m_GraphAssetProp = serializedObject.FindProperty("graphAsset");
            m_SeedProp = serializedObject.FindProperty("seed");
            m_CookModeProp = serializedObject.FindProperty("cookMode");
            m_HostOutputModeProp = serializedObject.FindProperty("hostOutputMode");
            m_OverridesProp = serializedObject.FindProperty("m_ParameterOverrides");
            m_MeshBindingsProp = serializedObject.FindProperty("m_MeshBindings");
            m_TerrainBindingsProp = serializedObject.FindProperty("m_TerrainBindings");
            m_MaterialBindingsProp = serializedObject.FindProperty("m_MaterialBindings");
            m_ScatterPointScaleProp = serializedObject.FindProperty("scatterPointScale");
            m_ScatterPointMeshProp = serializedObject.FindProperty("scatterPointMesh");
            m_ScatterDisplayModeProp = serializedObject.FindProperty("scatterDisplayMode");
            m_EnableAsyncCookInEditorProp = serializedObject.FindProperty("enableAsyncCookInEditor");
            m_PreferAssetJsonProp = serializedObject.FindProperty("preferAssetJson");
            m_ShowStampOverlaysProp = serializedObject.FindProperty("showStampOverlays");
            m_ShowMaskOverlayProp = serializedObject.FindProperty("showMaskOverlay");
            m_MaskOverlayLayerProp = serializedObject.FindProperty("maskOverlayLayer");
            m_MaskOverlayOpacityProp = serializedObject.FindProperty("maskOverlayOpacity");
            m_StampLiveCookModeProp = serializedObject.FindProperty("stampLiveCookMode");

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
            if (m_HostOutputModeProp != null)
            {
                EditorGUILayout.PropertyField(
                    m_HostOutputModeProp,
                    new GUIContent(
                        "Host Output",
                        "Mesh: apply cook result to MeshFilter/MeshRenderer. " +
                        "Terrain: write typed HeightField Output to bound TerrainData only."));
            }
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
            if (!Application.isPlaying && m_EnableAsyncCookInEditorProp != null)
            {
                EditorGUILayout.PropertyField(
                    m_EnableAsyncCookInEditorProp,
                    new GUIContent("Async Cook In Editor", "Run cook in background thread; Esc cancels current cook."));
                if (m_PreferAssetJsonProp != null)
                {
                    EditorGUILayout.PropertyField(
                        m_PreferAssetJsonProp,
                        new GUIContent(
                            "Prefer Asset JSON",
                            "Full-graph cook uses on-disk .pcg even if Graph Editor is open (Agent/MCP reviews). " +
                            "Turn off while authoring so Inspector/Graph edits recook. " +
                            "Node Preview always uses the live Graph Editor document."));
                }
            }
            if (m_ShowStampOverlaysProp != null)
            {
                EditorGUILayout.PropertyField(
                    m_ShowStampOverlaysProp,
                    new GUIContent(
                        "Show Stamp Overlays",
                        "Draw MaskByObject stamp volumes in Scene View (wire boxes)."));
            }

            var isTerrainHost = m_HostOutputModeProp != null &&
                (PcgHostOutputMode)m_HostOutputModeProp.enumValueIndex == PcgHostOutputMode.Terrain;
            if (isTerrainHost && m_StampLiveCookModeProp != null)
            {
                EditorGUILayout.PropertyField(
                    m_StampLiveCookModeProp,
                    new GUIContent(
                        "Stamp Live Cook",
                        "When Scene stamp gizmos move: Manual = no cook; OnRelease = cook on mouse up; WhileDragging = debounced cook."));
            }

            if (EditorGUI.EndChangeCheck())
            {
                serializedObject.ApplyModifiedProperties();
                if (m_Target.HostOutputMode == PcgHostOutputMode.Terrain)
                    m_Target.SetHostOutputMode(PcgHostOutputMode.Terrain, requestCook: false);
                if (m_Target.SupportsEditModePreview())
                    m_Target.RequestPreviewCook(immediate: true);
                serializedObject.Update();
            }

            if (isTerrainHost)
            {
                EditorGUI.BeginChangeCheck();
                if (m_ShowMaskOverlayProp != null)
                {
                    EditorGUILayout.PropertyField(
                        m_ShowMaskOverlayProp,
                        new GUIContent(
                            "Show Mask Overlay",
                            "Houdini-style red tint of the cooked HeightField mask layer on Terrain. Only drawn while Enter PCG Mode is active."));
                }

                if (m_ShowMaskOverlayProp != null && m_ShowMaskOverlayProp.boolValue)
                {
                    if (m_MaskOverlayLayerProp != null)
                    {
                        EditorGUILayout.PropertyField(
                            m_MaskOverlayLayerProp,
                            new GUIContent(
                                "Mask Overlay Layer",
                                "Named scalar layer to tint (default mask). Use HeightFieldIsolateLayer to copy mesa/cliffs into mask."));
                    }

                    if (m_MaskOverlayOpacityProp != null)
                    {
                        EditorGUILayout.PropertyField(
                            m_MaskOverlayOpacityProp,
                            new GUIContent(
                                "Mask Overlay Opacity",
                                "Red tint strength (0–1)."));
                    }
                }

                if (EditorGUI.EndChangeCheck())
                {
                    serializedObject.ApplyModifiedProperties();
                    SceneView.RepaintAll();
                }
            }

            DrawCookModeHelp();
            DrawAsyncCookStatus();

            EditorGUILayout.Space();

            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Run", GUILayout.Height(28)))
                {
                    serializedObject.ApplyModifiedProperties();
                    PcgCookBackend.ClearCookCache();
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

            if (!Application.isPlaying && GUILayout.Button("Cancel Preview Cook (Esc)"))
                PcgGraphComponent.CancelAllEditModeAsyncCooks();

            DrawScatterSettings();
            DrawHostBindings();
            DrawMaterialBindings();
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

        private void DrawScatterSettings()
        {
            var mode = m_HostOutputModeProp != null
                ? (PcgHostOutputMode)m_HostOutputModeProp.enumValueIndex
                : PcgHostOutputMode.Mesh;
            if (mode == PcgHostOutputMode.Terrain)
                return;

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Scatter Rendering", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();

            if (m_ScatterDisplayModeProp != null)
            {
                EditorGUILayout.PropertyField(
                    m_ScatterDisplayModeProp,
                    new GUIContent(
                        "Display Mode",
                        "Merged Mesh: combine instances into one MeshFilter (default). " +
                        "GPU Instancing: draw with Graphics.RenderMeshIndirect " +
                        "(one batch per non-empty prototype material/submesh slot)."));
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "Scatter Display Mode is unavailable until scripts recompile. " +
                    "Use Graph Editor toolbar «Scatter Display» or PCG → Settings.",
                    MessageType.Warning);
            }

            EditorGUILayout.PropertyField(
                m_ScatterPointScaleProp,
                new GUIContent("Point Scale", "Scale of each generated scatter instance mesh."));
            EditorGUILayout.PropertyField(
                m_ScatterPointMeshProp,
                new GUIContent("Point Mesh", "Mesh used for each point instance. Empty = built-in Sphere/Cube fallback."));
            if (EditorGUI.EndChangeCheck())
            {
                serializedObject.ApplyModifiedProperties();
                if (m_Target.SupportsEditModePreview())
                    m_Target.RequestPreviewCook(immediate: true);
                serializedObject.Update();
            }
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
                        "Manual only — use Run or Graph Editor.",
                        MessageType.Info);
                    break;
            }
        }

        private void DrawAsyncCookStatus()
        {
            if (Application.isPlaying || m_Target == null)
                return;

            var status = m_Target.LastAsyncCookStatus;
            if (string.IsNullOrEmpty(status))
                status = "idle";

            var type = MessageType.None;
            if (m_Target.IsAsyncCookInProgress || status == "running")
                type = MessageType.Info;
            else if (status == "failed")
                type = MessageType.Error;
            else if (status == "cancelled")
                type = MessageType.Warning;

            EditorGUILayout.HelpBox($"Async cook status: {status}", type);
        }

        private void DrawHostBindings()
        {
            var mode = m_HostOutputModeProp != null
                ? (PcgHostOutputMode)m_HostOutputModeProp.enumValueIndex
                : PcgHostOutputMode.Mesh;

            if (mode == PcgHostOutputMode.Terrain)
            {
                EditorGUILayout.Space();
                EditorGUILayout.HelpBox(
                    "Host Output = Terrain: attach this component to the Terrain GameObject. " +
                    "Cook writes HeightField → this object's TerrainData (no Terrain Binding).",
                    MessageType.Info);
                if (m_Target != null &&
                    PcgTerrainBindingTable.ResolveSelfTerrain(m_Target.gameObject) == null)
                {
                    EditorGUILayout.HelpBox(
                        "No Terrain on this GameObject. Move PcgGraphComponent onto the Terrain.",
                        MessageType.Warning);
                }
                DrawMeshBindings(collapsed: true);
            }
            else
            {
                DrawMeshBindings(collapsed: false);
                // Legacy override only when GetTerrainData is present and the component
                // is not already on a Terrain.
                if (GraphHasGetTerrainData() &&
                    (m_Target == null ||
                     PcgTerrainBindingTable.ResolveSelfTerrain(m_Target.gameObject) == null))
                {
                    DrawTerrainBindingsLegacy();
                }
            }
        }

        private void DrawMeshBindings(bool collapsed)
        {
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Mesh Bindings", EditorStyles.boldLabel);

            if (collapsed)
            {
                EditorGUILayout.HelpBox(
                    "Host Output = Terrain: Mesh Bindings are unused.",
                    MessageType.Info);
                return;
            }

            if (GraphHasGetMeshData())
            {
                EditorGUILayout.HelpBox(
                    "GetMeshData needs a mesh source. Add a binding:\n" +
                    "bindingKey = targetMesh · Source = Scene Object · Scene Object = your mesh in hierarchy.",
                    MessageType.Info);

                if (m_MeshBindingsProp.arraySize == 0)
                {
                    EditorGUILayout.HelpBox(
                        "No Mesh Bindings configured — cook will fail until you add one.",
                        MessageType.Warning);
                }
            }

            EditorGUILayout.PropertyField(m_MeshBindingsProp, includeChildren: true);
        }

        private void DrawTerrainBindingsLegacy()
        {
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Terrain Import (legacy)", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                "Preferred: put PcgGraphComponent on the Terrain so GetTerrainData imports Self. " +
                "This list is only for importing a remote Terrain while Host Output = Mesh.",
                MessageType.Info);

            EditorGUI.BeginChangeCheck();
            EditorGUILayout.PropertyField(m_TerrainBindingsProp, includeChildren: true);
            if (EditorGUI.EndChangeCheck())
            {
                serializedObject.ApplyModifiedProperties();
                if (m_Target.SupportsEditModePreview())
                    m_Target.RequestPreviewCook(immediate: true);
                serializedObject.Update();
            }
        }

        private void DrawMaterialBindings()
        {
            var mode = m_HostOutputModeProp != null
                ? (PcgHostOutputMode)m_HostOutputModeProp.enumValueIndex
                : PcgHostOutputMode.Mesh;
            if (mode == PcgHostOutputMode.Terrain)
                return;

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Material Bindings", EditorStyles.boldLabel);

            EditorGUI.BeginChangeCheck();

            var meshMaterialProp = serializedObject.FindProperty("meshMaterial");
            if (meshMaterialProp != null)
            {
                EditorGUILayout.PropertyField(
                    meshMaterialProp,
                    new GUIContent(
                        "Fallback Material",
                        "Used when a slot has no binding or an empty material name."));
            }

            var names = CollectGraphMaterialNames();
            SyncMaterialBindings(names);

            if (names.Count == 0 && m_MaterialBindingsProp.arraySize == 0)
            {
                if (EditorGUI.EndChangeCheck())
                    ApplyMaterialBindingChanges();
                EditorGUILayout.HelpBox(
                    "No AssignMaterial names in this graph. Add AssignMaterial nodes " +
                    "(or Run once so cook slots appear), then assign Unity Materials here.",
                    MessageType.Info);
                return;
            }

            EditorGUILayout.HelpBox(
                "Each row is an AssignMaterial name from the graph (or last cook). " +
                "Assign a Unity Material — changes apply immediately without re-running. " +
                "Unmapped slots use Fallback Material.",
                MessageType.Info);

            for (var i = 0; i < m_MaterialBindingsProp.arraySize; i++)
            {
                var element = m_MaterialBindingsProp.GetArrayElementAtIndex(i);
                var nameProp = element.FindPropertyRelative("materialName");
                var materialProp = element.FindPropertyRelative("material");
                var name = nameProp.stringValue;
                var inGraph = names.Contains(name);

                EditorGUILayout.BeginHorizontal();
                if (inGraph)
                {
                    EditorGUILayout.PrefixLabel(name);
                }
                else
                {
                    var options = BuildMaterialNamePopupOptions(names, name);
                    var current = Mathf.Max(0, System.Array.IndexOf(options, name));
                    var next = EditorGUILayout.Popup("Name", current, options);
                    if (next >= 0 && next < options.Length && options[next] != name)
                        nameProp.stringValue = options[next];
                }

                EditorGUILayout.PropertyField(materialProp, GUIContent.none);
                if (!inGraph && GUILayout.Button("×", GUILayout.Width(22)))
                {
                    m_MaterialBindingsProp.DeleteArrayElementAtIndex(i);
                    EditorGUILayout.EndHorizontal();
                    break;
                }

                EditorGUILayout.EndHorizontal();
            }

            if (EditorGUI.EndChangeCheck())
                ApplyMaterialBindingChanges();
        }

        private void ApplyMaterialBindingChanges()
        {
            serializedObject.ApplyModifiedProperties();
            m_Target.RefreshAppliedMaterials();
            SceneView.RepaintAll();
        }

        private List<string> CollectGraphMaterialNames()
        {
            var names = new List<string>();
            var seen = new HashSet<string>();

            void Add(string name)
            {
                if (string.IsNullOrEmpty(name) || !seen.Add(name))
                    return;
                names.Add(name);
            }

            void CollectFromNodes(IEnumerable<PcgGraphNodeRecord> nodes)
            {
                if (nodes == null)
                    return;
                foreach (var node in nodes)
                {
                    if (node?.type != "AssignMaterial")
                        continue;
                    Add(node.data?.GetRaw("materialName")?.ToString());
                }
            }

            var doc = m_Target.Document;
            if (doc != null)
            {
                CollectFromNodes(doc.nodes);
                if (doc.subgraphs != null)
                {
                    foreach (var subgraph in doc.subgraphs)
                        CollectFromNodes(subgraph?.nodes);
                }

                if (doc.parameters != null && m_Target.ParameterOverrides != null)
                {
                    var overridesById = new Dictionary<string, PcgParameterOverride>();
                    foreach (var ov in m_Target.ParameterOverrides)
                    {
                        if (ov != null && !string.IsNullOrEmpty(ov.parameterId))
                            overridesById[ov.parameterId] = ov;
                    }

                    foreach (var param in doc.parameters)
                    {
                        if (param == null || param.targetProperty != "materialName")
                            continue;
                        if (overridesById.TryGetValue(param.id, out var ov))
                            Add(ov.GetValue()?.ToString());
                        else
                            Add(param.defaultValue);
                    }
                }
            }

            var lastCook = m_Target.LastMaterialNames;
            if (lastCook != null)
            {
                foreach (var name in lastCook)
                    Add(name);
            }

            names.Sort(System.StringComparer.Ordinal);
            return names;
        }

        private void SyncMaterialBindings(List<string> names)
        {
            var materialByName = new Dictionary<string, Object>();
            for (var i = 0; i < m_MaterialBindingsProp.arraySize; i++)
            {
                var element = m_MaterialBindingsProp.GetArrayElementAtIndex(i);
                var name = element.FindPropertyRelative("materialName").stringValue;
                if (string.IsNullOrEmpty(name) || materialByName.ContainsKey(name))
                    continue;
                materialByName[name] = element.FindPropertyRelative("material").objectReferenceValue;
            }

            // Keep graph/cook names first, then orphaned bindings that still have a Material.
            var ordered = new List<string>(names);
            for (var i = 0; i < m_MaterialBindingsProp.arraySize; i++)
            {
                var element = m_MaterialBindingsProp.GetArrayElementAtIndex(i);
                var name = element.FindPropertyRelative("materialName").stringValue;
                if (string.IsNullOrEmpty(name) || names.Contains(name))
                    continue;
                if (element.FindPropertyRelative("material").objectReferenceValue == null)
                    continue;
                if (!ordered.Contains(name))
                    ordered.Add(name);
            }

            var needsSync = m_MaterialBindingsProp.arraySize != ordered.Count;
            if (!needsSync)
            {
                for (var i = 0; i < ordered.Count; i++)
                {
                    var element = m_MaterialBindingsProp.GetArrayElementAtIndex(i);
                    if (element.FindPropertyRelative("materialName").stringValue != ordered[i])
                    {
                        needsSync = true;
                        break;
                    }
                }
            }

            if (!needsSync)
                return;

            while (m_MaterialBindingsProp.arraySize < ordered.Count)
                m_MaterialBindingsProp.InsertArrayElementAtIndex(m_MaterialBindingsProp.arraySize);
            while (m_MaterialBindingsProp.arraySize > ordered.Count)
                m_MaterialBindingsProp.DeleteArrayElementAtIndex(m_MaterialBindingsProp.arraySize - 1);

            for (var i = 0; i < ordered.Count; i++)
            {
                var element = m_MaterialBindingsProp.GetArrayElementAtIndex(i);
                var name = ordered[i];
                element.FindPropertyRelative("materialName").stringValue = name;
                materialByName.TryGetValue(name, out var material);
                element.FindPropertyRelative("material").objectReferenceValue = material;
            }
        }

        private static string[] BuildMaterialNamePopupOptions(List<string> names, string current)
        {
            var options = new List<string>(names.Count + 1);
            options.AddRange(names);
            if (!string.IsNullOrEmpty(current) && !names.Contains(current))
                options.Insert(0, current);
            if (options.Count == 0)
                options.Add(string.IsNullOrEmpty(current) ? "(none)" : current);
            return options.ToArray();
        }

        private bool GraphHasGetMeshData()
        {
            var doc = m_Target.Document;
            if (doc?.nodes == null)
                return false;

            foreach (var node in doc.nodes)
            {
                if (node?.type == "GetMeshData")
                    return true;
            }

            return false;
        }

        private bool GraphHasGetTerrainData()
        {
            var doc = m_Target.Document;
            if (doc?.nodes == null)
                return false;

            foreach (var node in doc.nodes)
            {
                if (node?.type == "GetTerrainData")
                    return true;
            }

            return false;
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
