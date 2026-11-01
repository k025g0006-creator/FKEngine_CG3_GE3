#pragma once

#include "Matrix4x4.h"
#include "Vector4.h"
#include <array>
#include <cstddef>
#include <string>
#include <d3d12.h>
#include <wrl.h>

class SpriteBase;

// 1枚分のデータ。SpriteBaseは外側で、画像リソースとSRVはTextureManagerが保持する。
class Sprite
{
public:
    Sprite() = default;
    Sprite(const Sprite&) = delete;
    Sprite& operator=(const Sprite&) = delete;

    void Initialize(SpriteBase* spriteBase, const std::string& textureFilePath);
    // UINT32_MAXは画像未指定。後でSetTexture/SetTextureIndexから設定できる。
    void Initialize(SpriteBase* spriteBase, uint32_t textureIndex = UINT32_MAX);
    void Update();
    void Draw() const;
    void SetPosition(const Vector2& value) { position_ = value; }
    const Vector2& GetPosition() const { return position_; }
    void SetSize(const Vector2& value) { size_ = value; }
    const Vector2& GetSize() const { return size_; }
    void SetRotation(float value) { rotation_ = value; }
    float GetRotation() const { return rotation_; }
    void SetColor(const std::array<float, 4>& value) { color_ = value; }
    const std::array<float, 4>& GetColor() const { return color_; }
    void SetAnchorPoint(const Vector2& value) { anchorPoint_ = value; }
    const Vector2& GetAnchorPoint() const { return anchorPoint_; }
    void SetFlipX(bool value) { isFlipX_ = value; }
    bool GetFlipX() const { return isFlipX_; }
    void SetFlipY(bool value) { isFlipY_ = value; }
    bool GetFlipY() const { return isFlipY_; }
    // 元画像内のピクセル単位。表示サイズとは独立した切り出し範囲。
    void SetTextureLeftTop(const Vector2& value);
    const Vector2& GetTextureLeftTop() const { return textureLeftTop_; }
    void SetTextureSize(const Vector2& value);
    const Vector2& GetTextureSize() const { return textureSize_; }
    void ResetTextureRegion();
    void SetUVPosition(const Vector2& value) { uvPosition_ = value; }
    const Vector2& GetUVPosition() const { return uvPosition_; }
    void SetUVScale(const Vector2& value) { uvScale_ = value; }
    const Vector2& GetUVScale() const { return uvScale_; }
    void SetUVRotation(float value) { uvRotation_ = value; }
    float GetUVRotation() const { return uvRotation_; }
    void SetTexture(const std::string& filePath);
    void SetTextureIndex(uint32_t textureIndex);
    bool HasTexture() const { return textureIndex_ != UINT32_MAX; }
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    uint32_t GetTextureIndex() const { return textureIndex_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle() const { return textureHandle_; }

private:
    // Sprite専用シェーダーの入力・定数バッファと同じ並びにする。
    struct VertexData
    {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };
    struct Material
    {
        Vector4 color;
        int32_t enableLighting;
        float padding[3];
        Matrix4x4 uvTransform;
    };
    struct TransformationMatrix
    {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };
    static_assert(sizeof(VertexData) == 36);
    static_assert(offsetof(VertexData, normal) == 24);
    static_assert(sizeof(Material) == 96);
    static_assert(offsetof(Material, uvTransform) == 32);
    static_assert(sizeof(TransformationMatrix) == 128);

    void CreateVertexData();
    void CreateMaterialData();
    void CreateTransformationMatrixData();
    void AdjustTextureSize();
    void ClampTextureRegion();

    SpriteBase* spriteBase_ = nullptr; // 所有しない
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle_{};
    std::string textureFilePath_;
    uint32_t textureIndex_ = UINT32_MAX;
    Vector2 position_{ 0.0f, 0.0f };
    Vector2 size_{ 160.0f, 160.0f };
    Vector2 anchorPoint_{ 0.0f, 0.0f };
    bool isFlipX_ = false;
    bool isFlipY_ = false;
    Vector2 textureLeftTop_{ 0.0f, 0.0f };
    Vector2 textureSize_{ 100.0f, 100.0f };
    float rotation_ = 0.0f;
    std::array<float, 4> color_{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vector2 uvPosition_{ 0.0f, 0.0f };
    Vector2 uvScale_{ 1.0f, 1.0f };
    float uvRotation_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    VertexData* vertexData_ = nullptr;
    uint32_t* indexData_ = nullptr;
    Material* materialData_ = nullptr;
    TransformationMatrix* transformationMatrixData_ = nullptr;
};
