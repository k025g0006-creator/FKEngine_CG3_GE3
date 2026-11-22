#include "TextureManager.h"
#include "DirectXBase.h"
#include "SRVInformationManager.h"
#include "StringUtility.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <stdexcept>

std::unique_ptr<TextureManager> TextureManager::instance_;

TextureManager* TextureManager::GetInstance()
{
    if (!instance_) { instance_ = std::make_unique<TextureManager>(ConstructorKey{}); }
    return instance_.get();
}

void TextureManager::Finalize()
{
    if (instance_ && instance_->srvManager_) {
        auto& manager = *instance_;
        manager.ReleaseCompletedResources();
        for (const auto& entry : manager.textureDatas_) {
            manager.srvManager_->Free(manager.srvManager_->GetCPUDescriptorHandle(entry.second.srvIndex));
        }
    }
    instance_.reset();
}

void TextureManager::Initialize(SRVInformationManager& srvManager)
{
    if (directXBase_ || !srvManager.GetDirectXBase() || !srvManager.GetDescriptorHeap()) {
        throw std::invalid_argument("TextureManager requires an initialized SRVInformationManager and one Initialize call.");
    }
    textureDatas_.reserve(SRVInformationManager::kMaxSRVCount);
    textureIndices_.reserve(SRVInformationManager::kMaxSRVCount);
    retiredTextures_.reserve(SRVInformationManager::kMaxSRVCount);
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
    if (nextTextureIndex_ == UINT32_MAX) { throw std::overflow_error("Texture indices exhausted."); }
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
    data.pathKey = key;
    data.metadata = mipImages.GetMetadata();
    data.resource = directXBase_->CreateTextureResource(data.metadata);
    if (!data.resource) { throw std::runtime_error("Failed to create texture resource: " + filePath); }

    // コマンドを積んだだけでは転送は終わらない。完了通知までUploadHeapも所有する。
    // ハッシュ表の割り当てを、GPUへコマンドを積む前に済ませる。
    const auto textureIndex = nextTextureIndex_;
    const auto inserted = textureIndices_.emplace(key, textureIndex);
    if (!inserted.second) { return; }
    auto registered = textureDatas_.end();
    try {
        registered = textureDatas_.emplace(textureIndex, std::move(data)).first;
        auto& texture = registered->second;
        texture.srvIndex = srvManager_->Allocate();
        srvManager_->CreateSRVForTexture2D(texture.srvIndex, texture.resource.Get(), texture.metadata.format,
            static_cast<UINT>(texture.metadata.mipLevels));
        texture.uploadResource = directXBase_->UploadTextureData(texture.resource, mipImages);
        ++nextTextureIndex_;
    } catch (...) {
        if (registered != textureDatas_.end()) {
            const auto srvIndex = registered->second.srvIndex;
            if (srvIndex != UINT32_MAX) { srvManager_->Free(srvManager_->GetCPUDescriptorHandle(srvIndex)); }
            textureDatas_.erase(registered);
        }
        textureIndices_.erase(inserted.first);
        throw;
    }
}

bool TextureManager::UnloadTexture(uint32_t textureIndex)
{
    const auto found = textureDatas_.find(textureIndex);
    if (found == textureDatas_.end()) { return false; }

    auto& retired = retiredTextures_.emplace_back(std::move(found->second));
    textureIndices_.erase(retired.pathKey);
    textureDatas_.erase(found);
    return true;
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
    const auto active = std::ranges::count_if(textureDatas_,
        [](const auto& entry) { return entry.second.uploadResource != nullptr; });
    const auto retired = std::ranges::count_if(retiredTextures_,
        [](const TextureData& texture) { return texture.uploadResource != nullptr; });
    return static_cast<size_t>(active + retired);
}

void TextureManager::ReleaseCompletedResources()
{
    for (auto& entry : textureDatas_) { entry.second.uploadResource.Reset(); }
    for (const auto& texture : retiredTextures_) {
        srvManager_->Free(srvManager_->GetCPUDescriptorHandle(texture.srvIndex));
    }
    retiredTextures_.clear();
}
