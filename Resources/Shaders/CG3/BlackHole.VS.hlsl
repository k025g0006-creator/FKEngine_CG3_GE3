struct BlackHoleInstance
{
    row_major float4x4 wvp;
    float4 color;
    float4 style; // Shape, time, energy, reserved
};

StructuredBuffer<BlackHoleInstance> gInstances : register(t0);

struct SpritePixelInput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
    nointerpolation float4 style : TEXCOORD1;
};

SpritePixelInput main(float4 position : POSITION0, uint instanceId : SV_InstanceID)
{
    const BlackHoleInstance instance = gInstances[instanceId];
    SpritePixelInput output;
    output.position = mul(position, instance.wvp);
    output.local = position.xy;
    output.color = instance.color;
    output.style = instance.style;
    return output;
}
