Shader "Hidden/PcgPolygonWireOverlay"
{
    // Screen-constant wire quads with Blender-style edge AA (overlay_edit_mesh_frag + LINE_SMOOTH_*).
    SubShader
    {
        Tags { "IgnoreProjector" = "True" "Queue" = "Overlay" }

        Pass
        {
            ZWrite Off
            ZTest LEqual
            Blend SrcAlpha OneMinusSrcAlpha
            Cull Off

            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 edgeCoord : TEXCOORD1;
            };

            struct v2f
            {
                float4 pos : SV_POSITION;
                noperspective float edgeCoord : TEXCOORD0;
            };

            float _HalfPx;

            v2f vert(appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.edgeCoord = v.edgeCoord.x;
                return o;
            }

            fixed4 frag(v2f i) : SV_Target
            {
                // overlay_shader_shared.hh: DISC_RADIUS = M_1_SQRTPI * 1.05
                const float lineSmoothStart = 0.5 - 0.592;
                const float lineSmoothEnd = 0.5 + 0.592;
                float dist = abs(i.edgeCoord) - max(_HalfPx - 0.5, 0.0);
                float mixW = smoothstep(lineSmoothStart, lineSmoothEnd, dist);
                return fixed4(0, 0, 0, 1.0 - mixW);
            }
            ENDCG
        }
    }
    Fallback Off
}
