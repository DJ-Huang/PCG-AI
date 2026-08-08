using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// Runs inspector bottom actions (e.g. Meshy Generate) off the main thread.
    /// The prepare delegate runs on the calling (main) thread and must capture
    /// everything Unity-API-bound (texture encode, EditorPrefs-backed settings,
    /// node data) into a thread-safe work delegate; completion is marshaled back
    /// through an EditorApplication.update pump. Inspector sections poll
    /// <see cref="GetState"/> via UI Toolkit schedules (auto-stopped on detach).
    /// State is keyed by (nodeId, actionId) so one node can host several actions.
    /// </summary>
    public static class PcgNodeAsyncActionController
    {
        public sealed class State
        {
            public bool Running;
            public string Message = "";
            public float Progress;
            public string Error;
            public bool Succeeded;
            public string Payload;
        }

        public readonly struct WorkResult
        {
            public readonly bool Ok;
            public readonly string Error;
            public readonly string Payload;
            public readonly string LogOnSuccess;

            private WorkResult(bool ok, string error, string payload, string logOnSuccess)
            {
                Ok = ok;
                Error = error;
                Payload = payload;
                LogOnSuccess = logOnSuccess;
            }

            public static WorkResult Success(string payload = null, string logOnSuccess = null) =>
                new(true, null, payload, logOnSuccess);

            public static WorkResult Failure(string error) =>
                new(false, error, null, null);
        }

        public readonly struct PrepareResult
        {
            public readonly string Error;
            public readonly Func<CancellationToken, Action<string, float>, WorkResult> Work;
            public bool Ok => Work != null;

            private PrepareResult(
                string error, Func<CancellationToken, Action<string, float>, WorkResult> work)
            {
                Error = error;
                Work = work;
            }

            public static PrepareResult Failure(string error) => new(error, null);

            public static PrepareResult Success(
                Func<CancellationToken, Action<string, float>, WorkResult> work) => new(null, work);
        }

        private sealed class Entry
        {
            public readonly State State = new();
            public CancellationTokenSource Cts;
            public Task<WorkResult> Task;
            public Action<string> OnSucceeded;
        }

        private static readonly Dictionary<string, Entry> s_Entries = new();
        private static bool s_PumpRegistered;

        /// <summary>Snapshot of the action state (empty when never started).</summary>
        public static State GetState(string nodeId, string actionId)
        {
            lock (s_Entries)
            {
                if (TryGetEntry(nodeId, actionId, out var entry))
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
                            Payload = entry.State.Payload,
                        };
                    }
                }
            }
            return new State();
        }

        public static bool IsRunning(string nodeId, string actionId) =>
            GetState(nodeId, actionId).Running;

        /// <summary>
        /// Main-thread entry point. <paramref name="prepare"/> runs immediately on the
        /// calling thread; on success the returned work runs on a background thread.
        /// <paramref name="onSucceeded"/> receives WorkResult.Payload on the main thread.
        /// No-op while the same (nodeId, actionId) is still running.
        /// </summary>
        public static void Begin(
            string nodeId,
            string actionId,
            Func<PrepareResult> prepare,
            Action<string> onSucceeded)
        {
            if (string.IsNullOrEmpty(nodeId) || string.IsNullOrEmpty(actionId) ||
                IsRunning(nodeId, actionId))
                return;

            var entry = new Entry { OnSucceeded = onSucceeded };
            lock (s_Entries)
                s_Entries[Key(nodeId, actionId)] = entry;

            var prepared = prepare?.Invoke() ?? PrepareResult.Failure("Missing prepare delegate.");
            if (!prepared.Ok)
            {
                lock (entry.State)
                {
                    entry.State.Error = prepared.Error ?? "Prepare failed.";
                    entry.State.Succeeded = false;
                }
                Debug.LogError(
                    $"[PCG] Action prepare failed ({actionId} on '{nodeId}'):\n{prepared.Error}");
                return;
            }

            lock (entry.State)
            {
                entry.State.Running = true;
                entry.State.Message = "Starting…";
                entry.State.Progress = 0f;
                entry.State.Error = null;
                entry.State.Succeeded = false;
                entry.State.Payload = null;
            }
            EnsurePump();

            entry.Cts = new CancellationTokenSource();
            var token = entry.Cts.Token;
            entry.Task = Task.Run(() =>
            {
                try
                {
                    return prepared.Work(
                        token,
                        (msg, t) =>
                        {
                            lock (entry.State)
                            {
                                entry.State.Message = msg;
                                entry.State.Progress = t;
                            }
                        });
                }
                catch (OperationCanceledException)
                {
                    return WorkResult.Failure("Canceled.");
                }
                catch (Exception ex)
                {
                    return WorkResult.Failure(ex.GetBaseException().Message);
                }
            }, token);
        }

        public static void Cancel(string nodeId, string actionId)
        {
            lock (s_Entries)
            {
                if (TryGetEntry(nodeId, actionId, out var entry) && entry.State.Running)
                    entry.Cts?.Cancel();
            }
        }

        private static string Key(string nodeId, string actionId) => nodeId + "|" + actionId;

        private static bool TryGetEntry(string nodeId, string actionId, out Entry entry)
        {
            entry = null;
            return !string.IsNullOrEmpty(nodeId) &&
                   !string.IsNullOrEmpty(actionId) &&
                   s_Entries.TryGetValue(Key(nodeId, actionId), out entry);
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

        private static void FinalizeEntry(string entryKey, Entry entry)
        {
            WorkResult result;
            try
            {
                result = entry.Task.Result;
            }
            catch (Exception ex)
            {
                result = WorkResult.Failure(ex.GetBaseException().Message);
            }

            lock (entry.State)
            {
                entry.State.Running = false;
                if (result.Ok)
                {
                    entry.State.Succeeded = true;
                    entry.State.Progress = 1f;
                    entry.State.Message = "Done";
                    entry.State.Payload = result.Payload;
                    entry.State.Error = null;
                }
                else
                {
                    entry.State.Succeeded = false;
                    entry.State.Error = result.Error ?? "Action failed.";
                }
            }

            if (!result.Ok)
            {
                Debug.LogError(
                    $"[PCG] Action failed ({entryKey}):\n{result.Error}");
                return;
            }

            if (!string.IsNullOrEmpty(result.LogOnSuccess))
                Debug.Log(result.LogOnSuccess);
            try
            {
                entry.OnSucceeded?.Invoke(result.Payload);
            }
            catch (Exception ex)
            {
                Debug.LogException(ex);
            }
        }
    }
}
