#include "Sprite.h"
#include "SpriteBase.h"
#include "DirectXBase.h"
#include "Transform.h"
#include "TextureManager.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace
{
void MapSpriteResource(ID3D12Resource* resource, void** address)
{
    const D3D12_RANGE readRange{ 0, 0 };
    if (resource == nullptr || FAILED(resource->Map(0, &readRange, address))) {
        throw std::runtime_error("Failed to map Sprite buffer.");
    }
}
}

void Sprite::Initialize(SpriteBase* spriteBase, const std::string& textureFilePath)
{
    if (!spriteBase || !spriteBase->GetDirectXBase() || textureFilePath.empty() || spriteBase_) {
        throw std::invalid_argument("Sprite requires an initialized SpriteBase and a texture file path.");
    }
    auto* textures = TextureManager::GetInstance();
    textures->LoadTexture(textureFilePath);
    Initialize(spriteBase, textures->GetTextureIndexByFilePath(textureFilePath));
}

void Sprite::Initialize(SpriteBase* spriteBase, uint32_t textureIndex)
{
    if (!spriteBase || !spriteBase->GetDirectXBase() || spriteBase_) {
        throw std::invalid_argument("Sprite requires an initialized SpriteBase and one Initialize call.");
    }
    // 共通部は借りるだけ。各Spriteが所有するのは下で作成するバッファ。
    spriteBase_ = spriteBase;
    try {
        SetTextureIndex(textureIndex);
        CreateVertexData();
        CreateMaterialData();
        CreateTransformationMatrixData();
        if (HasTexture()) { AdjustTextureSize(); }
        Update();
    } catch (...) {
        transformationResource_.Reset();
        materialResource_.Reset();
        indexResource_.Reset();
        vertexResource_.Reset();
        vertexData_ = nullptr;
        indexData_ = nullptr;
        materialData_ = nullptr;
        transformationMatrixData_ = nullptr;
        textureHandle_ = {};
        textureFilePath_.clear();
        textureIndex_ = UINT32_MAX;
        spriteBase_ = nullptr;
        vertexDirty_ = transformDirty_ = materialDirty_ = true;
        projectionRevision_ = 0;
        throw;
    }
}

void Sprite::SetTexture(const std::string& filePath)
{
    auto* textures = TextureManager::GetInstance();
    textures->LoadTexture(filePath);
    SetTextureIndex(textures->GetTextureIndexByFilePath(filePath));
}

void Sprite::SetTextureIndex(uint32_t textureIndex)
{
    if (textureIndex_ == textureIndex) { return; }
    if (textureIndex == UINT32_MAX) {
        textureIndex_ = UINT32_MAX;
        textureHandle_ = {};
        textureFilePath_.clear();
        textureImageSize_ = { 1.0f, 1.0f };
        materialDirty_ = true;
        return;
    }
    auto* textures = TextureManager::GetInstance();
    // 範囲チェックを済ませてから状態を変更する。無効な番号でも元の画像は保持する。
    const std::string path = textures->GetTextureFilePath(textureIndex);
    const auto handle = textures->GetSrvHandleGPU(textureIndex);
    const auto desc = textures->GetTextureBuffer(textureIndex)->GetDesc();
    textureFilePath_ = path;
    textureIndex_ = textureIndex;
    textureHandle_ = handle;
    textureImageSize_ = { static_cast<float>(desc.Width), static_cast<float>(desc.Height) };
    materialDirty_ = true;
    // 後から変更する場合、表示サイズは保ち、切り出しだけを全体へ戻す。
    ResetTextureRegion();
}
void Sprite::ResetTextureRegion()
{
    if (!HasTexture()) { return; }
    if (textureLeftTop_.x == 0.0f && textureLeftTop_.y == 0.0f &&
        textureSize_.x == textureImageSize_.x && textureSize_.y == textureImageSize_.y) { return; }
    textureLeftTop_ = { 0.0f, 0.0f };
    textureSize_ = textureImageSize_;
    materialDirty_ = true;
}

void Sprite::AdjustTextureSize()
{
    ResetTextureRegion();
    SetSize(textureSize_);
}
void Sprite::ClampTextureRegion()
{
    if (!HasTexture()) { return; }
    const float width = textureImageSize_.x;
    const float height = textureImageSize_.y;
    textureLeftTop_.x = std::clamp(textureLeftTop_.x, 0.0f, width - 1.0f);
    textureLeftTop_.y = std::clamp(textureLeftTop_.y, 0.0f, height - 1.0f);
    textureSize_.x = std::clamp(textureSize_.x, 1.0f, width - textureLeftTop_.x);
    textureSize_.y = std::clamp(textureSize_.y, 1.0f, height - textureLeftTop_.y);
}
void Sprite::SetTextureLeftTop(const Vector2& value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        throw std::invalid_argument("Texture crop position must be finite.");
    }
    const Vector2 previousPosition = textureLeftTop_;
    const Vector2 previousSize = textureSize_;
    textureLeftTop_ = value;
    ClampTextureRegion();
    if (textureLeftTop_.x != previousPosition.x || textureLeftTop_.y != previousPosition.y ||
        textureSize_.x != previousSize.x || textureSize_.y != previousSize.y) { materialDirty_ = true; }
}

void Sprite::SetTextureSize(const Vector2& value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        throw std::invalid_argument("Texture crop size must be finite.");
    }
    const Vector2 previousPosition = textureLeftTop_;
    const Vector2 previousSize = textureSize_;
    textureSize_ = value;
    ClampTextureRegion();
    if (textureLeftTop_.x != previousPosition.x || textureLeftTop_.y != previousPosition.y ||
        textureSize_.x != previousSize.x || textureSize_.y != previousSize.y) { materialDirty_ = true; }
}

void Sprite::CreateVertexData()
{
    auto* directX = spriteBase_->GetDirectXBase();
    vertexResource_ = directX->CreateBufferResource(sizeof(VertexData) * 4);
    indexResource_ = directX->CreateBufferResource(sizeof(uint32_t) * 6);
    MapSpriteResource(vertexResource_.Get(), reinterpret_cast<void**>(&vertexData_));
    MapSpriteResource(indexResource_.Get(), reinterpret_cast<void**>(&indexData_));
    // 接続順は不変なので、6個のインデックスは初期化時に1度だけ書く。
    const uint32_t indices[] = { 0, 1, 2, 2, 1, 3 };
    std::memcpy(indexData_, indices, sizeof(indices));
    vertexBufferView_ = { vertexResource_->GetGPUVirtualAddress(), sizeof(VertexData) * 4, sizeof(VertexData) };
    indexBufferView_ = { indexResource_->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R32_UINT };
}

void Sprite::CreateMaterialData()
{
    // 定数バッファの配置境界は256byte。
    materialResource_ = spriteBase_->GetDirectXBase()->CreateBufferResource(256);
    MapSpriteResource(materialResource_.Get(), reinterpret_cast<void**>(&materialData_));
    *materialData_ = {};
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = 0;
    materialData_->uvTransform = Math::MakeIdentity4x4();
}

void Sprite::CreateTransformationMatrixData()
{
    transformationResource_ = spriteBase_->GetDirectXBase()->CreateBufferResource(256);
    MapSpriteResource(transformationResource_.Get(), reinterpret_cast<void**>(&transformationMatrixData_));
    transformationMatrixData_->WVP = Math::MakeIdentity4x4();
    transformationMatrixData_->World = Math::MakeIdentity4x4();
}

bool Sprite::IsDirty() const
{
    return vertexDirty_ || transformDirty_ || materialDirty_ ||
        (spriteBase_ && projectionRevision_ != spriteBase_->GetProjectionRevision());
}

const Matrix4x4& Sprite::GetUVTransform() const
{
    if (!materialData_) { throw std::logic_error("Initialize and update Sprite before reading the UV transform."); }
    return materialData_->uvTransform;
}

void Sprite::Update()
{
    if (!spriteBase_) { throw std::logic_error("Initialize Sprite before updating."); }
    if (projectionRevision_ != spriteBase_->GetProjectionRevision()) { transformDirty_ = true; }
    // 変更がなければMap済みメモリへの書き込みも行列演算も行わない。
    if (!IsDirty()) { return; }
    if (vertexDirty_) { UpdateVertices(); vertexDirty_ = false; }
    if (transformDirty_) {
        UpdateTransformation();
        projectionRevision_ = spriteBase_->GetProjectionRevision();
        transformDirty_ = false;
    }
    if (materialDirty_) { UpdateMaterial(); materialDirty_ = false; }
}

void Sprite::UpdateVertices()
{
    float left = -anchorPoint_.x * size_.x;
    float top = -anchorPoint_.y * size_.y;
    float right = left + size_.x;
    float bottom = top + size_.y;
    if (isFlipX_) { left = -left; right = -right; }
    if (isFlipY_) { top = -top; bottom = -bottom; }
    // 頂点UVは常に0～1。切り出しを含むUV変形はマテリアルだけが担当する。
    vertexData_[0] = { { left, bottom, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    vertexData_[1] = { { right, bottom, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    vertexData_[2] = { { left, top, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } };
    vertexData_[3] = { { right, top, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } };
}

void Sprite::UpdateTransformation()
{
    const FKEngine::Transform transform{
        { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, rotation_ }, { position_.x, position_.y, 0.0f }
    };
    const Matrix4x4 world = Math::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    transformationMatrixData_->WVP = Math::Multiply(world, spriteBase_->GetProjectionMatrix());
    transformationMatrixData_->World = world;
}

void Sprite::UpdateMaterial()
{
    Matrix4x4 crop = Math::MakeIdentity4x4();
    if (HasTexture()) {
        crop = Math::Multiply(
            Math::MakeScaleMatrix({ textureSize_.x / textureImageSize_.x, textureSize_.y / textureImageSize_.y, 1.0f }),
            Math::MakeTranslateMatrix({ textureLeftTop_.x / textureImageSize_.x, textureLeftTop_.y / textureImageSize_.y, 0.0f }));
    }
    const Matrix4x4 extra = Math::Multiply(
        Math::Multiply(Math::MakeScaleMatrix({ uvScale_.x, uvScale_.y, 1.0f }), Math::MakeRotateZMatrix(uvRotation_)),
        Math::MakeTranslateMatrix({ uvPosition_.x, uvPosition_.y, 0.0f }));
    // 行ベクトル：固定UV → 切り出し → 詳細UV変形。従来と同じ順序を1つの行列に合成する。
    materialData_->uvTransform = Math::Multiply(crop, extra);
    materialData_->color = { color_[0], color_[1], color_[2], color_[3] };
}
void Sprite::Draw() const
{
    if (!spriteBase_) { throw std::logic_error("Initialize Sprite before drawing."); }
    if (!HasTexture()) { return; } // 未指定のSRVをGPUへバインドしない。
    auto* commandList = spriteBase_->GetDirectXBase()->GetCommandList().Get();
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, transformationResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, textureHandle_);
    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}
