#include "SRVInformationManager.h"
#include "DirectXBase.h"
#include "ResourceObject.h"
#include <stdexcept>

void SRVInformationManager::Initialize(DirectXBase* directXBase)
{
    if (descriptorHeap_ || !directXBase || !directXBase->GetDevice() || !directXBase->GetCommandList()) {
        throw std::invalid_argument("SRVInformationManager requires an initialized DirectXBase and one Initialize call.");
    }
    descriptorHeap_ = CreateDescriptorHeap(directXBase->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        kMaxSRVCount, true);
    if (!descriptorHeap_) { throw std::runtime_error("Failed to create the shared SRV heap."); }
    descriptorSize_ = directXBase->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    directXBase_ = directXBase;
}

void SRVInformationManager::Finalize()
{
    descriptorHeap_.Reset();
    descriptorSize_ = 0;
    allocated_.reset();
    directXBase_ = nullptr;
}

UINT SRVInformationManager::Allocate()
{
    if (!descriptorHeap_) { throw std::logic_error("Initialize SRVInformationManager before allocating."); }
    for (UINT index = 0; index < kMaxSRVCount; ++index) {
        if (!allocated_.test(index)) {
            allocated_.set(index);
            return index;
        }
    }
    throw std::runtime_error("Shared SRV heap capacity exceeded.");
}

void SRVInformationManager::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    if (!descriptorHeap_) { throw std::logic_error("SRVInformationManager is not initialized."); }
    const auto start = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    if (handle.ptr < start.ptr) { throw std::out_of_range("SRV handle is outside the shared heap."); }
    const auto offset = handle.ptr - start.ptr;
    if (offset % descriptorSize_ != 0 || offset / descriptorSize_ >= kMaxSRVCount) {
        throw std::out_of_range("SRV handle is outside the shared heap or misaligned.");
    }
    const auto index = static_cast<UINT>(offset / descriptorSize_);
    ValidateIndex(index);
    allocated_.reset(index);
}

void SRVInformationManager::ValidateIndex(UINT index) const
{
    if (!descriptorHeap_) { throw std::logic_error("SRVInformationManager is not initialized."); }
    if (index >= kMaxSRVCount || !allocated_.test(index)) { throw std::out_of_range("SRV index is not allocated."); }
}

D3D12_CPU_DESCRIPTOR_HANDLE SRVInformationManager::GetCPUDescriptorHandle(UINT index) const
{
    ValidateIndex(index);
    auto handle = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorSize_) * index;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE SRVInformationManager::GetGPUDescriptorHandle(UINT index) const
{
    ValidateIndex(index);
    auto handle = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(descriptorSize_) * index;
    return handle;
}

void SRVInformationManager::CreateSRVForTexture2D(UINT index, ID3D12Resource* resource, DXGI_FORMAT format, UINT mipLevels) const
{
    const auto handle = GetCPUDescriptorHandle(index);
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = mipLevels;
    directXBase_->GetDevice()->CreateShaderResourceView(resource, &desc, handle);
}

void SRVInformationManager::CreateSRVForStructuredBuffer(UINT index, ID3D12Resource* resource, UINT elementCount, UINT elementStride) const
{
    const auto handle = GetCPUDescriptorHandle(index);
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Buffer.NumElements = elementCount;
    desc.Buffer.StructureByteStride = elementStride;
    directXBase_->GetDevice()->CreateShaderResourceView(resource, &desc, handle);
}

void SRVInformationManager::PreDraw() const
{
    if (!descriptorHeap_) { throw std::logic_error("Initialize SRVInformationManager before drawing."); }
    ID3D12DescriptorHeap* heaps[] = { descriptorHeap_.Get() };
    directXBase_->GetCommandList()->SetDescriptorHeaps(_countof(heaps), heaps);
}

void SRVInformationManager::SetGraphicsRootDescriptorTable(UINT rootParameterIndex, UINT index) const
{
    const auto handle = GetGPUDescriptorHandle(index);
    directXBase_->GetCommandList()->SetGraphicsRootDescriptorTable(rootParameterIndex, handle);
}
