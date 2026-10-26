#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <string>
#include <d3d12.h>
#include <DirectXTex.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl.h>

class DirectXBase
{
public:
	static constexpr UINT kBackBufferCount = 2;
	static constexpr UINT kSRVDescriptorCount = 128;

	bool Initialize(HWND hwnd, UINT width, UINT height);
	void Finalize();
	void PreDraw(const float clearColor[4]);
	void PostDraw();
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
		const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
		const DirectX::ScratchImage& mipImages);

	Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() { return device_; }
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& GetCommandList() { return commandList_; }
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& GetSRVDescriptorHeap() { return srvDescriptorHeap_; }
	UINT GetSRVDescriptorSize() const { return srvDescriptorSize_; }

private:
	bool InitializeCommand();
	void InitializeFixFPS();
	void UpdateFixFPS();

	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBackBufferCount]{};
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;
	UINT rtvDescriptorSize_ = 0;
	UINT srvDescriptorSize_ = 0;
	UINT dsvDescriptorSize_ = 0;
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
	std::chrono::steady_clock::time_point fpsReference_{};
	bool fpsInitialized_ = false;
	UINT currentBackBufferIndex_ = 0;
};

