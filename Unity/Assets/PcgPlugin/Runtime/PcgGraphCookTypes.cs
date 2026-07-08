using System;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    /// <summary>Houdini-style cook trigger (Component + Graph Editor).</summary>
    public enum PcgCookMode
    {
        None = 0,
        RunOnStart = 1,
        EveryFrame = 2,
        OnParameterChange = 3,
        Manual = 4,
    }

    /// <summary>How scatter point results are drawn.</summary>
    public enum PcgScatterDisplayMode
    {
        MergedMesh = 0,
        GpuInstancing = 1,
    }

    [Serializable]
    public class PcgMeshBinding
    {
        public string bindingKey = "targetMesh";
        public PcgMeshBindingSource source = PcgMeshBindingSource.SceneObject;
        public GameObject sceneObject;
        public Mesh meshAsset;
    }

    public enum PcgMeshBindingSource
    {
        Self = 0,
        SceneObject = 1,
        Asset = 2,
        Binding = 3,
    }

    [Serializable]
    public class PcgPreviewMeshBinding
    {
        public string bindingKey = "targetMesh";
        public MeshFilter previewMeshFilter;
    }
}
