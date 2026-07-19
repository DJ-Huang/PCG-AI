namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Removes authoring-only execution semantics before a graph reaches PcgCore.
    /// ExportFBX behaves like a passive Output during normal Editor and Player cooks;
    /// its file-writing ROP is invoked explicitly by the Editor assembly.
    /// </summary>
    public static class PcgGraphExecutionPolicy
    {
        public static bool TryPrepareJson(string json, out string preparedJson, out string error)
        {
            if (string.IsNullOrWhiteSpace(json))
            {
                preparedJson = null;
                error = "JSON is empty.";
                return false;
            }
            if (json.IndexOf("ExportFBX", System.StringComparison.Ordinal) < 0)
            {
                preparedJson = json;
                error = null;
                return true;
            }

            preparedJson = null;
            if (!PcgGraphSerializer.TryFromJson(json, out var document, out error))
                return false;

            var removedIds = ReplaceEditorOnlyNodes(document.nodes, document.edges);
            if (document.subgraphs != null)
            {
                foreach (var subgraph in document.subgraphs)
                    removedIds.UnionWith(ReplaceEditorOnlyNodes(subgraph.nodes, subgraph.edges));
            }
            document.parameters?.RemoveAll(parameter =>
                parameter != null && removedIds.Contains(parameter.targetNode));

            preparedJson = PcgGraphSerializer.ToJson(document, pretty: false);
            return true;
        }

        private static System.Collections.Generic.HashSet<string> ReplaceEditorOnlyNodes(
            System.Collections.Generic.List<PcgGraphNodeRecord> nodes,
            System.Collections.Generic.List<PcgGraphEdgeRecord> edges)
        {
            var removedIds = new System.Collections.Generic.HashSet<string>();
            if (nodes == null)
                return removedIds;

            var hasRegularOutput = nodes.Exists(node => node != null && node.type == "Output");
            var keptPassiveOutput = false;
            foreach (var node in new System.Collections.Generic.List<PcgGraphNodeRecord>(nodes))
            {
                if (node == null || node.type != "ExportFBX")
                    continue;

                if (!hasRegularOutput && !keptPassiveOutput)
                {
                    node.type = "Output";
                    node.data = new PcgNodeData();
                    node.data.SetRaw("label", "FBX Export (passive cook)");
                    keptPassiveOutput = true;
                    continue;
                }

                removedIds.Add(node.id);
                nodes.Remove(node);
            }
            edges?.RemoveAll(edge => edge != null &&
                (removedIds.Contains(edge.source) || removedIds.Contains(edge.target)));
            return removedIds;
        }
    }
}
