#include "Particle.hlsli"

// C++側のMaterial構造体と並びを合わせておく（enableLightingはParticleでは使わない）
struct Material
{
    float4 color;
    int32_t enableLighting;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);

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

    // Lightingは行わない
    output.color = gMaterial.color * textureColor * input.color;

    // 最終的なα値が0のときはPixelを棄却する
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
