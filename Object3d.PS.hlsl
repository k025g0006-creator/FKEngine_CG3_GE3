#include "object3d.hlsli"

struct Material
{
    float4 color;
    int32_t lightingMode;
    float4x4 uvTransform;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

ConstantBuffer<Material> gMaterial : register(b0);

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);


struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
   
    if (gMaterial.lightingMode != 0)
    {
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = 0.0f;

        if (gMaterial.lightingMode == 1)
        {
            // Lambert : 通常のランバート反射（負の値は0にクランプ）
            cos = saturate(NdotL);
        }
        else
        {
            // HalfLambert : 陰影を滑らかにする半球ランバート反射
            cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        }

        cos = max(cos, 0.2f); // 最低でも0.2の明るさを保証する（簡易アンビエント）
        output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
    }
    else
    {
        // None : ライティングを適用しない
        output.color = gMaterial.color * textureColor;
    }
    return output;
}