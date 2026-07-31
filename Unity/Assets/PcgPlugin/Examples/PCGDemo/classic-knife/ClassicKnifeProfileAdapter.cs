using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG.Examples.ClassicKnife
{
    /// <summary>
    /// Dedicated CS2 Classic Knife silhouette adapter (not a generic blade body).
    /// FRONT/BACK alpha traces agree to ~0.67 px; stations are measured from the
    /// reference pair, not freehand. Units: meters. Z thickness inferred (broadside only).
    /// PCG loft profiles in the .pcg file are authored from these stations.
    /// </summary>
    public static class ClassicKnifeProfileAdapter
    {
        public const float OverallLength = 0.245f;
        public const float OverallHeight = 0.03326f;
        public const float MaxWidthZ = 0.0224f;
        public const float TraceAgreementPx = 0.67f;

        public const float BladeHalfThickness = 0.00175f;
        public const float HandleHalfThickness = 0.0105f;
        public const float GuardHalfThickness = 0.0098f;

        public const float TipX = 0.24476f;
        public const float ButtX = 0.0f;
        public const float GuardX = 0.10719f;
        public const float FerruleX = 0.0965f;
        public const float FerruleWidth = 0.00135f;

        public const int BeadCount = 18;
        public const float BeadRadius = 0.00045f;
        public const float QuiltPitch = 0.0032f;
        public const float ScrewRadius = 0.00135f;
        public const float ScrewCountersink = 0.0021f;

        public static readonly Vector2 Choil = new(0.10838f, -0.01663f);
        public static readonly Vector2 Lanyard = new(0.00674f, -0.00518f);
        public const float LanyardRadius = 0.0034f;

        public static readonly Vector2[] Scallops =
        {
            new(0.12681f, 0.01376f),
            new(0.12298f, 0.01352f),
            new(0.11963f, 0.01352f),
            new(0.11604f, 0.01328f),
            new(0.11221f, 0.01304f),
            new(0.10886f, 0.01304f),
        };

        public static readonly Vector2[] FrontScrews =
        {
            new(0.0705f, 0.0072f),
            new(0.0705f, -0.0068f),
        };

        public static readonly Vector2[] RearScrews =
        {
            new(0.0115f, 0.0078f),
            new(0.0098f, -0.0065f),
        };

        public static readonly Dictionary<string, Vector3> ExplodeOffsets = new()
        {
            { "blade_body", new Vector3(0.03f, 0.01f, 0f) },
            { "crossguard", new Vector3(0.01f, 0.02f, 0.01f) },
            { "ferrule", new Vector3(0.0f, 0.015f, 0.015f) },
            { "tang_grip", new Vector3(-0.02f, -0.01f, 0.02f) },
            { "butt_plate", new Vector3(-0.03f, -0.015f, 0f) },
            { "hardware", new Vector3(0.01f, -0.02f, -0.02f) },
        };

        /// <summary>
        /// Closed YZ profile at fixed X (meters). Captures drop-point tip, six spine
        /// scallops, deep choil, hammer-head guard, ferrule, grip envelope, staircase butt.
        /// </summary>
        public static readonly ProfileStation[] BodyStations =
        {
            new(0.00048f, new Vector2[] { new(-0.00455f, -0.0112f), new(-0.00406f, -0.0112f), new(-0.00371f, -0.01085f), new(-0.00371f, 0.01085f), new(-0.00406f, 0.0112f), new(-0.00455f, 0.0112f), new(-0.0049f, 0.01085f), new(-0.0049f, -0.01085f) }),
            new(0.00646f, new Vector2[] { new(-0.01149f, -0.0112f), new(0.00168f, -0.0112f), new(0.00203f, -0.01085f), new(0.00203f, 0.01085f), new(0.00168f, 0.0112f), new(-0.01149f, 0.0112f), new(-0.01184f, 0.01085f), new(-0.01184f, -0.01085f) }),
            new(0.01507f, new Vector2[] { new(-0.01006f, -0.0112f), new(0.01006f, -0.0112f), new(0.01041f, -0.01085f), new(0.01041f, 0.01085f), new(0.01006f, 0.0112f), new(-0.01006f, 0.0112f), new(-0.01041f, 0.01085f), new(-0.01041f, -0.01085f) }),
            new(0.02082f, new Vector2[] { new(-0.00838f, -0.0105f), new(0.01006f, -0.0105f), new(0.01041f, -0.01015f), new(0.01041f, 0.01015f), new(0.01006f, 0.0105f), new(-0.00838f, 0.0105f), new(-0.00873f, 0.01015f), new(-0.00873f, -0.01015f) }),
            new(0.03039f, new Vector2[] { new(-0.00719f, -0.0105f), new(0.01054f, -0.0105f), new(0.01089f, -0.01015f), new(0.01089f, 0.01015f), new(0.01054f, 0.0105f), new(-0.00719f, 0.0105f), new(-0.00754f, 0.01015f), new(-0.00754f, -0.01015f) }),
            new(0.03996f, new Vector2[] { new(-0.00575f, -0.0105f), new(0.01101f, -0.0105f), new(0.01136f, -0.01015f), new(0.01136f, 0.01015f), new(0.01101f, 0.0105f), new(-0.00575f, 0.0105f), new(-0.0061f, 0.01015f), new(-0.0061f, -0.01015f) }),
            new(0.05144f, new Vector2[] { new(-0.00455f, -0.0105f), new(0.01149f, -0.0105f), new(0.01184f, -0.01015f), new(0.01184f, 0.01015f), new(0.01149f, 0.0105f), new(-0.00455f, 0.0105f), new(-0.0049f, 0.01015f), new(-0.0049f, -0.01015f) }),
            new(0.06101f, new Vector2[] { new(-0.00216f, -0.0105f), new(0.01197f, -0.0105f), new(0.01232f, -0.01015f), new(0.01232f, 0.01015f), new(0.01197f, 0.0105f), new(-0.00216f, 0.0105f), new(-0.00251f, 0.01015f), new(-0.00251f, -0.01015f) }),
            new(0.07537f, new Vector2[] { new(-0.00264f, -0.0105f), new(0.01245f, -0.0105f), new(0.0128f, -0.01015f), new(0.0128f, 0.01015f), new(0.01245f, 0.0105f), new(-0.00264f, 0.0105f), new(-0.00299f, 0.01015f), new(-0.00299f, -0.01015f) }),
            new(0.08972f, new Vector2[] { new(-0.00719f, -0.0105f), new(0.01628f, -0.0105f), new(0.01663f, -0.01015f), new(0.01663f, 0.01015f), new(0.01628f, 0.0105f), new(-0.00719f, 0.0105f), new(-0.00754f, 0.01015f), new(-0.00754f, -0.01015f) }),
            new(0.10025f, new Vector2[] { new(-0.00647f, -0.0105f), new(0.01341f, -0.0105f), new(0.01376f, -0.01015f), new(0.01376f, 0.01015f), new(0.01341f, 0.0105f), new(-0.00647f, 0.0105f), new(-0.00682f, 0.01015f), new(-0.00682f, -0.01015f) }),
            new(0.10408f, new Vector2[] { new(-0.00886f, -0.0098f), new(0.01365f, -0.0098f), new(0.014f, -0.00945f), new(0.014f, 0.00945f), new(0.01365f, 0.0098f), new(-0.00886f, 0.0098f), new(-0.00921f, 0.00945f), new(-0.00921f, -0.00945f) }),
            new(0.10791f, new Vector2[] { new(-0.01628f, -0.0098f), new(0.01269f, -0.0098f), new(0.01304f, -0.00945f), new(0.01304f, 0.00945f), new(0.01269f, 0.0098f), new(-0.01628f, 0.0098f), new(-0.01663f, 0.00945f), new(-0.01663f, -0.00945f) }),
            new(0.10886f, new Vector2[] { new(-0.01604f, -0.00174f), new(0.01269f, -0.00174f), new(0.01304f, -0.00139f), new(0.01304f, 0.00139f), new(0.01269f, 0.00174f), new(-0.01604f, 0.00174f), new(-0.01639f, 0.00139f), new(-0.01639f, -0.00139f) }),
            new(0.11173f, new Vector2[] { new(-0.01508f, -0.00172f), new(0.01269f, -0.00172f), new(0.01304f, -0.00137f), new(0.01304f, 0.00137f), new(0.01269f, 0.00172f), new(-0.01508f, 0.00172f), new(-0.01543f, 0.00137f), new(-0.01543f, -0.00137f) }),
            new(0.1146f, new Vector2[] { new(-0.01413f, -0.00171f), new(0.01341f, -0.00171f), new(0.01376f, -0.00136f), new(0.01376f, 0.00136f), new(0.01341f, 0.00171f), new(-0.01413f, 0.00171f), new(-0.01448f, 0.00136f), new(-0.01448f, -0.00136f) }),
            new(0.11939f, new Vector2[] { new(-0.01245f, -0.00168f), new(0.01317f, -0.00168f), new(0.01352f, -0.00133f), new(0.01352f, 0.00133f), new(0.01317f, 0.00168f), new(-0.01245f, 0.00168f), new(-0.0128f, 0.00133f), new(-0.0128f, -0.00133f) }),
            new(0.12322f, new Vector2[] { new(-0.01149f, -0.00166f), new(0.01341f, -0.00166f), new(0.01376f, -0.00131f), new(0.01376f, 0.00131f), new(0.01341f, 0.00166f), new(-0.01149f, 0.00166f), new(-0.01184f, 0.00131f), new(-0.01184f, -0.00131f) }),
            new(0.12992f, new Vector2[] { new(-0.0103f, -0.00162f), new(0.0146f, -0.00162f), new(0.01495f, -0.00127f), new(0.01495f, 0.00127f), new(0.0146f, 0.00162f), new(-0.0103f, 0.00162f), new(-0.01065f, 0.00127f), new(-0.01065f, -0.00127f) }),
            new(0.14236f, new Vector2[] { new(-0.00934f, -0.00155f), new(0.01484f, -0.00155f), new(0.01519f, -0.0012f), new(0.01519f, 0.0012f), new(0.01484f, 0.00155f), new(-0.00934f, 0.00155f), new(-0.00969f, 0.0012f), new(-0.00969f, -0.0012f) }),
            new(0.15289f, new Vector2[] { new(-0.00934f, -0.00149f), new(0.01532f, -0.00149f), new(0.01567f, -0.00114f), new(0.01567f, 0.00114f), new(0.01532f, 0.00149f), new(-0.00934f, 0.00149f), new(-0.00969f, 0.00114f), new(-0.00969f, -0.00114f) }),
            new(0.17011f, new Vector2[] { new(-0.0103f, -0.00139f), new(0.01532f, -0.00139f), new(0.01567f, -0.00104f), new(0.01567f, 0.00104f), new(0.01532f, 0.00139f), new(-0.0103f, 0.00139f), new(-0.01065f, 0.00104f), new(-0.01065f, -0.00104f) }),
            new(0.18351f, new Vector2[] { new(-0.01101f, -0.00131f), new(0.01484f, -0.00131f), new(0.01519f, -0.00096f), new(0.01519f, 0.00096f), new(0.01484f, 0.00131f), new(-0.01101f, 0.00131f), new(-0.01136f, 0.00096f), new(-0.01136f, -0.00096f) }),
            new(0.19404f, new Vector2[] { new(-0.01125f, -0.00125f), new(0.01413f, -0.00125f), new(0.01448f, -0.0009f), new(0.01448f, 0.0009f), new(0.01413f, 0.00125f), new(-0.01125f, 0.00125f), new(-0.0116f, 0.0009f), new(-0.0116f, -0.0009f) }),
            new(0.21031f, new Vector2[] { new(-0.00982f, -0.00116f), new(0.01197f, -0.00116f), new(0.01232f, -0.00081f), new(0.01232f, 0.00081f), new(0.01197f, 0.00116f), new(-0.00982f, 0.00116f), new(-0.01017f, 0.00081f), new(-0.01017f, -0.00081f) }),
            new(0.22466f, new Vector2[] { new(-0.00623f, -0.00108f), new(0.00958f, -0.00108f), new(0.00993f, -0.00073f), new(0.00993f, 0.00073f), new(0.00958f, 0.00108f), new(-0.00623f, 0.00108f), new(-0.00658f, 0.00073f), new(-0.00658f, -0.00073f) }),
            new(0.23519f, new Vector2[] { new(-0.00192f, -0.00102f), new(0.00695f, -0.00102f), new(0.0073f, -0.00067f), new(0.0073f, 0.00067f), new(0.00695f, 0.00102f), new(-0.00192f, 0.00102f), new(-0.00227f, 0.00067f), new(-0.00227f, -0.00067f) }),
            new(0.24476f, new Vector2[] { new(0.00406f, -0.00096f), new(0.00336f, -0.00096f), new(0.00371f, -0.00061f), new(0.00371f, 0.00061f), new(0.00336f, 0.00096f), new(0.00406f, 0.00096f), new(0.00371f, 0.00061f), new(0.00371f, -0.00061f) })
        };

        public readonly struct ProfileStation
        {
            public readonly float X;
            public readonly Vector2[] Yz;

            public ProfileStation(float x, Vector2[] yz)
            {
                X = x;
                Yz = yz;
            }
        }
    }
}
