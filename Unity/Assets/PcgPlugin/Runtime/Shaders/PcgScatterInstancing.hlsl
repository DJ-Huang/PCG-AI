#ifndef PCG_SCATTER_INSTANCING_INCLUDED
#define PCG_SCATTER_INSTANCING_INCLUDED

#if (defined(UNITY_SUPPORT_INSTANCING) && defined(PROCEDURAL_INSTANCING_ON)) || defined(UNITY_PROCEDURAL_INSTANCING_ENABLED)
#define PCG_SCATTER_GPU_ACTIVE
#endif

StructuredBuffer<float4x4> _PcgTransformBuffer;

#ifdef PCG_SCATTER_GPU_ACTIVE
#ifdef unity_ObjectToWorld
#undef unity_ObjectToWorld
#endif
#ifdef unity_WorldToObject
#undef unity_WorldToObject
#endif
#endif

float4x4 PcgInverseMatrix(float4x4 m)
{
    float3 c0 = m._m00_m10_m20;
    float3 c1 = m._m01_m11_m21;
    float3 c2 = m._m02_m12_m22;
    float3 c3 = m._m03_m13_m23;

    float3 r0 = float3(
        c1.y * c2.z - c1.z * c2.y,
        c0.z * c2.y - c0.y * c2.z,
        c0.y * c1.z - c0.z * c1.y);
    float3 r1 = float3(
        c1.z * c2.x - c1.x * c2.z,
        c0.x * c2.z - c0.z * c2.x,
        c0.z * c1.x - c0.x * c1.z);
    float3 r2 = float3(
        c1.x * c2.y - c1.y * c2.x,
        c0.y * c2.x - c0.x * c2.y,
        c0.x * c1.y - c0.y * c1.x);

    float det = dot(c0, r0);
    float invDet = 1.0 / det;

    float3x3 invRotScale = float3x3(r0, r1, r2) * invDet;
    float3 invTrans = -mul(invRotScale, c3);

    return float4x4(
        float4(invRotScale[0], 0.0),
        float4(invRotScale[1], 0.0),
        float4(invRotScale[2], 0.0),
        float4(invTrans, 1.0));
}

void setupPcgScatter()
{
#ifdef PCG_SCATTER_GPU_ACTIVE
    unity_ObjectToWorld = _PcgTransformBuffer[unity_InstanceID];
    unity_WorldToObject = PcgInverseMatrix(unity_ObjectToWorld);
#endif
}

#endif
