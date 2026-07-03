using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    public sealed class PcgGraphEditorWindow : EditorWindow
    {
        private const string MenuPath = "PCG/Graph Editor";

        private PcgGraphView _graphView;
        private Label _statusLabel;

        [MenuItem(MenuPath)]
        public static void Open()
        {
            var window = GetWindow<PcgGraphEditorWindow>();
            window.titleContent = new GUIContent("PCG Graph");
            window.Show();
        }

        private void OnEnable()
        {
            ConstructToolbar();
            ConstructGraphView();
            LoadDefaultGraph();
        }

        private void ConstructToolbar()
        {
            var toolbar = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    paddingLeft = 6,
                    paddingRight = 6,
                    paddingTop = 4,
                    paddingBottom = 4,
                },
            };

            toolbar.Add(MakeButton("Run", RunGraph));
            toolbar.Add(MakeButton("New", NewGraph));
            toolbar.Add(MakeButton("Import…", ImportGraph));
            toolbar.Add(MakeButton("Export…", ExportGraph));
            toolbar.Add(MakeButton("Load Example", LoadExampleGraph));

            _statusLabel = new Label { style = { marginLeft = 12, unityTextAlign = TextAnchor.MiddleLeft } };
            toolbar.Add(_statusLabel);

            rootVisualElement.Add(toolbar);
        }

        private static Button MakeButton(string text, System.Action onClick)
        {
            var button = new Button(onClick) { text = text };
            button.style.marginRight = 4;
            return button;
        }

        private void ConstructGraphView()
        {
            _graphView = new PcgGraphView();
            rootVisualElement.Add(_graphView);
        }

        private void LoadDefaultGraph()
        {
            _graphView.LoadDocument(PcgGraphDefaults.CreatePipeline());
            SetStatus("Ready — default 3-node pipeline loaded.");
        }

        private void NewGraph()
        {
            if (!EditorUtility.DisplayDialog("New Graph", "Replace the current graph with the default pipeline?", "New", "Cancel"))
                return;

            LoadDefaultGraph();
        }

        private void RunGraph()
        {
            var doc = _graphView.ExportDocument();
            var json = PcgGraphSerializer.ToJson(doc);
            var seed = PcgGraphDefaults.ExtractSeed(doc);

            if (PcgGraphRunner.RunJsonAndUpdatePreview(json, seed))
                SetStatus($"Run OK — seed {seed}, preview updated.");
            else
                SetStatus("Run failed — see Console.");
        }

        private void ImportGraph()
        {
            var path = EditorUtility.OpenFilePanel(
                "Import Graph JSON",
                PcgGraphRunner.DefaultSchemaDir,
                "json");

            if (string.IsNullOrEmpty(path))
                return;

            ImportFromPath(path);
        }

        private void LoadExampleGraph()
        {
            var path = Path.Combine(PcgGraphRunner.DefaultSchemaDir, "example.pcg.json");
            if (!File.Exists(path))
            {
                SetStatus($"Example not found: {path}");
                return;
            }

            ImportFromPath(path);
        }

        private void ImportFromPath(string path)
        {
            var json = File.ReadAllText(path);
            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out var error))
            {
                SetStatus($"Import failed: {error}");
                Debug.LogError($"[PCG] Graph import failed: {error}");
                return;
            }

            _graphView.LoadDocument(doc);
            SetStatus($"Imported: {path}");
        }

        private void ExportGraph()
        {
            var doc = _graphView.ExportDocument();
            var json = PcgGraphSerializer.ToJson(doc);

            var path = EditorUtility.SaveFilePanel(
                "Export Graph JSON",
                PcgGraphRunner.DefaultSchemaDir,
                "graph.pcg.json",
                "json");

            if (string.IsNullOrEmpty(path))
                return;

            File.WriteAllText(path, json);
            SetStatus($"Exported: {path}");
            Debug.Log($"[PCG] Graph exported to {path}");
        }

        private void SetStatus(string message)
        {
            if (_statusLabel != null)
                _statusLabel.text = message;
        }
    }
}
