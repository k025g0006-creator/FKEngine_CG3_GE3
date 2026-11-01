#include "TextureManager.h"
#include "DirectXBase.h"
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

void TextureManager::Initialize(DirectXBase* directXBase)
{
    if (!directXBase || !directXBase->GetDevice() || !directXBase->GetSRVDescriptorHeap() || directXBase_) {
        throw std::invalid_argument("TextureManager requires an initialized DirectXBase and one Initialize call.");
    }
    textureDatas_.reserve(DirectXBase::kMaxSRVCount - DirectXBase::kFirstTextureSRVIndex);
    directXBase_ = directXBase;
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
    const auto loaded = std::find_if(textureDatas_.begin(), textureDatas_.end(),
        [&](const TextureData& texture) { return texture.pathKey == key; });
    if (loaded != textureDatas_.end()) { return; }
    if (textureDatas_.size() >= DirectXBase::kMaxSRVCount - DirectXBase::kFirstTextureSRVIndex) {
        throw std::runtime_error("TextureManager SRV capacity exceeded.");
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
    const UINT descriptorIndex = DirectXBase::kFirstTextureSRVIndex + static_cast<UINT>(textureDatas_.size());
    const UINT descriptorSize = directXBase_->GetSRVDescriptorSize();
    data.srvHandleCPU = directXBase_->GetSRVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    data.srvHandleGPU = directXBase_->GetSRVDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
    data.srvHandleCPU.ptr += static_cast<SIZE_T>(descriptorIndex) * descriptorSize;
    data.srvHandleGPU.ptr += static_cast<UINT64>(descriptorIndex) * descriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = data.metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(data.metadata.mipLevels);
    directXBase_->GetDevice()->CreateShaderResourceView(data.resource.Get(), &srvDesc, data.srvHandleCPU);

    // コマンドを積んだだけでは転送は終わらない。完了通知までUploadHeapも所有する。
    data.uploadResource = directXBase_->UploadTextureData(data.resource, mipImages);
    textureDatas_.push_back(std::move(data));
}

const TextureManager::TextureData& TextureManager::FindTexture(const std::string& filePath) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    const auto key = MakePathKey(filePath);
    const auto found = std::find_if(textureDatas_.begin(), textureDatas_.end(),
        [&](const TextureData& texture) { return texture.pathKey == key; });
    if (found == textureDatas_.end()) { throw std::out_of_range("Texture is not loaded: " + filePath); }
    return *found;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath) const
{
    return FindTexture(filePath).srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(const std::string& filePath) const
{
    return FindTexture(filePath).metadata;
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath) const
{
    const TextureData& texture = FindTexture(filePath);
    return static_cast<uint32_t>(&texture - textureDatas_.data());
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex) const
{
    if (!directXBase_) { throw std::logic_error("TextureManager is not initialized."); }
    return textureDatas_.at(textureIndex).srvHandleGPU;
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
