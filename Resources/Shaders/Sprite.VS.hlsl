#include "Sprite.hlsli"

struct SpriteTransformation
{
    float4x4 WVP;
    float4x4 World;
};
ConstantBuffer<SpriteTransformation> gTransformation : register(b0);

struct SpriteVertexInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

SpriteVertexOutput main(SpriteVertexInput input)
{
    SpriteVertexOutput output;
    output.position = mul(input.position, gTransformation.WVP);
    output.texcoord = input.texcoord;
    return output;
}
