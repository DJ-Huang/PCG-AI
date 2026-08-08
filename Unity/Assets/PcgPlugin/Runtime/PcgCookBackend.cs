using System.Collections.Generic;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// All cooks go through localhost pcg-server over HTTP.
    /// </summary>
    public static class PcgCookBackend
    {
        public static (PcgResultCode code, PcgGraphExecuteResult result) ExecuteGraph(
            string json,
            int seed,
            IReadOnlyList<PcgTextureUpload> textures,
            IReadOnlyList<PcgMeshUpload> meshes,
            IReadOnlyList<PcgSplineUpload> splines,
            IReadOnlyList<PcgHeightFieldUpload> heightfields,
            string jobId = null) =>
            PcgNative.ExecuteGraph(json, seed, textures, meshes, splines, heightfields, jobId);

        public static void RequestCancel(string jobId = null) => PcgNative.RequestCancel(jobId);

        public static void ClearCancel() => PcgNative.ClearCancel();

        public static void ClearCookCache() => PcgNative.ClearCookCache();

        public static string DescribeBackend() => $"RemoteHttp ({PcgCookClient.BaseUrl})";
    }
}
