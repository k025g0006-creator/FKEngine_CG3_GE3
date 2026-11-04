#pragma once

#include <array>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include "BlendMode.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "Vector4.h"

class DirectXBase;
class Model;

// 配置・変換・ライトと共通描画設定を担当する。見た目はModelを参照する。
// ルートシグネチャとPSOは同じデバイスのインスタンス間で共有し、画像はTextureManagerから借りる。
class Object3DBase
{
public:
    struct TransformationMatrix
    {
        Matrix4x4 WVP = Math::MakeIdentity4x4();
        Matrix4x4 World = Math::MakeIdentity4x4();
        Matrix4x4 WorldInverseTranspose = Math::MakeIdentity4x4();
    };
    struct DirectionalLight
    {
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector3 direction{ 0.0f, -1.0f, 1.0f };
        float intensity = 0.0f;
    };
    struct PointLight
    {
        Vector4 color{ 1.0f, 0.85f, 0.65f, 1.0f };
        Vector3 position{ 0.0f, 2.0f, -3.0f };
        float intensity = 0.0f;
        float radius = 10.0f;
        float decay = 2.0f;
        float padding[2]{};
    };
    struct SpotLight
    {
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector3 position{ 2.0f, 1.25f, 0.0f };
        float intensity = 4.0f;
        Vector3 direction{ -0.70710678f, -0.70710678f, 0.0f };
        float distance = 7.0f;
        float decay = 2.0f;
        float cosAngle = 0.5f;
        float cosFalloffStart = 0.8660254f;
        float padding[1]{};
    };
    struct CameraForGPU
    {
        Vector3 worldPosition{};
        float padding = 0.0f;
    };
    static_assert(sizeof(TransformationMatrix) == 192);
    static_assert(sizeof(DirectionalLight) == 32);
    static_assert(sizeof(PointLight) == 48);
    static_assert(sizeof(SpotLight) == 64);
    static_assert(sizeof(CameraForGPU) == 16);

    Object3DBase() = default;
    Object3DBase(const Object3DBase&) = delete;
    Object3DBase& operator=(const Object3DBase&) = delete;

    void Initialize(DirectXBase* directXBase);
    // GPU完了待ち後、TextureManager・DirectXBaseの終了前に呼ぶ。
    void Finalize();
    void Update();
    // デバッグカメラなど、アプリ側のビュー・投影行列を使う場合。
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);
    // PreDraw後、全3DオブジェクトのDrawの直前に1回呼ぶ。
    void SetCommonRenderState(BlendMode blendMode = kBlendModeNone) const;
    // 共通描画設定後に呼ぶ。個別のバッファと画像を設定して描画する。
    void Draw() const;
    // Modelは借りる。参照する全Objectより長く保持する。nullptrで描画を解除する。
    void SetModel(Model* model);
    // 読込済みモデルをパスで割り当てる。未登録ならnullptrとなり描画しない。
    void SetModel(const std::string& filePath);
    Model* GetModel() const { return model_; }
    void SetScale(const Vector3& value) { transform_.scale = value; }
    void SetRotate(const Vector3& value) { transform_.rotate = value; }
    void SetTranslate(const Vector3& value) { transform_.translate = value; }
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotate() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }

    DirectXBase* GetDirectXBase() const { return directXBase_; }
    FKEngine::Transform& GetTransform() { return transform_; }
    const FKEngine::Transform& GetTransform() const { return transform_; }
    FKEngine::Transform& GetCameraTransform() { return cameraTransform_; }
    const FKEngine::Transform& GetCameraTransform() const { return cameraTransform_; }
    DirectionalLight& GetDirectionalLight() { return directionalLight_; }
    const DirectionalLight& GetDirectionalLight() const { return directionalLight_; }
    PointLight& GetPointLight() { return pointLight_; }
    const PointLight& GetPointLight() const { return pointLight_; }
    SpotLight& GetSpotLight() { return spotLight_; }
    const SpotLight& GetSpotLight() const { return spotLight_; }
    const TransformationMatrix& GetTransformationMatrix() const { return transformationMatrix_; }

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState();
    void CreateConstantBuffers();

    struct RenderState
    {
        ID3D12Device* device = nullptr; // RootSignature/PSOがデバイスの寿命を保持する
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates;
    };
    static std::weak_ptr<RenderState> sharedRenderState_;
    std::shared_ptr<RenderState> renderState_;
    DirectXBase* directXBase_ = nullptr; // 非所有
    Model* model_ = nullptr; // 非所有
    FKEngine::Transform transform_{ {1.0f, 1.0f, 1.0f}, {}, {0.0f, 1.0f, 0.0f} };
    FKEngine::Transform cameraTransform_{ {1.0f, 1.0f, 1.0f}, {0.38f, 0.0f, 0.0f}, {0.0f, 6.0f, -16.0f} };
    TransformationMatrix transformationMatrix_;
    DirectionalLight directionalLight_;
    PointLight pointLight_;
    SpotLight spotLight_;
    CameraForGPU camera_;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    TransformationMatrix* transformationData_ = nullptr;
    DirectionalLight* directionalLightData_ = nullptr;
    CameraForGPU* cameraData_ = nullptr;
    PointLight* pointLightData_ = nullptr;
    SpotLight* spotLightData_ = nullptr;
};
