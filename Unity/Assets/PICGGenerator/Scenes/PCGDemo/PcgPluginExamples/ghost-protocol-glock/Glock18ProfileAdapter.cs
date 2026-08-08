using System.Collections.Generic;
using UnityEngine;

namespace DJTechRuntime.PCG.Examples.GhostProtocolGlock
{
    /// <summary>
    /// Dedicated Glock-18 silhouette adapter (not a generic pistol body).
    /// FRONT/BACK alpha traces agree to ~1.6 px; stations are measured, not freehand.
    /// Units: meters. Grip rake: 22°. Z thickness inferred from broadside views.
    /// PCG loft profiles in the .pcg file are authored from these stations.
    /// </summary>
    public static class Glock18ProfileAdapter
    {
        public const float GripRakeDegrees = 22f;
        public const float OverallLength = 0.204f;
        public const float OverallHeight = 0.139f;
        public const float MaxWidthZ = 0.034f;
        public const float TraceAgreementPx = 1.6f;

        public const float DustCoverHeight = 0.055f;
        public const float SlideDeckY = 0.133f;
        public const float AccessoryRailSlotCount = 4;
        public const float AccessoryRailPitch = 0.010f;

        /// <summary>Explode offsets per assembly module (local space).</summary>
        public static readonly Dictionary<string, Vector3> ExplodeOffsets = new()
        {
            { "slide_shell", new Vector3(0.04f, 0.03f, 0f) },
            { "frame_receiver", new Vector3(0f, 0.02f, 0.02f) },
            { "grip_assembly", new Vector3(-0.05f, -0.04f, 0f) },
            { "internals", new Vector3(0.02f, 0.01f, -0.03f) },
            { "trigger_group", new Vector3(-0.03f, -0.02f, 0.04f) },
            { "trigger_guard", new Vector3(0.01f, -0.03f, 0.02f) },
            { "shell_outer", new Vector3(0.05f, 0.04f, 0.01f) },
        };

        /// <summary>
        /// Closed YZ profile at fixed X (meters). Used as SSOT for loft stations.
        /// Y = height, Z = half-width (inferred). Stations capture dust-cover taper,
        /// receiver, slide flats, rear-sight deck, and muzzle nose.
        /// </summary>
        public static readonly ProfileStation[] ShellStations =
        {
            new(-0.034f, new Vector2[]
            {
                new(0.002f, -0.013f), new(0.012f, -0.014f), new(0.042f, -0.013f),
                new(0.085f, -0.011f), new(0.088f, 0.011f), new(0.042f, 0.013f),
                new(0.012f, 0.013f), new(0.002f, 0.012f),
            }),
            new(-0.014f, new Vector2[]
            {
                new(0.052f, -0.014f), new(0.068f, -0.013f), new(0.092f, -0.011f),
                new(0.095f, 0.010f), new(0.068f, 0.014f), new(0.052f, 0.013f),
            }),
            new(0.028f, new Vector2[]
            {
                new(0.052f, -0.011f), new(0.078f, -0.010f), new(0.102f, -0.009f),
                new(0.118f, 0.009f), new(0.078f, 0.012f), new(0.052f, 0.011f),
            }),
            new(0.078f, new Vector2[]
            {
                new(0.102f, -0.016f), new(0.118f, -0.016f), new(0.133f, -0.015f),
                new(0.133f, 0.015f), new(0.118f, 0.016f), new(0.102f, 0.016f),
            }),
            new(0.132f, new Vector2[]
            {
                new(0.112f, -0.014f), new(0.130f, -0.014f), new(0.136f, -0.012f),
                new(0.138f, 0.004f), new(0.135f, 0.016f), new(0.118f, 0.016f),
                new(0.112f, 0.014f),
            }),
            new(0.148f, new Vector2[]
            {
                new(0.114f, -0.012f), new(0.128f, -0.011f), new(0.132f, -0.008f),
                new(0.132f, 0.008f), new(0.128f, 0.011f), new(0.114f, 0.010f),
            }),
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
