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

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 padding;
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float padding;
};

ConstantBuffer<Material> gMaterial : register(b0);

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

// カメラのワールド位置（C++側のCameraForGPUと並びを合わせる）
struct Camera
{
    float32_t3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);

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
        cos = max(cos, 0.15f); // 最低限の環境光。強すぎると全体が白く飽和する
        
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

        // 光源から表面へ向かう入射方向と、表面から光源へ向かう方向を求める。
        float32_t3 pointLightDirection = input.worldPosition - gPointLight.position;
        float pointLightDistance = length(pointLightDirection);
        pointLightDirection /= max(pointLightDistance, 0.0001f);
        float32_t3 toPointLight = -pointLightDirection;
        float pointNdotL = saturate(dot(normal, toPointLight));
        float pointLightRadius = max(gPointLight.radius, 0.0001f);
        float pointAttenuation = pow(
            saturate(1.0f - pointLightDistance / pointLightRadius),
            max(gPointLight.decay, 0.0001f));
        float32_t3 pointDiffuse = gMaterial.color.rgb * textureColor.rgb
            * gPointLight.color.rgb * gPointLight.intensity * pointNdotL * pointAttenuation;

        // PointLightの鏡面反射。DirectionalLightと同じPhong方式を使う。
        float pointSpecularPow;
        if (gMaterial.useBlinnPhong != 0)
        {
            float32_t3 pointHalfVector = normalize(toPointLight + toEye);
            pointSpecularPow = pow(saturate(dot(normal, pointHalfVector)), gMaterial.shininess);
        }
        else
        {
            float32_t3 pointReflectLight = reflect(pointLightDirection, normal);
            pointSpecularPow = pow(saturate(dot(pointReflectLight, toEye)), gMaterial.shininess);
        }
        float32_t3 pointSpecular = gPointLight.color.rgb * gPointLight.intensity
            * pointAttenuation * pointSpecularPow * gMaterial.specularColor;

        // SpotLight: 距離の減衰に加え、中心軸からの角度でも光を減衰させる。
        float32_t3 spotLightDirectionOnSurface = input.worldPosition - gSpotLight.position;
        float spotLightDistance = length(spotLightDirectionOnSurface);
        spotLightDirectionOnSurface /= max(spotLightDistance, 0.0001f);
        float spotCosAngle = dot(spotLightDirectionOnSurface, normalize(gSpotLight.direction));
        float spotConeCos = clamp(gSpotLight.cosAngle, -0.9999f, 0.9999f);
        // Falloff開始角はコーン境界より内側に置き、分母が0にならないようにする。
        float spotFalloffStartCos = clamp(gSpotLight.cosFalloffStart, spotConeCos + 0.0001f, 1.0f);
        float spotFalloffFactor = saturate(
            (spotCosAngle - spotConeCos) / max(spotFalloffStartCos - spotConeCos, 0.0001f));
        float spotRadius = max(gSpotLight.distance, 0.0001f);
        float spotDistanceAttenuation = pow(
            saturate(1.0f - spotLightDistance / spotRadius),
            max(gSpotLight.decay, 0.0001f));
        float spotAttenuation = spotDistanceAttenuation * spotFalloffFactor;
        float32_t3 toSpotLight = -spotLightDirectionOnSurface;
        float spotNdotL = saturate(dot(normal, toSpotLight));
        float32_t3 spotDiffuse = gMaterial.color.rgb * textureColor.rgb
            * gSpotLight.color.rgb * gSpotLight.intensity * spotNdotL * spotAttenuation;

        float spotSpecularPow;
        if (gMaterial.useBlinnPhong != 0)
        {
            float32_t3 spotHalfVector = normalize(toSpotLight + toEye);
            spotSpecularPow = pow(saturate(dot(normal, spotHalfVector)), gMaterial.shininess);
        }
        else
        {
            float32_t3 spotReflectLight = reflect(spotLightDirectionOnSurface, normal);
            spotSpecularPow = pow(saturate(dot(spotReflectLight, toEye)), gMaterial.shininess);
        }
        float32_t3 spotSpecular = gSpotLight.color.rgb * gSpotLight.intensity
            * spotAttenuation * spotSpecularPow * gMaterial.specularColor;
        
        // 各ライトの拡散反射と鏡面反射を合算する。
        output.color.rgb = diffuse + specular + pointDiffuse + pointSpecular + spotDiffuse + spotSpecular;
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
