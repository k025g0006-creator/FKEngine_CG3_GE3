#include "object3d.hlsli"

struct Material
{
    float4 color;
    int32_t enableLighting;
    float4x4 uvTransform;
    float32_t shininess; // 光沢度
    float32_t3 specularColor; // 物体の鏡面反射色
    int32_t useBlinnPhong; // 1:Blinn-Phong、0:Phong
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

ConstantBuffer<Material> gMaterial : register(b0);

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

// カメラのワールド位置（C++側のCameraForGPUと並びを合わせる）
struct Camera
{
    float32_t3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);


struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // 物体表面からカメラ（視点）への方向。鏡面反射などで使う
    float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
   
    if (gMaterial.enableLighting != 0)
    {
        float NdoL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = pow(NdoL * 0.5f + 0.5f, 2.0f);
        cos = max(cos, 0.2f); // 最低でも0.2の明るさを保証する（簡易アンビエント）
        
        // 拡散反射
        float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        
        // 鏡面反射の強度を求める
        float32_t3 normal = normalize(input.normal);
        float32_t3 lightDir = normalize(gDirectionalLight.direction);
        float specularPow;
        if (gMaterial.useBlinnPhong != 0)
        {
            // Blinn-Phong。ライトへの方向とカメラへの方向の中間(HalfVector)と、法線の内積で求める
            float32_t3 halfVector = normalize(-lightDir + toEye);
            float NdotH = dot(normal, halfVector);
            specularPow = pow(saturate(NdotH), gMaterial.shininess);
        }
        else
        {
            // Phong。入射光の反射ベクトルと、カメラへの方向の内積で求める
            float32_t3 reflectLight = reflect(lightDir, normal);
            float RdotE = dot(reflectLight, toEye);
            specularPow = pow(saturate(RdotE), gMaterial.shininess);
        }
        // 物体の鏡面反射色はMaterialで設定する
        float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * gMaterial.specularColor;
        
        // 拡散反射 + 鏡面反射
        output.color.rgb = diffuse + specular;
        // アルファは今まで通り
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
