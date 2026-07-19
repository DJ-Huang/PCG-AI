using System.Collections.Generic;
using UnityEngine;
using DJTechRuntime.PCG;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Visualizes geometry groups in the Scene View via Gizmos.
    /// Edge groups: cyan lines. Face groups: yellow translucent. Point groups: red spheres.
    /// Attach to same GameObject as PcgGraphComponent. Enabled when a group is selected for visualization.
    /// </summary>
    public class PcgGroupVisualizer : MonoBehaviour
    {
        [SerializeField] private string m_HighlightedGroupName;
        [SerializeField] private string m_HighlightedDomain;
        [SerializeField] private List<int> m_MemberIds = new();
        [SerializeField] private Vector3[] m_EdgeEndpoints;
        [SerializeField] private Vector3[] m_Vertices;
        [SerializeField] private int[] m_Triangles;

        private bool m_ShowHighlight;

        /// <summary>Current result JSON from graph execution (contains group stats).</summary>
        [SerializeField, HideInInspector] private string m_LastResultJson;

        public bool IsHighlighting => m_ShowHighlight && !string.IsNullOrEmpty(m_HighlightedGroupName);

        /// <summary>Cached result JSON from the last graph execution.</summary>
        public string ResultJson => m_LastResultJson;

        /// <summary>Called after graph execution to cache the result JSON for group lookup.</summary>
        public void SetResultJson(string json)
        {
            m_LastResultJson = json;
        }

        /// <summary>Highlights a specific group by name. Pass null/empty to clear.</summary>
        public void HighlightGroup(string groupName, string domain)
        {
            if (string.IsNullOrEmpty(groupName))
            {
                ClearHighlight();
                return;
            }

            m_HighlightedGroupName = groupName;
            m_HighlightedDomain = domain;
            m_MemberIds.Clear();
            m_EdgeEndpoints = null;

            if (!string.IsNullOrEmpty(m_LastResultJson))
            {
                ParseGroupMembers(m_LastResultJson, groupName, domain, m_MemberIds, out m_EdgeEndpoints);
            }

            CacheMeshData();
            m_ShowHighlight = m_MemberIds.Count > 0;
        }

        public void ClearHighlight()
        {
            m_ShowHighlight = false;
            m_HighlightedGroupName = null;
            m_HighlightedDomain = null;
            m_MemberIds.Clear();
        }

        /// <summary>Returns list of available groups from the last execution result.</summary>
        public List<(string name, string domain, int count)> GetAvailableGroups()
        {
            var result = new List<(string, string, int)>();
            if (string.IsNullOrEmpty(m_LastResultJson))
                return result;

            try
            {
                var root = JsonUtility.FromJson<GroupsWrapper>(m_LastResultJson);
                if (root?.groups == null)
                    return result;
                foreach (var g in root.groups)
                    result.Add((g.name, g.domain, g.count));
            }
            catch
            {
                // JSON may not be in expected format
            }

            return result;
        }

        private void CacheMeshData()
        {
            var mf = GetComponent<MeshFilter>();
            if (mf == null || mf.sharedMesh == null)
            {
                m_Vertices = null;
                m_Triangles = null;
                return;
            }

            var mesh = mf.sharedMesh;
            m_Vertices = mesh.vertices;
            m_Triangles = mesh.triangles;
        }

        private void ParseGroupMembers(string json, string groupName, string domain, List<int> output, out Vector3[] edgeEndpoints)
        {
            output.Clear();
            edgeEndpoints = null;
            try
            {
                var root = JsonUtility.FromJson<GroupsWrapper>(json);
                if (root?.groups == null)
                    return;
                foreach (var g in root.groups)
                {
                    if (g.name != groupName || g.domain != domain)
                        continue;
                    if (g.members == null)
                        continue;
                    foreach (var id in g.members)
                        output.Add(id);
                    if (g.edgeEndpoints != null && g.edgeEndpoints.Length >= 6)
                    {
                        var pts = new Vector3[g.edgeEndpoints.Length / 3];
                        for (int i = 0; i + 2 < g.edgeEndpoints.Length; i += 3)
                            pts[i / 3] = new Vector3(g.edgeEndpoints[i], g.edgeEndpoints[i + 1], g.edgeEndpoints[i + 2]);
                        edgeEndpoints = pts;
                    }
                    return;
                }
            }
            catch
            {
                // ignore parse errors
            }
        }

#if UNITY_EDITOR
        private void OnDrawGizmos()
        {
            if (!m_ShowHighlight || m_MemberIds == null || m_MemberIds.Count == 0)
                return;
            if (m_Vertices == null || m_Vertices.Length == 0)
            {
                CacheMeshData();
                if (m_Vertices == null)
                    return;
            }

            var transformLocalToWorld = transform.localToWorldMatrix;

            if (m_HighlightedDomain == "edge")
            {
                Gizmos.color = new Color(0f, 1f, 0.8f, 0.9f);

                if (m_EdgeEndpoints != null && m_EdgeEndpoints.Length >= 2)
                {
                    for (int i = 0; i + 1 < m_EdgeEndpoints.Length; i += 2)
                    {
                        var p0 = transformLocalToWorld.MultiplyPoint(m_EdgeEndpoints[i]);
                        var p1 = transformLocalToWorld.MultiplyPoint(m_EdgeEndpoints[i + 1]);
                        Gizmos.DrawLine(p0, p1);
                    }
                }
                else
                {
                    foreach (var key in m_MemberIds)
                    {
                        int a = (int)((long)key / 1000000);
                        int b = (int)((long)key % 1000000);
                        if (a < 0 || b < 0 || a >= m_Vertices.Length || b >= m_Vertices.Length)
                            continue;
                        var p0 = transformLocalToWorld.MultiplyPoint(m_Vertices[a]);
                        var p1 = transformLocalToWorld.MultiplyPoint(m_Vertices[b]);
                        Gizmos.DrawLine(p0, p1);
                    }
                }
            }
            else if (m_HighlightedDomain == "face")
            {
                Gizmos.color = new Color(1f, 0.85f, 0f, 0.3f);
                foreach (var faceIdx in m_MemberIds)
                {
                    int baseIdx = faceIdx * 3;
                    if (m_Triangles == null || baseIdx + 2 >= m_Triangles.Length)
                        continue;
                    int v0 = m_Triangles[baseIdx];
                    int v1 = m_Triangles[baseIdx + 1];
                    int v2 = m_Triangles[baseIdx + 2];
                    if (v0 >= m_Vertices.Length || v1 >= m_Vertices.Length || v2 >= m_Vertices.Length)
                        continue;
                    var p0 = transformLocalToWorld.MultiplyPoint(m_Vertices[v0]);
                    var p1 = transformLocalToWorld.MultiplyPoint(m_Vertices[v1]);
                    var p2 = transformLocalToWorld.MultiplyPoint(m_Vertices[v2]);
                    // Draw triangle edges
                    Gizmos.DrawLine(p0, p1);
                    Gizmos.DrawLine(p1, p2);
                    Gizmos.DrawLine(p2, p0);
                }
            }
            else if (m_HighlightedDomain == "point")
            {
                Gizmos.color = new Color(1f, 0.3f, 0.3f, 0.9f);
                foreach (var ptIdx in m_MemberIds)
                {
                    if (ptIdx < 0 || ptIdx >= m_Vertices.Length)
                        continue;
                    var p = transformLocalToWorld.MultiplyPoint(m_Vertices[ptIdx]);
                    Gizmos.DrawSphere(p, 0.05f);
                }
            }
        }
#endif

        [System.Serializable]
        private class GroupEntry
        {
            public string name;
            public string domain;
            public int count;
            public int[] members;
            public float[] edgeEndpoints;
        }

        [System.Serializable]
        private class GroupsWrapper
        {
            public GroupEntry[] groups;
        }
    }
}
