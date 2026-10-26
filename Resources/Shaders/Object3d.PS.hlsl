#include "object3d.hlsli"

struct Material
{
    float4 color;
    int32_t  enableLighting;
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
   
    if (gMaterial.enableLighting != 0)
    {
        float NdoL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = pow(NdoL * 0.5f + 0.5f, 2.0f);
        cos = max(cos, 0.2f); // 最低でも0.2の明るさを保証する（簡易アンビエント）
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }
    
    if (output.color.a == 0.0f)
    {
        discard;
    }
    return output;
}
