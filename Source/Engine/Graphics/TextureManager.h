#pragma once

#include <DirectXTex.h>
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>

class DirectXBase;

// ファイルパスとテクスチャを関連付けて共有する。メインスレッドから利用する。
class TextureManager
{
public:
    static TextureManager* GetInstance();
    // Spriteの解放とGPU完了待ちの後、DirectXBaseの解放前に呼ぶ。
    static void Finalize();
    void Initialize(DirectXBase* directXBase);
    void LoadTexture(const std::string& filePath);
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filePath) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(uint32_t textureIndex) const;
    const DirectX::TexMetadata& GetMetadata(const std::string& filePath) const;
    const DirectX::TexMetadata& GetMetadata(uint32_t textureIndex) const;
    uint32_t GetTextureIndexByFilePath(const std::string& filePath) const;
    ID3D12Resource* GetTextureBuffer(uint32_t textureIndex) const;
    const std::string& GetTextureFilePath(uint32_t textureIndex) const;
    size_t GetTextureCount() const { return textureDatas_.size(); }
    size_t GetPendingUploadCount() const;
    // DirectXBase::PostDrawでGPUの完了を待った直後にだけ呼ぶ。
    void ReleaseUploadResources();

private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    struct TextureData
    {
        std::string filePath;
        DirectX::TexMetadata metadata{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
    };
    static std::wstring MakePathKey(const std::string& filePath);
    const TextureData& FindTexture(const std::string& filePath) const;
    static TextureManager* instance_;
    DirectXBase* directXBase_ = nullptr; // mainから借りる。所有しない。
    std::vector<TextureData> textureDatas_;
    std::unordered_map<std::wstring, uint32_t> textureIndices_;
};
