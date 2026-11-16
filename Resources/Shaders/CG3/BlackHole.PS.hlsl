struct SpritePixelInput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
    nointerpolation float4 style : TEXCOORD1;
};

static const float kPi = 3.14159265;

float Gaussian(float distance, float width)
{
    const float x = distance / width;
    return exp2(-x * x);
}

float4 DrawCore(float2 local, float3 tint, float time, float energy)
{
    const float radius = length(local);
    const float angle = atan2(local.y, local.x + 0.00001);
    const float horizon = 0.28;
    const float edge = max(fwidth(radius), 0.004);
    const float shadow = 1.0 - smoothstep(horizon - edge, horizon + edge, radius);
    const float outside = smoothstep(horizon, horizon + 0.028, radius);
    const float falloff = 1.0 - smoothstep(0.68, 0.95, radius);

    // Differential winding bends the glowing strands around the event horizon.
    const float turn = angle + 11.0 * (radius - horizon) - time * 2.4;
    const float turbulence = 0.46 * sin(5.0 * angle - time * 1.8 + 17.0 * radius);
    const float arms = pow(0.5 + 0.5 * cos(3.0 * turn + turbulence), 3.0);
    const float strands = 0.5 + 0.5 * cos(52.0 * radius + 7.0 * angle - time * 7.0);
    const float disk = outside * falloff * exp2(-7.0 * max(radius - horizon, 0.0));
    const float3 violet = float3(0.32, 0.025, 0.82);
    const float3 cyan = float3(0.035, 0.58, 1.0);
    const float3 diskTint = lerp(violet, cyan, strands * arms);
    const float3 nebula = disk * (0.09 + arms * (0.27 + 0.16 * strands)) * lerp(diskTint, tint, 0.18);

    const float photonRing = Gaussian(radius - (horizon + 0.022), 0.009 + 0.004 * energy);
    const float crescent = 0.62 + 0.38 * cos(angle - time * 1.7 + 0.8);
    const float3 rim = float3(0.32, 0.68, 1.0) * photonRing * crescent * (0.42 + 0.28 * energy);
    const float lensArc = Gaussian(radius - (0.53 + 0.018 * sin(2.0 * angle - time)), 0.013);
    const float3 halo = float3(0.13, 0.12, 0.6) * lensArc * arms * 0.21;

    // The center attenuates the scene while the surrounding RGB stays emissive.
    return float4(nebula + rim + halo, saturate(shadow * 0.997 + disk * 0.25));
}

float StarDistance(float2 local)
{
    const float sector = kPi * 0.4;
    const float angle = atan2(local.x, -local.y + 0.00001);
    const float folded = abs(frac(angle / sector + 0.5) - 0.5) * sector;
    const float2 sectorPoint = length(local) * float2(cos(folded), sin(folded));
    const float2 tip = float2(0.76, 0.0);
    const float2 notch = float2(0.2589, 0.1881);
    const float2 edge = notch - tip;
    const float2 fromTip = sectorPoint - tip;
    const float2 fromEdge = fromTip - edge * saturate(dot(fromTip, edge) / dot(edge, edge));
    return length(fromEdge) * (edge.x * fromTip.y - edge.y * fromTip.x >= 0.0 ? -1.0 : 1.0);
}

float3 DrawStar(float2 local, float3 tint, float energy)
{
    const float distance = StarDistance(local);
    const float aa = max(fwidth(distance), 0.015);
    const float fill = 1.0 - smoothstep(-aa, aa, distance);
    const float halo = Gaussian(max(distance, 0.0), 0.13) * (1.0 - smoothstep(0.84, 0.99, length(local)));
    const float3 heart = lerp(tint, float3(1.0, 1.0, 1.0), 0.22 + 0.18 * energy);
    return heart * fill * 0.64 + tint * halo * 0.13;
}

float3 DrawSpark(float2 local, float3 tint)
{
    const float r2 = dot(local, local);
    const float halo = exp2(-8.0 * r2) * (1.0 - smoothstep(0.6, 0.97, r2));
    const float center = exp2(-27.0 * r2);
    return tint * halo * 0.34 + lerp(tint, float3(1.0, 0.92, 0.82), 0.34) * center * 0.36;
}

float3 DrawRing(float2 local, float3 tint, float time, float energy)
{
    const float radius = length(local);
    const float angle = atan2(local.y, local.x + 0.00001);
    const float width = max(fwidth(radius), 0.013);
    const float ripple = 0.009 * cos(angle * 17.0 + time * 12.0);
    const float front = Gaussian(radius - 0.76 - ripple, width);
    const float glow = Gaussian(radius - 0.73, 0.078);
    const float echo = Gaussian(radius - 0.64 + ripple, width * 1.45);
    const float texture = 0.7 + 0.3 * cos(angle * 9.0 - time * 8.0);
    const float edge = 1.0 - smoothstep(0.88, 0.99, radius);
    return tint * (front * 0.52 + glow * 0.16 + echo * (0.11 + 0.12 * energy)) * texture * edge;
}

float3 DrawFlash(float2 local, float3 tint, float time)
{
    const float radius = length(local);
    const float angle = atan2(local.y, local.x + 0.00001);
    const float rays = pow(abs(cos(3.0 * angle - 0.8 * time)), 18.0);
    const float aura = exp2(-5.0 * radius * radius);
    const float center = exp2(-28.0 * radius * radius);
    const float edge = 1.0 - smoothstep(0.7, 0.99, radius);
    return (tint * (0.24 * aura + 0.27 * rays * aura) + float3(0.67, 0.8, 0.9) * center * 0.56) * edge;
}

float3 DrawTrail(float2 local, float3 tint)
{
    const float head = saturate(local.x * 0.5 + 0.5);
    const float taper = 0.1 + 0.57 * head;
    const float lengthFade = smoothstep(-0.98, -0.25, local.x) * (1.0 - smoothstep(0.52, 0.99, local.x));
    const float body = 1.0 - smoothstep(0.0, taper, abs(local.y));
    return tint * lengthFade * body * body * (0.14 + 0.3 * head);
}

float4 main(SpritePixelInput input) : SV_Target0
{
    const uint shape = (uint) input.style.x;
    const float time = input.style.y;
    const float energy = saturate(input.style.z);
    const float intensity = saturate(input.color.a);
    const float3 tint = input.color.rgb;
    const float2 local = input.local;

    switch (shape)
    {
        case 0: return DrawCore(local, tint, time, energy) * intensity;
        case 1: return float4(DrawStar(local, tint, energy) * intensity, 0.0);
        case 2: return float4(DrawSpark(local, tint) * intensity, 0.0);
        case 3: return float4(DrawRing(local, tint, time, energy) * intensity, 0.0);
        case 4: return float4(DrawFlash(local, tint, time) * intensity, 0.0);
        case 5: return float4(DrawTrail(local, tint) * intensity, 0.0);
        default: return 0.0;
    }
}
