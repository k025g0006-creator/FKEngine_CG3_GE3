#pragma once
#include <d3d12.h>
#include <DirectXTex.h>
#include <wrl.h>

using namespace Microsoft::WRL;

Microsoft::WRL::ComPtr<ID3D12Resource>
CreateTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device,const DirectX::TexMetadata& metadata);

// バッファリソース（頂点バッファ・定数バッファ等）の生成
ComPtr<ID3D12Resource> CreateBufferResource(const ComPtr<ID3D12Device>& device,size_t sizeInBytes);

// ディスクリプタヒープの生成
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(const ComPtr<ID3D12Device>& device,D3D12_DESCRIPTOR_HEAP_TYPE heapType,UINT numDescriptors,bool shaderVisible);

class ResourceObject
{
public:
	ResourceObject(ID3D12Resource* resource) : resource_(resource){}
	~ResourceObject() = default;
	

	ID3D12Resource* Get()
	{
		return resource_;
	}

private:
	ID3D12Resource* resource_;
};