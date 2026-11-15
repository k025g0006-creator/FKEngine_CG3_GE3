#pragma once

#include "Matrix4x4.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <Effekseer.h>
#include <EffekseerRendererDX12.h>

class EffekseerSceneEffect
{
public:
	bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, UINT bufferCount,
		const D3D12_RESOURCE_DESC& backBufferDesc, DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat,
		const char16_t* effectPath);
	bool IsReady() const { return effect_ != nullptr; }
	void Play();
	void Update(float deltaSeconds, const Matrix4x4& view, const Matrix4x4& projection);
	bool Draw(ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer);

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> background_;
	EffekseerRenderer::RendererRef renderer_;
	Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool> memoryPool_;
	Effekseer::RefPtr<EffekseerRenderer::CommandList> commandList_;
	Effekseer::ManagerRef manager_;
	Effekseer::EffectRef effect_;
	Effekseer::Handle handle_ = -1;
	float elapsedSeconds_ = 0.0f;
};
