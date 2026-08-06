using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using DJTechRuntime.PCG;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Drives explicit Meshy Image-to-3D generation from the node inspector.
    /// HTTP work runs on a background thread (matching the polling contract:
    /// create → poll status/progress every 5s → download GLB); inspector UI polls
    /// <see cref="GetState"/> via UI Toolkit schedules, and completion is applied
    /// on the main thread from an EditorApplication.update pump.
    /// </summary>
    public static class PcgMeshyGenerateController
    {
        public sealed class State
        {
            public bool Running;
            public string Message = "";
            public float Progress;
            public string Error;
            public bool Succeeded;
            public string ModelPath;
        }

        private sealed class Entry
        {
            public readonly State State = new();
            public CancellationTokenSource Cts;
            public Task<PcgMeshyClient.GenerateResult> Task;
            public Action<string> OnSucceeded;
        }

        private static readonly Dictionary<string, Entry> s_Entries = new();
        private static bool s_PumpRegistered;

        /// <summary>Snapshot of the node's generation state (empty when never started).</summary>
        public static State GetState(string nodeId)
        {
            lock (s_Entries)
            {
                if (!string.IsNullOrEmpty(nodeId) && s_Entries.TryGetValue(nodeId, out var entry))
                {
                    lock (entry.State)
                    {
                        return new State
                        {
                            Running = entry.State.Running,
                            Message = entry.State.Message,
                            Progress = entry.State.Progress,
                            Error = entry.State.Error,
                            Succeeded = entry.State.Succeeded,
                            ModelPath = entry.State.ModelPath,
                        };
                    }
                }
            }
            return new State();
        }

        public static bool IsRunning(string nodeId) => GetState(nodeId).Running;

        /// <summary>
        /// Begins generation for the node. Must be called on the main thread —
        /// cache-key hashing and PNG encoding touch Unity APIs. On success the
        /// downloaded GLB lands in Library/PCG/MeshyCache and
        /// <paramref name="onSucceeded"/> is invoked on the main thread with the path.
        /// </summary>
        public static void Begin(string nodeId, PcgNodeData data, Action<string> onSucceeded)
        {
            if (string.IsNullOrEmpty(nodeId) || IsRunning(nodeId))
                return;

            var entry = new Entry { OnSucceeded = onSucceeded };
            lock (s_Entries)
                s_Entries[nodeId] = entry;

            if (!PcgMeshyResolver.TryBuildGenerateRequest(
                    nodeId, data, out var request, out var absolutePath, out var error))
            {
                lock (entry.State)
                {
                    entry.State.Error = error;
                    entry.State.Succeeded = false;
                }
                return;
            }

            lock (entry.State)
            {
                entry.State.Running = true;
                entry.State.Message = "Creating Meshy task…";
                entry.State.Progress = 0.05f;
                entry.State.Error = null;
                entry.State.Succeeded = false;
                entry.State.ModelPath = null;
            }
            EnsurePump();

            entry.Cts = new CancellationTokenSource();
            var token = entry.Cts.Token;
            entry.Task = Task.Run(() =>
                PcgMeshyClient.GenerateAndDownload(
                    request,
                    absolutePath,
                    token,
                    (msg, t) =>
                    {
                        lock (entry.State)
                        {
                            entry.State.Message = msg;
                            entry.State.Progress = t;
                        }
                    }),
                token);
        }

        public static void Cancel(string nodeId)
        {
            lock (s_Entries)
            {
                if (!string.IsNullOrEmpty(nodeId) &&
                    s_Entries.TryGetValue(nodeId, out var entry) &&
                    entry.State.Running)
                {
                    entry.Cts?.Cancel();
                }
            }
        }

        private static void EnsurePump()
        {
            if (s_PumpRegistered)
                return;
            s_PumpRegistered = true;
            EditorApplication.update += Pump;
        }

        private static void Pump()
        {
            List<KeyValuePair<string, Entry>> finished = null;
            var anyRunning = false;
            lock (s_Entries)
            {
                foreach (var kvp in s_Entries)
                {
                    var entry = kvp.Value;
                    lock (entry.State)
                    {
                        if (entry.State.Running && entry.Task != null && entry.Task.IsCompleted)
                            (finished ??= new List<KeyValuePair<string, Entry>>()).Add(kvp);
                        else if (entry.State.Running)
                            anyRunning = true;
                    }
                }
            }

            if (finished != null)
            {
                foreach (var kvp in finished)
                    FinalizeEntry(kvp.Key, kvp.Value);
            }

            if (!anyRunning && finished == null)
            {
                EditorApplication.update -= Pump;
                s_PumpRegistered = false;
            }
        }

        private static void FinalizeEntry(string nodeId, Entry entry)
        {
            PcgMeshyClient.GenerateResult result;
            try
            {
                result = entry.Task.Result;
            }
            catch (Exception ex)
            {
                result = new PcgMeshyClient.GenerateResult
                {
                    Error = ex.GetBaseException().Message,
                };
            }

            lock (entry.State)
            {
                entry.State.Running = false;
                if (result.Ok)
                {
                    entry.State.Succeeded = true;
                    entry.State.Progress = 1f;
                    entry.State.Message = "Done";
                    entry.State.ModelPath = result.ModelPath;
                    entry.State.Error = null;
                }
                else
                {
                    entry.State.Succeeded = false;
                    entry.State.Error = result.Error ?? "Meshy generation failed.";
                }
            }

            if (!result.Ok)
                return;

            Debug.Log(
                $"[PCG] Meshy3DGenerator '{nodeId}' cached → {result.ModelPath}" +
                (result.ConsumedCredits > 0 ? $" (credits={result.ConsumedCredits})" : ""));
            try
            {
                entry.OnSucceeded?.Invoke(result.ModelPath);
            }
            catch (Exception ex)
            {
                Debug.LogException(ex);
            }
        }
    }
}
