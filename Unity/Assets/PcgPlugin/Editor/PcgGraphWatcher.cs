using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG
{
    /// <summary>
    /// Watches a graph JSON file and reloads on change or manual trigger.
    /// </summary>
    [InitializeOnLoad]
    public static class PcgGraphWatcher
    {
        private const string MenuRoot = "PCG/";
        private const string PrefsWatchedPath = "PCG.WatchedGraphPath";
        private const string PrefsAutoReload = "PCG.AutoReloadGraph";
        private const string PrefsSeed = "PCG.WatchedGraphSeed";

        private static FileSystemWatcher _watcher;
        private static bool _pendingReload;
        private static DateTime _lastWriteUtc = DateTime.MinValue;

        static PcgGraphWatcher()
        {
            EditorApplication.update += OnEditorUpdate;
            SetupWatcher();
        }

        public static string WatchedPath
        {
            get
            {
                var stored = EditorPrefs.GetString(PrefsWatchedPath, string.Empty);
                return string.IsNullOrEmpty(stored) ? PcgGraphRunner.DefaultWatchedGraphPath : stored;
            }
            set
            {
                EditorPrefs.SetString(PrefsWatchedPath, value);
                SetupWatcher();
            }
        }

        public static bool AutoReload
        {
            get => EditorPrefs.GetBool(PrefsAutoReload, true);
            set
            {
                EditorPrefs.SetBool(PrefsAutoReload, value);
                SetupWatcher();
            }
        }

        public static int Seed
        {
            get => EditorPrefs.GetInt(PrefsSeed, 42);
            set => EditorPrefs.SetInt(PrefsSeed, value);
        }

        [MenuItem(MenuRoot + "Set Watched Graph…")]
        public static void SetWatchedGraph()
        {
            var path = EditorUtility.OpenFilePanel(
                "Select Watched Graph JSON",
                PcgGraphRunner.DefaultSchemaDir,
                "json");

            if (string.IsNullOrEmpty(path))
                return;

            WatchedPath = path;
            Debug.Log($"[PCG] Watching graph: {WatchedPath}");
        }

        [MenuItem(MenuRoot + "Reload Watched Graph")]
        public static void ReloadWatchedGraph()
        {
            PcgGraphRunner.RunFileAndUpdatePreview(WatchedPath, Seed);
        }

        [MenuItem(MenuRoot + "Reload Watched Graph", true)]
        private static bool ReloadWatchedGraphValidate()
        {
            return File.Exists(WatchedPath);
        }

        private static void SetupWatcher()
        {
            DisposeWatcher();

            var path = WatchedPath;
            if (!AutoReload || string.IsNullOrEmpty(path) || !File.Exists(path))
                return;

            var dir = Path.GetDirectoryName(path);
            var file = Path.GetFileName(path);
            if (string.IsNullOrEmpty(dir) || string.IsNullOrEmpty(file))
                return;

            _watcher = new FileSystemWatcher(dir, file)
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName,
                EnableRaisingEvents = true,
            };

            _watcher.Changed += OnFileChanged;
            _watcher.Created += OnFileChanged;
            _watcher.Renamed += OnFileChanged;
        }

        private static void OnFileChanged(object sender, FileSystemEventArgs e)
        {
            var writeUtc = SafeGetLastWriteUtc(e.FullPath);
            if (writeUtc <= _lastWriteUtc)
                return;

            _lastWriteUtc = writeUtc;
            _pendingReload = true;
        }

        private static DateTime SafeGetLastWriteUtc(string path)
        {
            try
            {
                return File.Exists(path) ? File.GetLastWriteTimeUtc(path) : DateTime.UtcNow;
            }
            catch
            {
                return DateTime.UtcNow;
            }
        }

        private static void OnEditorUpdate()
        {
            if (!_pendingReload || !AutoReload)
                return;

            _pendingReload = false;
            ReloadWatchedGraph();
        }

        private static void DisposeWatcher()
        {
            if (_watcher == null)
                return;

            _watcher.EnableRaisingEvents = false;
            _watcher.Changed -= OnFileChanged;
            _watcher.Created -= OnFileChanged;
            _watcher.Renamed -= OnFileChanged;
            _watcher.Dispose();
            _watcher = null;
        }
    }
}
