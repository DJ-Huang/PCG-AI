using System;
using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    [Serializable]
    public class PcgPoint
    {
        public float x;
        public float y;
        public float z;
    }

    [Serializable]
    public class PcgExecutionResult
    {
        public string status;
        public string prefab;
        public float scale = 1f;
        public int pointCount;
        public PcgPoint[] points;
    }

    /// <summary>
    /// Parses result JSON produced by pcg_execute_graph.
    /// </summary>
    public static class PcgResultParser
    {
        public static bool TryParse(string resultJson, out PcgExecutionResult result, out string error)
        {
            result = null;
            error = null;

            if (string.IsNullOrWhiteSpace(resultJson))
            {
                error = "Result JSON is empty.";
                return false;
            }

            try
            {
                result = JsonUtility.FromJson<PcgExecutionResult>(resultJson);
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }

            if (result == null)
            {
                error = "Failed to deserialize result JSON.";
                return false;
            }

            if (result.points == null)
                result.points = Array.Empty<PcgPoint>();

            return true;
        }

        public static List<Vector3> ToVector3List(PcgExecutionResult result)
        {
            var vectors = new List<Vector3>(result.points.Length);
            foreach (var point in result.points)
                vectors.Add(new Vector3(point.x, point.y, point.z));
            return vectors;
        }
    }
}
