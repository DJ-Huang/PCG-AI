Shader "Hidden/PcgPolygonWireOverlay"
{
    Properties
    {
        _Color ("Color", Color) = (0, 0, 0, 1)
        _LineWidth ("Line Width", Float) = 2
        _Viewport ("Viewport", Vector) = (1, 1, 0, 0)
        _DepthBias ("Depth Bias", Float) = 0.0005
        [Enum(UnityEngine.Rendering.CompareFunction)] _ZTest ("ZTest", Float) = 4
    }

    SubShader
    {
        Tags { "IgnoreProjector" = "True" "Queue" = "Overlay" }

        Pass
        {
            ZWrite Off
            ZTest [_ZTest]
            Blend SrcAlpha OneMinusSrcAlpha
            Cull Off

            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float3 startLocal : POSITION;
                float3 endLocal : TEXCOORD0;
                float2 corner : TEXCOORD1;
            };

            struct v2f
            {
                float4 pos : SV_POSITION;
                noperspective float2 corner : TEXCOORD0;
                float visible : TEXCOORD1;
                noperspective float lineLengthPixels : TEXCOORD2;
            };

            float4 _Color;
            float _LineWidth;
            float4 _Viewport;
            float _DepthBias;

            v2f vert(appdata v)
            {
                v2f o;
                float4 startClip = UnityObjectToClipPos(float4(v.startLocal, 1.0));
                float4 endClip = UnityObjectToClipPos(float4(v.endLocal, 1.0));
                float startDistance = startClip.z - UNITY_NEAR_CLIP_VALUE * startClip.w;
                float endDistance = endClip.z - UNITY_NEAR_CLIP_VALUE * endClip.w;
                bool startVisible = startDistance >= 0.0;
                bool endVisible = endDistance >= 0.0;

                o.visible = 1.0;
                o.lineLengthPixels = 0.0;
                if (!startVisible && !endVisible)
                {
                    o.pos = float4(0, 0, 0, 0);
                    o.corner = v.corner;
                    o.visible = 0.0;
                    return o;
                }

                if (!startVisible)
                {
                    float denominator = startDistance - endDistance;
                    float t = abs(denominator) > 1e-6 ? startDistance / denominator : 0.0;
                    startClip = lerp(startClip, endClip, t);
                }
                else if (!endVisible)
                {
                    float denominator = endDistance - startDistance;
                    float t = abs(denominator) > 1e-6 ? endDistance / denominator : 0.0;
                    endClip = lerp(endClip, startClip, t);
                }

                float startW = max(abs(startClip.w), 1e-6);
                float endW = max(abs(endClip.w), 1e-6);
                float2 startNdc = startClip.xy / startW;
                float2 endNdc = endClip.xy / endW;
                float2 deltaPixels = (endNdc - startNdc) * 0.5 * _Viewport.xy;
                float lengthPixels = max(length(deltaPixels), 1e-4);
                float2 direction = deltaPixels / lengthPixels;
                float2 normal = float2(-direction.y, direction.x);
                float halfWidth = max(_LineWidth, 0.01) * 0.5;
                float along = v.corner.x * 0.5 + 0.5;
                float2 centerNdc = lerp(startNdc, endNdc, along);
                float2 offsetPixels = normal * v.corner.y * halfWidth;
                float2 capPixels = direction * v.corner.x * halfWidth;
                float2 offsetNdc = (offsetPixels + capPixels) * 2.0 / _Viewport.xy;
                float4 centerClip = lerp(startClip, endClip, along);
                centerClip.xy = centerNdc * centerClip.w + offsetNdc * centerClip.w;

                #if defined(UNITY_REVERSED_Z)
                    centerClip.z += _DepthBias * centerClip.w;
                #else
                    centerClip.z -= _DepthBias * centerClip.w;
                #endif

                o.pos = centerClip;
                o.corner = v.corner;
                o.lineLengthPixels = lengthPixels;
                return o;
            }

            fixed4 frag(v2f i) : SV_Target
            {
                if (i.visible < 0.5)
                    discard;

                float radiusPixels = max(_LineWidth, 0.01) * 0.5;
                float halfSegmentPixels = i.lineLengthPixels * 0.5;
                float halfExpandedPixels = halfSegmentPixels + radiusPixels;
                float alongPixels = i.corner.x * halfExpandedPixels;
                float capPixels = max(abs(alongPixels) - halfSegmentPixels, 0.0);
                float sidePixels = abs(i.corner.y) * radiusPixels;
                float distanceToLine = length(float2(capPixels, sidePixels)) - radiusPixels;
                float aa = max(fwidth(distanceToLine), 1e-4);
                float coverage = 1.0 - smoothstep(-aa, aa, distanceToLine);
                return fixed4(_Color.rgb, _Color.a * coverage);
            }
            ENDCG
        }
    }
    Fallback Off
}
