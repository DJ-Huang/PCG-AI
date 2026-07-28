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
            IReadOnlyList<PcgHeightFieldUpload> heightfields) =>
            PcgNative.ExecuteGraph(json, seed, textures, meshes, splines, heightfields);

        public static void RequestCancel() => PcgNative.RequestCancel();

        public static void ClearCancel() => PcgNative.ClearCancel();

        public static void ClearCookCache() => PcgNative.ClearCookCache();

        public static string DescribeBackend() => $"RemoteHttp ({PcgCookClient.BaseUrl})";
    }
}
