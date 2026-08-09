Shader "Hidden/PCG/Tests/MaterialProperties"
{
    Properties
    {
        _Color ("Tint", Color) = (1, 1, 1, 1)
        _TestVector ("Direction", Vector) = (0, 1, 0, 0)
        _TestFloat ("Strength", Float) = 0.25
        _TestRange ("Coat", Range(0, 2)) = 0.5
        _TestInt ("Layer", Int) = 1
        _MainTex ("Main Texture", 2D) = "white" {}
        [HideInInspector] _HiddenValue ("Hidden Value", Float) = 9
    }

    SubShader
    {
        Tags { "RenderType" = "Opaque" }
        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float4 vertex : SV_POSITION;
                float2 uv : TEXCOORD0;
            };

            sampler2D _MainTex;
            float4 _MainTex_ST;
            float4 _Color;

            v2f vert(appdata input)
            {
                v2f output;
                output.vertex = UnityObjectToClipPos(input.vertex);
                output.uv = TRANSFORM_TEX(input.uv, _MainTex);
                return output;
            }

            fixed4 frag(v2f input) : SV_Target
            {
                return tex2D(_MainTex, input.uv) * _Color;
            }
            ENDCG
        }
    }
}
