using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Resolves GetSplineData nodes to polyline buffers for pcg_execute_graph_v7.
    /// </summary>
    public static class PcgSplineResolver
    {
        public static List<PcgSplineUpload> CollectFromGraphJson(
            string json,
            GameObject componentHost,
            IReadOnlyList<PcgSplineBinding> componentBindings,
            IReadOnlyList<PcgPreviewSplineBinding> previewBindings)
        {
            var uploads = new List<PcgSplineUpload>();
            if (string.IsNullOrWhiteSpace(json))
                return uploads;

            if (!PcgGraphSerializer.TryFromJson(json, out var doc, out _))
                return uploads;

            foreach (var node in doc.nodes)
            {
                if (node.type != "GetSplineData" || string.IsNullOrEmpty(node.id))
                    continue;

                var upload = TryResolveGetSplineData(
                    node.id, node.data, componentHost, componentBindings, previewBindings);
                if (upload != null)
                    uploads.Add(upload);
            }

            return uploads;
        }

        public static PcgSplineUpload TryResolveGetSplineData(
            string slotId,
            PcgNodeData nodeData,
            GameObject componentHost,
            IReadOnlyList<PcgSplineBinding> componentBindings,
            IReadOnlyList<PcgPreviewSplineBinding> previewBindings)
        {
            var sourceText = nodeData?.GetRaw("source")?.ToString() ?? "Binding";
            var bindingKey = nodeData?.GetRaw("bindingKey")?.ToString() ?? "bridgePath";

            LineRenderer line = null;
            if (sourceText.Equals("Self", System.StringComparison.OrdinalIgnoreCase))
            {
                if (componentHost != null)
                    line = componentHost.GetComponent<LineRenderer>();
            }
            else
            {
                line = ResolveBindingLine(bindingKey, componentBindings, previewBindings);
            }

            if (line == null || line.positionCount < 2)
                return null;

            return BuildUpload(slotId, line);
        }

        private static LineRenderer ResolveBindingLine(
            string bindingKey,
            IReadOnlyList<PcgSplineBinding> componentBindings,
            IReadOnlyList<PcgPreviewSplineBinding> previewBindings)
        {
            if (previewBindings != null)
            {
                foreach (var preview in previewBindings)
                {
                    if (preview == null || preview.previewLineRenderer == null)
                        continue;
                    if (string.IsNullOrEmpty(bindingKey) ||
                        preview.bindingKey == bindingKey)
                        return preview.previewLineRenderer;
                }
            }

            if (componentBindings == null)
                return null;

            foreach (var binding in componentBindings)
            {
                if (binding == null || binding.lineRenderer == null)
                    continue;
                if (string.IsNullOrEmpty(bindingKey) || binding.bindingKey == bindingKey)
                    return binding.lineRenderer;
            }

            return null;
        }

        private static PcgSplineUpload BuildUpload(string slotId, LineRenderer line)
        {
            var count = line.positionCount;
            if (count < 2)
                return null;

            var positions = new float[count * 3];
            for (var i = 0; i < count; i++)
            {
                var p = line.useWorldSpace ? line.GetPosition(i) : line.transform.TransformPoint(line.GetPosition(i));
                positions[i * 3] = p.x;
                positions[i * 3 + 1] = p.y;
                positions[i * 3 + 2] = p.z;
            }

            return new PcgSplineUpload
            {
                SlotId = slotId,
                Positions = positions,
                SplinePointCounts = new[] { count },
                Closed = new byte[] { (byte)(line.loop ? 1 : 0) },
                SplineCount = 1,
            };
        }
    }
}
