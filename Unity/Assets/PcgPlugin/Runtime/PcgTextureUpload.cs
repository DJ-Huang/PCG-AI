using System;

namespace DJTechRuntime.PCG
{
    /// <summary>
    /// Runtime texture pixels uploaded to pcg-core before graph execution.
    /// SlotId must match the ImageTexture node id in the graph.
    /// </summary>
    public sealed class PcgTextureUpload
    {
        public string SlotId;
        public int Width;
        public int Height;
        public float[] Rgba;
    }
}
