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
    if (textureIndex == UINT32_MAX) {
        textureIndex_ = UINT32_MAX;
        textureHandle_ = {};
        textureFilePath_.clear();
        return;
    }
    auto* textures = TextureManager::GetInstance();
    // 範囲チェックを済ませてから状態を変更する。無効な番号でも元の画像は保持する。
    const std::string path = textures->GetTextureFilePath(textureIndex);
    const auto handle = textures->GetSrvHandleGPU(textureIndex);
    textureFilePath_ = path;
    textureIndex_ = textureIndex;
    textureHandle_ = handle;
    // 後から変更する場合、表示サイズは保ち、切り出しだけを全体へ戻す。
    ResetTextureRegion();
}
void Sprite::ResetTextureRegion()
{
    if (!HasTexture()) { return; }
    ID3D12Resource* texture = TextureManager::GetInstance()->GetTextureBuffer(textureIndex_);
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    textureLeftTop_ = { 0.0f, 0.0f };
    textureSize_ = { static_cast<float>(desc.Width), static_cast<float>(desc.Height) };
}

void Sprite::AdjustTextureSize()
{
    ResetTextureRegion();
    size_ = textureSize_;
}

void Sprite::ClampTextureRegion()
{
    if (!HasTexture()) { return; }
    ID3D12Resource* texture = TextureManager::GetInstance()->GetTextureBuffer(textureIndex_);
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    const float width = static_cast<float>(desc.Width);
    const float height = static_cast<float>(desc.Height);
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
    textureLeftTop_ = value;
    ClampTextureRegion();
}

void Sprite::SetTextureSize(const Vector2& value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        throw std::invalid_argument("Texture crop size must be finite.");
    }
    textureSize_ = value;
    ClampTextureRegion();
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

void Sprite::Update()
{
    if (!spriteBase_) { throw std::logic_error("Initialize Sprite before updating."); }
    float left = -anchorPoint_.x * size_.x;
    float top = -anchorPoint_.y * size_.y;
    float right = left + size_.x;
    float bottom = top + size_.y;
    // 原点（アンカーポイント）を基準に頂点を反転する。
    if (isFlipX_) { left = -left; right = -right; }
    if (isFlipY_) { top = -top; bottom = -bottom; }
    float texLeft = 0.0f;
    float texRight = 1.0f;
    float texTop = 0.0f;
    float texBottom = 1.0f;
    if (HasTexture()) {
        ID3D12Resource* texture = TextureManager::GetInstance()->GetTextureBuffer(textureIndex_);
        const D3D12_RESOURCE_DESC desc = texture->GetDesc();
        // 元画像のピクセル範囲を、画像全体が1となるUV座標へ変換する。
        texLeft = textureLeftTop_.x / static_cast<float>(desc.Width);
        texRight = (textureLeftTop_.x + textureSize_.x) / static_cast<float>(desc.Width);
        texTop = textureLeftTop_.y / static_cast<float>(desc.Height);
        texBottom = (textureLeftTop_.y + textureSize_.y) / static_cast<float>(desc.Height);
    }
    vertexData_[0] = { { left, bottom, 0.0f, 1.0f }, { texLeft, texBottom }, { 0.0f, 0.0f, -1.0f } };
    vertexData_[1] = { { right, bottom, 0.0f, 1.0f }, { texRight, texBottom }, { 0.0f, 0.0f, -1.0f } };
    vertexData_[2] = { { left, top, 0.0f, 1.0f }, { texLeft, texTop }, { 0.0f, 0.0f, -1.0f } };
    vertexData_[3] = { { right, top, 0.0f, 1.0f }, { texRight, texTop }, { 0.0f, 0.0f, -1.0f } };

    const FKEngine::Transform transform{
        { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, rotation_ }, { position_.x, position_.y, 0.0f }
    };
    const Matrix4x4 world = Math::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    const Matrix4x4 view = Math::MakeIdentity4x4(); // Spriteはカメラの影響を受けない。
    const Matrix4x4 projection = Math::MakeOrthographicMatrix(0.0f, 0.0f,
        spriteBase_->GetScreenWidth(), spriteBase_->GetScreenHeight(), 0.0f, 100.0f);
    transformationMatrixData_->WVP = Math::Multiply(world, Math::Multiply(view, projection));
    transformationMatrixData_->World = world;
    materialData_->color = { color_[0], color_[1], color_[2], color_[3] };
    materialData_->uvTransform = Math::Multiply(Math::Multiply(Math::MakeScaleMatrix({ uvScale_.x, uvScale_.y, 1.0f }),
        Math::MakeRotateZMatrix(uvRotation_)), Math::MakeTranslateMatrix({ uvPosition_.x, uvPosition_.y, 0.0f }));
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
