#pragma once

#include <bitset>
#include <d3d12.h>
#include <wrl.h>

class DirectXBase;

// ImGui・画像・Structured Bufferが共有するSRVヒープを管理する。
// メインスレッドで利用し、確保した番号は解放するまで有効。
class SRVInformationManager
{
public:
    static constexpr UINT kMaxSRVCount = 512;

    SRVInformationManager() = default;
    SRVInformationManager(const SRVInformationManager&) = delete;
    SRVInformationManager& operator=(const SRVInformationManager&) = delete;

    void Initialize(DirectXBase* directXBase);
    // 利用側の終了とGPU完了待ちの後に呼ぶ。
    void Finalize();
    bool CanAllocate() const { return descriptorHeap_ && !allocated_.all(); }
    UINT Allocate();
    // 利用中のGPUコマンドが完了したSRVを再利用可能にする。
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle);
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(UINT index) const;
    void CreateSRVForTexture2D(UINT index, ID3D12Resource* resource, DXGI_FORMAT format, UINT mipLevels) const;
    void CreateSRVForStructuredBuffer(UINT index, ID3D12Resource* resource, UINT elementCount, UINT elementStride) const;
    // フレームごとに共通ヒープを設定する。
    void PreDraw() const;
    void SetGraphicsRootDescriptorTable(UINT rootParameterIndex, UINT index) const;

    DirectXBase* GetDirectXBase() const { return directXBase_; }
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return descriptorHeap_.Get(); }

private:
    void ValidateIndex(UINT index) const;
    DirectXBase* directXBase_ = nullptr; // 非所有
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
    UINT descriptorSize_ = 0;
    std::bitset<kMaxSRVCount> allocated_;
};
