#include "TextureManager.h"
#include "DirectXBase.h"
#include "SRVInformationManager.h"
#include "StringUtility.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <stdexcept>

TextureManager* TextureManager::instance_ = nullptr;

TextureManager* TextureManager::GetInstance()
{
    if (instance_ == nullptr) { instance_ = new TextureManager(); }
    return instance_;
}

void TextureManager::Finalize()
{
    delete instance_;
    instance_ = nullptr;
}

void TextureManager::Initialize(SRVInformationManager& srvManager)
{
    if (directXBase_ || !srvManager.GetDirectXBase() || !srvManager.GetDescriptorHeap()) {
        throw std::invalid_argument("TextureManager requires an initialized SRVInformationManager and one Initialize call.");
    }
    textureDatas_.reserve(SRVInformationManager::kMaxSRVCount);
    textureIndices_.reserve(SRVInformationManager::kMaxSRVCount);
    directXBase_ = srvManager.GetDirectXBase();
    srvManager_ = &srvManager;
}

std::wstring TextureManager::MakePathKey(const std::string& filePath)
{
    if (filePath.empty()) { throw std::invalid_argument("Texture file path is empty."); }
    // 相対/絶対パス、./、区切り文字、Windowsの大小文字の違いを揃える。
    const auto path = std::filesystem::weakly_canonical(
        std::filesystem::absolute(std::filesystem::path(StringUtility::ConvertString(filePath))));
    std::wstring key = path.native();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return key;
}

void TextureManager::LoadTexture(const std::string& filePath)
{
    if (!directXBase_) { throw std::logic_error("Initialize TextureManager before loading textures."); }
    const auto key = MakePathKey(filePath);
    if (textureIndices_.contains(key)) { return; }
    if (!srvManager_->CanAllocate()) {
        throw std::runtime_error("Shared SRV heap capacity exceeded.");
    }

    // DirectXBaseにあった画像デコードとMipMap生成を管理側へ移す。
    DirectX::ScratchImage image;
    const std::wstring widePath = StringUtility::ConvertString(filePath);
    if (FAILED(DirectX::LoadFromWICFile(widePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image))) {
        throw std::runtime_error("Failed to load texture: " + filePath);
    }
    DirectX::ScratchImage mipImages;
    if (FAILED(DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB, 0, mipImages))) {
        throw std::runtime_error("Failed to generate texture mipmaps: " + filePath);
    }

    TextureData data;
    data.filePath = filePath;
    data.metadata = mipImages.GetMetadata();
    data.resource = directXBase_->CreateTextureResource(data.metadata);
    if (!data.resource) { throw std::runtime_error("Failed to create texture resource: " + filePath); }

    // コマンドを積んだだけでは転送は終わらない。完了通知までUploadHeapも所有する。
    // ハッシュ表の割り当てを、GPUへコマンドを積む前に済ませる。
    const auto inserted = textureIndices_.emplace(key, static_cast<uint32_t>(textureDatas_.size()));
    if (!inserted.second) { return; }
    const size_t previousSize = textureDatas_.size();
    try {
        textureDatas_.push_back(std::move(data)); // 最大枚数をreserve済み。
        auto& registered = textureDatas_.back();
        registered.uploadResource = directXBase_->UploadTextureData(registered.resource, mipImages);
        // 空きは先に確認済み。転送準備が失敗した場合はSRV枠を消費しない。
        registered.srvIndex = srvManager_->Allocate();
        srvManager_->CreateSRVForTexture2D(registered.srvIndex, registered.resource.Get(), registered.metadata.format,
            static_cast<UINT>(registered.metadata.mipLevels));
    } catch (...) {
        if (textureDatas_.size() != previousSize) { textureDatas_.pop_back(); }
        textureIndices_.erase(inserted.first);
        throw;
    }
}

const TextureManager::TextureData& TextureManager::FindTexture(const std::string& filePath) const
{
    return textureDatas_.at(GetTextureIndexByFilePath(filePath));
}
D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath) const
{
    const auto srvIndex = GetSrvIndex(filePath);
    return srvManager_->GetGPUDescriptorHandle(srvIndex);
}

uint32_t TextureManager::GetSrvIndex(const std::string& filePath) const
{
    return FindTexture(filePath).srvIndex;
}

uint32_t TextureManager::GetSrvIndex(uint32_t textureIndex) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    return textureDatas_.at(textureIndex).srvIndex;
}

void TextureManager::BindTexture(UINT rootParameterIndex, uint32_t textureIndex) const
{
    const auto srvIndex = GetSrvIndex(textureIndex);
    srvManager_->SetGraphicsRootDescriptorTable(rootParameterIndex, srvIndex);
}

const DirectX::TexMetadata& TextureManager::GetMetadata(const std::string& filePath) const
{
    return FindTexture(filePath).metadata;
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    const auto found = textureIndices_.find(MakePathKey(filePath));
    if (found == textureIndices_.end()) { throw std::out_of_range("Texture is not loaded: " + filePath); }
    return found->second;
}
D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex) const
{
    const auto srvIndex = GetSrvIndex(textureIndex);
    return srvManager_->GetGPUDescriptorHandle(srvIndex);
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureIndex) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    // atでReleaseビルドでも範囲外の番号を拒否する。
    return textureDatas_.at(textureIndex).metadata;
}

ID3D12Resource* TextureManager::GetTextureBuffer(uint32_t textureIndex) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    return textureDatas_.at(textureIndex).resource.Get();
}

const std::string& TextureManager::GetTextureFilePath(uint32_t textureIndex) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    return textureDatas_.at(textureIndex).filePath;
}

size_t TextureManager::GetPendingUploadCount() const
{
    return static_cast<size_t>(std::count_if(textureDatas_.begin(), textureDatas_.end(),
        [](const TextureData& texture) { return texture.uploadResource != nullptr; }));
}

void TextureManager::ReleaseUploadResources()
{
    for (auto& texture : textureDatas_) { texture.uploadResource.Reset(); }
}
