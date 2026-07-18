using System;
using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG
{
    public enum PcgHostTerrainSampling
    {
        Center = 0,
        Corner = 1,
    }

    public enum PcgHostTerrainOrientation
    {
        ZX = 0,
        XY = 1,
        YZ = 2,
    }

    public enum PcgHostTerrainBorderType
    {
        Constant = 0,
        Repeat = 1,
        Streak = 2,
    }

    public sealed class PcgHostTerrainLayer
    {
        public string Name;
        public int TupleSize;
        public PcgHostTerrainBorderType BorderType;
        public float BorderValue;
        public float[] Values;
    }

    /// <summary>
    /// Engine-neutral terrain payload. P0 transports named height/mask layers;
    /// later adapters can add splat, foliage, detail, and stable texture names
    /// without changing the grid contract.
    /// </summary>
    public sealed class PcgHostTerrainSurface
    {
        private readonly Dictionary<string, PcgHostTerrainLayer> m_Layers =
            new(StringComparer.Ordinal);

        public int ResolutionX { get; set; }
        public int ResolutionZ { get; set; }
        public double SizeX { get; set; }
        public double SizeZ { get; set; }
        public double CenterX { get; set; }
        public double CenterY { get; set; }
        public double CenterZ { get; set; }
        public PcgHostTerrainSampling Sampling { get; set; } = PcgHostTerrainSampling.Corner;
        public PcgHostTerrainOrientation Orientation { get; set; } = PcgHostTerrainOrientation.ZX;
        public IReadOnlyDictionary<string, PcgHostTerrainLayer> Layers => m_Layers;

        public int SampleCount => ResolutionX > 0 && ResolutionZ > 0
            ? ResolutionX * ResolutionZ
            : 0;

        public bool IsValid
        {
            get
            {
                if (ResolutionX < 2 || ResolutionZ < 2 ||
                    !IsFinitePositive(SizeX) || !IsFinitePositive(SizeZ) ||
                    !m_Layers.TryGetValue("height", out var height) ||
                    !m_Layers.TryGetValue("mask", out var mask))
                {
                    return false;
                }

                foreach (var layer in m_Layers.Values)
                {
                    if (layer == null || string.IsNullOrEmpty(layer.Name) || layer.TupleSize <= 0 ||
                        layer.Values == null || layer.Values.Length != SampleCount * layer.TupleSize)
                    {
                        return false;
                    }
                }

                return height.TupleSize == 1 && mask.TupleSize == 1;
            }
        }

        public void SetLayer(PcgHostTerrainLayer layer)
        {
            if (layer == null || string.IsNullOrEmpty(layer.Name))
                throw new ArgumentException("Terrain layer needs a stable name.", nameof(layer));
            m_Layers[layer.Name] = layer;
        }

        public bool TryGetLayer(string name, out PcgHostTerrainLayer layer) =>
            m_Layers.TryGetValue(name, out layer);

        public PcgHeightFieldUpload ToUpload(string slotId)
        {
            if (!IsValid || string.IsNullOrEmpty(slotId))
                return null;

            return new PcgHeightFieldUpload
            {
                SlotId = slotId,
                ResolutionX = ResolutionX,
                ResolutionZ = ResolutionZ,
                SizeX = SizeX,
                SizeZ = SizeZ,
                CenterX = CenterX,
                CenterY = CenterY,
                CenterZ = CenterZ,
                Sampling = (int)Sampling,
                Orientation = (int)Orientation,
                Heights = m_Layers["height"].Values,
                Mask = m_Layers["mask"].Values,
            };
        }

        private static bool IsFinitePositive(double value) =>
            !double.IsNaN(value) && !double.IsInfinity(value) && value > 0.0;
    }

    public interface IHostTerrainAdapter
    {
        string AdapterId { get; }
        bool TryImportSurface(out PcgHostTerrainSurface surface, out string error);
        bool TryExportSurface(
            PcgHostTerrainSurface surface,
            out PcgTerrainApplyReport report,
            out string error);
    }

    public sealed class PcgHeightFieldUpload
    {
        public string SlotId;
        public int ResolutionX;
        public int ResolutionZ;
        public double SizeX;
        public double SizeZ;
        public double CenterX;
        public double CenterY;
        public double CenterZ;
        public int Sampling;
        public int Orientation;
        public float[] Heights;
        public float[] Mask;
    }

    [Serializable]
    public class PcgTerrainBinding
    {
        public string bindingKey = "targetTerrain";
        public PcgTerrainBindingSource source = PcgTerrainBindingSource.SceneObject;
        public GameObject sceneObject;
        public bool readFromHost = true;
        public bool writeCookResult = true;
    }

    public enum PcgTerrainBindingSource
    {
        Self = 0,
        SceneObject = 1,
    }

    public sealed class PcgTerrainApplyReport
    {
        public bool Applied;
        public bool SkippedUnchanged;
        public bool Resampled;
        public bool ScaledFootprint;
        public int ClampedSamples;
        public int SourceResolutionX;
        public int SourceResolutionZ;
        public int TargetResolution;
    }

    public static class PcgTerrainBindingTable
    {
        /// <summary>
        /// Terrain host path: the PcgGraphComponent lives on the Terrain object.
        /// </summary>
        public static Terrain ResolveSelfTerrain(GameObject componentHost)
        {
            if (componentHost == null)
                return null;
            var terrain = componentHost.GetComponent<Terrain>();
            if (terrain != null)
                return terrain;
            return componentHost.GetComponentInParent<Terrain>();
        }

        public static Terrain ResolveTerrain(PcgTerrainBinding binding, GameObject componentHost)
        {
            if (binding == null)
                return ResolveSelfTerrain(componentHost);

            if (binding.source == PcgTerrainBindingSource.Self)
                return ResolveSelfTerrain(componentHost);

            return binding.sceneObject != null
                ? binding.sceneObject.GetComponent<Terrain>()
                : null;
        }

        public static PcgTerrainBinding FindBinding(
            string bindingKey,
            IReadOnlyList<PcgTerrainBinding> bindings)
        {
            if (bindings == null)
                return null;
            foreach (var binding in bindings)
            {
                if (binding != null && binding.bindingKey == bindingKey)
                    return binding;
            }
            return null;
        }
    }

    public sealed class PcgUnityTerrainAdapter : IHostTerrainAdapter
    {
        // TerrainData stores height samples at roughly 16-bit precision. Treat
        // one quantization step as unchanged to avoid SetHeights feedback churn.
        private const float HeightEpsilon = 2e-5f;
        private readonly Terrain m_Terrain;
        private readonly Transform m_GraphSpace;

        public PcgUnityTerrainAdapter(Terrain terrain, Transform graphSpace)
        {
            m_Terrain = terrain;
            m_GraphSpace = graphSpace;
        }

        public string AdapterId => "unity-terrain";

        public bool TryImportSurface(out PcgHostTerrainSurface surface, out string error)
        {
            surface = null;
            error = null;
            if (!TryGetTerrainData(out var data, out error))
                return false;

            var sourceToGraph = ResolveWorldToGraph() * m_Terrain.transform.localToWorldMatrix;
            if (!IsAxisAligned(sourceToGraph))
            {
                error = "Unity Terrain binding must be axis-aligned with the PcgGraphComponent for P0.";
                return false;
            }

            var resolution = data.heightmapResolution;
            var normalized = data.GetHeights(0, 0, resolution, resolution);
            var size = data.size;
            var baseCenter = sourceToGraph.MultiplyPoint3x4(
                new Vector3(size.x * 0.5f, 0f, size.z * 0.5f));
            var axisX = sourceToGraph.MultiplyVector(new Vector3(size.x, 0f, 0f));
            var axisZ = sourceToGraph.MultiplyVector(new Vector3(0f, 0f, size.z));
            var heights = new float[resolution * resolution];
            var mask = new float[heights.Length];

            for (var z = 0; z < resolution; z++)
            {
                var v = resolution > 1 ? z / (float)(resolution - 1) : 0f;
                for (var x = 0; x < resolution; x++)
                {
                    var u = resolution > 1 ? x / (float)(resolution - 1) : 0f;
                    var sourcePoint = new Vector3(
                        u * size.x,
                        normalized[z, x] * size.y,
                        v * size.z);
                    var graphPoint = sourceToGraph.MultiplyPoint3x4(sourcePoint);
                    var index = z * resolution + x;
                    heights[index] = graphPoint.y - baseCenter.y;
                    mask[index] = 1f;
                }
            }

            surface = new PcgHostTerrainSurface
            {
                ResolutionX = resolution,
                ResolutionZ = resolution,
                SizeX = axisX.magnitude,
                SizeZ = axisZ.magnitude,
                CenterX = baseCenter.x,
                CenterY = baseCenter.y,
                CenterZ = baseCenter.z,
                Sampling = PcgHostTerrainSampling.Corner,
                Orientation = PcgHostTerrainOrientation.ZX,
            };
            surface.SetLayer(new PcgHostTerrainLayer
            {
                Name = "height",
                TupleSize = 1,
                BorderType = PcgHostTerrainBorderType.Streak,
                Values = heights,
            });
            surface.SetLayer(new PcgHostTerrainLayer
            {
                Name = "mask",
                TupleSize = 1,
                BorderType = PcgHostTerrainBorderType.Streak,
                Values = mask,
            });
            return true;
        }

        public bool TryExportSurface(
            PcgHostTerrainSurface surface,
            out PcgTerrainApplyReport report,
            out string error)
        {
            report = new PcgTerrainApplyReport();
            error = null;
            if (!TryGetTerrainData(out var data, out error))
                return false;
            if (surface == null || !surface.IsValid)
            {
                error = "Host Terrain Surface is invalid or is missing height/mask layers.";
                return false;
            }
            if (surface.Orientation != PcgHostTerrainOrientation.ZX)
            {
                error = $"Unity Terrain P0 accepts ZX HeightFields, got {surface.Orientation}.";
                return false;
            }

            surface.TryGetLayer("height", out var heightLayer);
            var targetResolution = data.heightmapResolution;
            var desired = new float[targetResolution, targetResolution];
            var graphToWorld = m_GraphSpace != null
                ? m_GraphSpace.localToWorldMatrix
                : Matrix4x4.identity;
            var worldToTerrain = m_Terrain.transform.worldToLocalMatrix;
            var terrainToGraph = ResolveWorldToGraph() * m_Terrain.transform.localToWorldMatrix;
            if (!IsAxisAligned(terrainToGraph))
            {
                error = "Unity Terrain binding must be axis-aligned with the PcgGraphComponent for P0.";
                return false;
            }
            var terrainSize = data.size;
            if (terrainSize.y <= 0f)
            {
                error = "Unity TerrainData.size.y must be greater than zero.";
                return false;
            }

            report.SourceResolutionX = surface.ResolutionX;
            report.SourceResolutionZ = surface.ResolutionZ;
            report.TargetResolution = targetResolution;
            report.Resampled = surface.ResolutionX != targetResolution ||
                               surface.ResolutionZ != targetResolution;
            var targetAxisX = terrainToGraph.MultiplyVector(
                new Vector3(terrainSize.x, 0f, 0f));
            var targetAxisZ = terrainToGraph.MultiplyVector(
                new Vector3(0f, 0f, terrainSize.z));
            report.ScaledFootprint = !Approximately(surface.SizeX, targetAxisX.magnitude) ||
                                     !Approximately(surface.SizeZ, targetAxisZ.magnitude);

            for (var z = 0; z < targetResolution; z++)
            {
                var v = targetResolution > 1 ? z / (float)(targetResolution - 1) : 0f;
                for (var x = 0; x < targetResolution; x++)
                {
                    var u = targetResolution > 1 ? x / (float)(targetResolution - 1) : 0f;
                    var displacement = SampleLayerBilinear(surface, heightLayer, u, v);
                    var graphPoint = new Vector3(
                        (float)(surface.CenterX + (u - 0.5f) * surface.SizeX),
                        (float)(surface.CenterY + displacement),
                        (float)(surface.CenterZ + (v - 0.5f) * surface.SizeZ));
                    var terrainPoint = worldToTerrain.MultiplyPoint3x4(
                        graphToWorld.MultiplyPoint3x4(graphPoint));
                    var normalized = terrainPoint.y / terrainSize.y;
                    var clamped = Mathf.Clamp01(normalized);
                    if (!Mathf.Approximately(normalized, clamped))
                        report.ClampedSamples++;
                    desired[z, x] = clamped;
                }
            }

            var current = data.GetHeights(0, 0, targetResolution, targetResolution);
            if (HeightsEqual(current, desired))
            {
                report.SkippedUnchanged = true;
                return true;
            }

            data.SetHeights(0, 0, desired);
            report.Applied = true;
            return true;
        }

        private bool TryGetTerrainData(out TerrainData data, out string error)
        {
            data = m_Terrain != null ? m_Terrain.terrainData : null;
            error = null;
            if (m_Terrain == null)
                error = "Terrain binding did not resolve a Unity Terrain component.";
            else if (data == null)
                error = "Bound Unity Terrain has no TerrainData.";
            return data != null;
        }

        private Matrix4x4 ResolveWorldToGraph() => m_GraphSpace != null
            ? m_GraphSpace.worldToLocalMatrix
            : Matrix4x4.identity;

        private static bool IsAxisAligned(Matrix4x4 sourceToGraph)
        {
            var right = sourceToGraph.MultiplyVector(Vector3.right).normalized;
            var up = sourceToGraph.MultiplyVector(Vector3.up).normalized;
            var forward = sourceToGraph.MultiplyVector(Vector3.forward).normalized;
            return Mathf.Abs(Vector3.Dot(right, Vector3.right)) > 0.999f &&
                   Mathf.Abs(Vector3.Dot(up, Vector3.up)) > 0.999f &&
                   Mathf.Abs(Vector3.Dot(forward, Vector3.forward)) > 0.999f;
        }

        private static float SampleLayerBilinear(
            PcgHostTerrainSurface surface,
            PcgHostTerrainLayer layer,
            float u,
            float v)
        {
            var gx = Mathf.Clamp01(u) * (surface.ResolutionX - 1);
            var gz = Mathf.Clamp01(v) * (surface.ResolutionZ - 1);
            var x0 = Mathf.FloorToInt(gx);
            var z0 = Mathf.FloorToInt(gz);
            var x1 = Mathf.Min(x0 + 1, surface.ResolutionX - 1);
            var z1 = Mathf.Min(z0 + 1, surface.ResolutionZ - 1);
            var tx = gx - x0;
            var tz = gz - z0;
            var a = layer.Values[z0 * surface.ResolutionX + x0];
            var b = layer.Values[z0 * surface.ResolutionX + x1];
            var c = layer.Values[z1 * surface.ResolutionX + x0];
            var d = layer.Values[z1 * surface.ResolutionX + x1];
            return Mathf.Lerp(Mathf.Lerp(a, b, tx), Mathf.Lerp(c, d, tx), tz);
        }

        private static bool HeightsEqual(float[,] a, float[,] b)
        {
            if (a == null || b == null || a.GetLength(0) != b.GetLength(0) ||
                a.GetLength(1) != b.GetLength(1))
            {
                return false;
            }
            for (var z = 0; z < a.GetLength(0); z++)
            {
                for (var x = 0; x < a.GetLength(1); x++)
                {
                    if (Mathf.Abs(a[z, x] - b[z, x]) > HeightEpsilon)
                        return false;
                }
            }
            return true;
        }

        private static bool Approximately(double a, double b) =>
            Math.Abs(a - b) <= Math.Max(1e-5, Math.Max(Math.Abs(a), Math.Abs(b)) * 1e-5);
    }

    public static class PcgTerrainResolver
    {
        public static List<PcgHeightFieldUpload> CollectFromGraphJson(
            string json,
            GameObject componentHost,
            IReadOnlyList<PcgTerrainBinding> bindings)
        {
            var uploads = new List<PcgHeightFieldUpload>();
            if (string.IsNullOrWhiteSpace(json) ||
                !PcgGraphSerializer.TryFromJson(json, out var document, out _))
            {
                return uploads;
            }

            // Preferred path: PcgGraphComponent on the Terrain. Legacy TerrainBindings
            // remain as a Mesh-mode override when the component is not on a Terrain.
            var selfTerrain = PcgTerrainBindingTable.ResolveSelfTerrain(componentHost);

            foreach (var node in document.nodes)
            {
                if (node == null || node.type != "GetTerrainData" || string.IsNullOrEmpty(node.id))
                    continue;

                Terrain terrain = selfTerrain;
                if (terrain == null)
                {
                    var bindingKey = node.data?.GetRaw("bindingKey")?.ToString() ?? "targetTerrain";
                    var binding = PcgTerrainBindingTable.FindBinding(bindingKey, bindings);
                    if (binding == null || !binding.readFromHost)
                        continue;
                    terrain = PcgTerrainBindingTable.ResolveTerrain(binding, componentHost);
                }

                var adapter = new PcgUnityTerrainAdapter(
                    terrain,
                    componentHost != null ? componentHost.transform : null);
                if (!adapter.TryImportSurface(out var surface, out var error))
                {
                    Debug.LogError($"[PCG] Terrain import '{node.id}' failed: {error}", componentHost);
                    continue;
                }

                var upload = surface.ToUpload(node.id);
                if (upload != null)
                    uploads.Add(upload);
            }
            return uploads;
        }

        public static ulong ComputeFingerprint(IReadOnlyList<PcgHeightFieldUpload> uploads)
        {
            const ulong offset = 14695981039346656037UL;
            const ulong prime = 1099511628211UL;
            var hash = offset;
            if (uploads == null)
                return hash;
            foreach (var upload in uploads)
            {
                HashString(ref hash, upload?.SlotId, prime);
                if (upload == null)
                    continue;
                HashInt(ref hash, upload.ResolutionX, prime);
                HashInt(ref hash, upload.ResolutionZ, prime);
                HashDouble(ref hash, upload.SizeX, prime);
                HashDouble(ref hash, upload.SizeZ, prime);
                HashDouble(ref hash, upload.CenterX, prime);
                HashDouble(ref hash, upload.CenterY, prime);
                HashDouble(ref hash, upload.CenterZ, prime);
                HashInt(ref hash, upload.Sampling, prime);
                HashInt(ref hash, upload.Orientation, prime);
                HashFloats(ref hash, upload.Heights, prime);
                HashFloats(ref hash, upload.Mask, prime);
            }
            return hash;
        }

        private static void HashString(ref ulong hash, string value, ulong prime)
        {
            if (value == null)
                return;
            foreach (var ch in value)
            {
                hash ^= (byte)(ch & 0xff);
                hash *= prime;
                hash ^= (byte)(ch >> 8);
                hash *= prime;
            }
        }

        private static void HashInt(ref ulong hash, int value, ulong prime)
        {
            unchecked
            {
                for (var shift = 0; shift < 32; shift += 8)
                {
                    hash ^= (byte)(value >> shift);
                    hash *= prime;
                }
            }
        }

        private static void HashFloats(ref ulong hash, float[] values, ulong prime)
        {
            if (values == null)
                return;
            foreach (var value in values)
            {
                var bytes = BitConverter.GetBytes(value);
                for (var i = 0; i < bytes.Length; i++)
                {
                    hash ^= bytes[i];
                    hash *= prime;
                }
            }
        }

        private static void HashDouble(ref ulong hash, double value, ulong prime)
        {
            var bytes = BitConverter.GetBytes(value);
            for (var i = 0; i < bytes.Length; i++)
            {
                hash ^= bytes[i];
                hash *= prime;
            }
        }
    }

    public static class PcgHeightFieldBinaryParser
    {
        public static bool TryParse(
            byte[] data,
            out PcgHostTerrainSurface surface,
            out string error)
        {
            surface = null;
            error = null;
            if (data == null || data.Length < PcgNative.HeightFieldBinaryHeaderSize)
            {
                error = "HeightField binary payload is too small.";
                return false;
            }

            var offset = 0;
            if (!TryReadUInt32(data, ref offset, out var magic) ||
                !TryReadUInt32(data, ref offset, out var version) ||
                magic != PcgNative.HeightFieldBinaryMagic ||
                version != PcgNative.HeightFieldBinaryVersion)
            {
                error = "HeightField binary header is invalid.";
                return false;
            }

            if (!TryReadInt32(data, ref offset, out var resolutionX) ||
                !TryReadInt32(data, ref offset, out var resolutionZ) ||
                !TryReadInt32(data, ref offset, out var layerCount) ||
                !TryReadUInt32(data, ref offset, out var sampling) ||
                !TryReadUInt32(data, ref offset, out var orientation) ||
                !TryReadUInt32(data, ref offset, out _) ||
                !TryReadDouble(data, ref offset, out var sizeX) ||
                !TryReadDouble(data, ref offset, out var sizeZ) ||
                !TryReadDouble(data, ref offset, out var centerX) ||
                !TryReadDouble(data, ref offset, out var centerY) ||
                !TryReadDouble(data, ref offset, out var centerZ) ||
                resolutionX < 2 || resolutionZ < 2 || layerCount < 0 || layerCount > 4096 ||
                sampling > 1 || orientation > 2)
            {
                error = "HeightField binary metadata is invalid.";
                return false;
            }

            var parsed = new PcgHostTerrainSurface
            {
                ResolutionX = resolutionX,
                ResolutionZ = resolutionZ,
                SizeX = sizeX,
                SizeZ = sizeZ,
                CenterX = centerX,
                CenterY = centerY,
                CenterZ = centerZ,
                Sampling = (PcgHostTerrainSampling)sampling,
                Orientation = (PcgHostTerrainOrientation)orientation,
            };

            for (var i = 0; i < layerCount; i++)
            {
                if (!TryReadUInt32(data, ref offset, out var nameBytes) ||
                    !TryReadInt32(data, ref offset, out var tupleSize) ||
                    !TryReadUInt32(data, ref offset, out var borderType) ||
                    !TryReadSingle(data, ref offset, out var borderValue) ||
                    !TryReadUInt32(data, ref offset, out var valueCount) ||
                    nameBytes == 0 || nameBytes > int.MaxValue || tupleSize <= 0 || tupleSize > 64 ||
                    borderType > 2 ||
                    (long)valueCount != (long)resolutionX * resolutionZ * tupleSize ||
                    valueCount > int.MaxValue ||
                    offset > data.Length - (int)nameBytes)
                {
                    error = $"HeightField layer {i} header is invalid.";
                    return false;
                }

                var name = System.Text.Encoding.UTF8.GetString(data, offset, (int)nameBytes);
                offset += (int)nameBytes;
                var values = new float[(int)valueCount];
                for (var valueIndex = 0; valueIndex < values.Length; valueIndex++)
                {
                    if (!TryReadSingle(data, ref offset, out values[valueIndex]))
                    {
                        error = $"HeightField layer '{name}' payload is truncated.";
                        return false;
                    }
                }

                parsed.SetLayer(new PcgHostTerrainLayer
                {
                    Name = name,
                    TupleSize = tupleSize,
                    BorderType = (PcgHostTerrainBorderType)borderType,
                    BorderValue = borderValue,
                    Values = values,
                });
            }

            if (!parsed.IsValid)
            {
                error = "HeightField binary is missing valid height/mask layers.";
                return false;
            }
            surface = parsed;
            return true;
        }

        private static bool TryReadInt32(byte[] data, ref int offset, out int value)
        {
            value = 0;
            if (offset > data.Length - sizeof(int))
                return false;
            value = BitConverter.ToInt32(data, offset);
            offset += sizeof(int);
            return true;
        }

        private static bool TryReadUInt32(byte[] data, ref int offset, out uint value)
        {
            value = 0;
            if (offset > data.Length - sizeof(uint))
                return false;
            value = BitConverter.ToUInt32(data, offset);
            offset += sizeof(uint);
            return true;
        }

        private static bool TryReadSingle(byte[] data, ref int offset, out float value)
        {
            value = 0f;
            if (offset > data.Length - sizeof(float))
                return false;
            value = BitConverter.ToSingle(data, offset);
            offset += sizeof(float);
            return true;
        }

        private static bool TryReadDouble(byte[] data, ref int offset, out double value)
        {
            value = 0.0;
            if (offset > data.Length - sizeof(double))
                return false;
            value = BitConverter.ToDouble(data, offset);
            offset += sizeof(double);
            return true;
        }
    }
}
