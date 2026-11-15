#include "Sprite.hlsli"

struct SpriteMaterial
{
    float4 color;
    int32_t enableLighting; // Spriteは照明なし。CPU側では0で初期化する。
    float3 padding;
    float4x4 uvTransform;
};
ConstantBuffer<SpriteMaterial> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    const float2 uv = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    return gMaterial.color * gTexture.Sample(gSampler, uv);
}
