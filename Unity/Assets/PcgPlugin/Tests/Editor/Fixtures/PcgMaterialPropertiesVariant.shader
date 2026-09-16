Shader "Hidden/PCG/Tests/MaterialPropertiesVariant"
{
    Properties
    {
        _Color ("Variant Tint", Color) = (0.5, 0.5, 0.5, 1)
        _TestFloat ("Variant Strength", Float) = 0.75
        _TestRange ("Now A Float", Float) = 1.25
        _MainTex ("Variant Texture", 2D) = "white" {}
    }

    SubShader
    {
        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            float4 vert(float4 vertex : POSITION) : SV_POSITION
            {
                return UnityObjectToClipPos(vertex);
            }

            fixed4 frag() : SV_Target
            {
                return fixed4(1, 1, 1, 1);
            }
            ENDCG
        }
    }
}
