using System;
using UnityEditor;
using UnityEngine;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>
    /// ScriptableObject proxy that bridges the in-memory PcgGraphDocument
    /// to Unity's native Undo system. Mirrors Shader Graph's GraphObject pattern:
    /// the entire graph is serialized to a JSON string stored in a [SerializeField]
    /// field, and a version-number mismatch detects when Unity has performed undo/redo.
    /// </summary>
    public sealed class PcgGraphState : ScriptableObject
    {
        [SerializeField] private string m_GraphJson = "";
        [SerializeField] private int m_SerializedVersion;
        [NonSerialized] private int m_DeserializedVersion;

        /// <summary>Current serialized graph state (restored by Unity on undo/redo).</summary>
        public string GraphJson => m_GraphJson;

        /// <summary>True when Unity's undo/redo has restored m_SerializedVersion
        /// but m_DeserializedVersion has not yet been synced.</summary>
        public bool WasUndoRedoPerformed => m_DeserializedVersion != m_SerializedVersion;

        public static PcgGraphState Create()
        {
            var state = CreateInstance<PcgGraphState>();
            state.hideFlags = HideFlags.HideAndDontSave;
            return state;
        }

        /// <summary>Register the current state with Unity's Undo system
        /// and increment both version counters.</summary>
        public void RegisterCompleteObjectUndo(string actionName)
        {
            Undo.RegisterCompleteObjectUndo(this, actionName);
            m_SerializedVersion++;
            m_DeserializedVersion++;
        }

        /// <summary>Update the serialized graph JSON (call after applying a mutation).</summary>
        public void SetGraphJson(string json)
        {
            m_GraphJson = json;
        }

        /// <summary>Synchronize the deserialized version after restoring from undo/redo.</summary>
        public void HandleUndoRedo()
        {
            m_DeserializedVersion = m_SerializedVersion;
        }

        /// <summary>Remove all undo entries for this proxy from Unity's stack.</summary>
        public void ClearUndo()
        {
            Undo.ClearUndo(this);
        }

        /// <summary>Cleanup: clear undo entries and destroy the proxy.</summary>
        public void Destroy()
        {
            Undo.ClearUndo(this);
            DestroyImmediate(this);
        }
    }
}
