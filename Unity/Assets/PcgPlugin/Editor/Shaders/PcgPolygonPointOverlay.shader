Shader "Hidden/PcgPolygonPointOverlay"
{
    Properties
    {
        _Color ("Color", Color) = (1, 1, 1, 1)
        _PointSize ("Point Size", Float) = 8
        _UseWorldSize ("Use World Size", Float) = 0
        _Viewport ("Viewport", Vector) = (1, 1, 0, 0)
        [Enum(UnityEngine.Rendering.CompareFunction)] _ZTest ("ZTest", Float) = 4
    }

    SubShader
    {
        Tags { "IgnoreProjector" = "True" "Queue" = "Overlay" }

        Pass
        {
            ZWrite Off
            ZTest [_ZTest]
            // Use the smallest raster depth adjustment for coplanar points.
            Offset 0, -1
            Blend SrcAlpha OneMinusSrcAlpha
            Cull Off

            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float3 centerLocal : POSITION;
                float2 corner : TEXCOORD0;
                fixed4 color : COLOR;
                float pointSize : TEXCOORD1;
            };

            struct v2f
            {
                float4 pos : SV_POSITION;
                noperspective float2 corner : TEXCOORD0;
                fixed4 color : COLOR0;
                float visible : TEXCOORD1;
            };

            float4 _Color;
            float _PointSize;
            float _UseWorldSize;
            float4 _Viewport;

            v2f vert(appdata v)
            {
                v2f o;
                float4 world = mul(unity_ObjectToWorld, float4(v.centerLocal, 1.0));
                float4 centerClip = mul(UNITY_MATRIX_VP, world);
                o.visible = centerClip.w > 0.0 ? 1.0 : 0.0;

                if (_UseWorldSize > 0.5)
                {
                    float3 view = mul(UNITY_MATRIX_V, world).xyz;
                    view.xy += v.corner * v.pointSize * 0.5;
                    o.pos = mul(UNITY_MATRIX_P, float4(view, 1.0));
                }
                else
                {
                    o.pos = centerClip;
                    // _PointSize is the full diameter in physical pixels.
                    o.pos.xy += v.corner * (_PointSize / _Viewport.xy) * centerClip.w;
                }

                o.corner = v.corner;
                o.color = v.color * _Color;
                return o;
            }

            fixed4 frag(v2f i) : SV_Target
            {
                if (i.visible < 0.5)
                    discard;

                float distanceToCircle = length(i.corner) - 1.0;
                float aa = max(fwidth(distanceToCircle), 1e-4);
                float coverage = 1.0 - smoothstep(-aa, aa, distanceToCircle);
                return fixed4(i.color.rgb, i.color.a * coverage);
            }
            ENDCG
        }
    }
    Fallback Off
}
