#include "EffekseerSceneEffect.h"

#include "d3dx12.h"

#include <algorithm>
#include <cstring>

namespace
{
	Effekseer::Matrix44 ToEffekseerMatrix(const Matrix4x4& matrix)
	{
		Effekseer::Matrix44 result;
		static_assert(sizeof(result.Values) == sizeof(matrix.m));
		std::memcpy(result.Values, matrix.m, sizeof(matrix.m));
		return result;
	}
}

bool EffekseerSceneEffect::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, UINT bufferCount,
	const D3D12_RESOURCE_DESC& backBufferDesc, DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat,
	const char16_t* effectPath)
{
	constexpr int32_t maxParticles = 8000;
	const auto graphics = EffekseerRendererDX12::CreateGraphicsDevice(device, queue, static_cast<int32_t>(bufferCount));
	if (!graphics)
	{
		return false;
	}
	renderer_ = EffekseerRendererDX12::Create(graphics, &colorFormat, 1, depthFormat, false, maxParticles);
	if (!renderer_)
	{
		return false;
	}
	memoryPool_ = EffekseerRenderer::CreateSingleFrameMemoryPool(graphics);
	commandList_ = EffekseerRenderer::CreateCommandList(graphics, memoryPool_);
	if (!memoryPool_ || !commandList_)
	{
		return false;
	}

	// 歪みの描画では、描画先とは別のテクスチャからシーンの色を参照する。
	D3D12_RESOURCE_DESC backgroundDesc = backBufferDesc;
	backgroundDesc.Format = colorFormat;
	backgroundDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &backgroundDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&background_))))
	{
		return false;
	}
	const auto backgroundTexture = EffekseerRendererDX12::CreateTexture(graphics, background_.Get());
	if (!backgroundTexture)
	{
		return false;
	}
	renderer_->SetBackground(backgroundTexture);

	manager_ = Effekseer::Manager::Create(maxParticles);
	manager_->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
	manager_->SetSpriteRenderer(renderer_->CreateSpriteRenderer());
	manager_->SetRibbonRenderer(renderer_->CreateRibbonRenderer());
	manager_->SetRingRenderer(renderer_->CreateRingRenderer());
	manager_->SetTrackRenderer(renderer_->CreateTrackRenderer());
	manager_->SetModelRenderer(renderer_->CreateModelRenderer());
	manager_->SetTextureLoader(renderer_->CreateTextureLoader());
	manager_->SetModelLoader(renderer_->CreateModelLoader());
	manager_->SetMaterialLoader(renderer_->CreateMaterialLoader());
	effect_ = Effekseer::Effect::Create(manager_, effectPath, 0.4f);
	return IsReady();
}

void EffekseerSceneEffect::Play()
{
	if (!IsReady())
	{
		return;
	}
	if (handle_ >= 0 && manager_->Exists(handle_))
	{
		manager_->StopEffect(handle_);
	}
	handle_ = manager_->Play(effect_, 0.0f, 0.5f, 0.0f);
	elapsedSeconds_ = 0.0f;
}

void EffekseerSceneEffect::Update(float deltaSeconds, const Matrix4x4& view, const Matrix4x4& projection)
{
	if (handle_ < 0)
	{
		return;
	}
	const float step = std::min(deltaSeconds, 0.1f);
	elapsedSeconds_ += step;
	renderer_->SetTime(elapsedSeconds_);
	renderer_->SetCameraMatrix(ToEffekseerMatrix(view));
	renderer_->SetProjectionMatrix(ToEffekseerMatrix(projection));
	Effekseer::Manager::LayerParameter layer;
	layer.ViewerPosition = renderer_->GetCameraPosition();
	manager_->SetLayerParameter(0, layer);
	manager_->Update(step * 60.0f);
	if (!manager_->Exists(handle_))
	{
		handle_ = -1;
	}
}

bool EffekseerSceneEffect::Draw(ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
{
	if (handle_ < 0)
	{
		return false;
	}
	const D3D12_RESOURCE_BARRIER copyBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(background_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
	};
	commands->ResourceBarrier(_countof(copyBarriers), copyBarriers);
	commands->CopyResource(background_.Get(), backBuffer);
	const D3D12_RESOURCE_BARRIER drawBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(background_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
	};
	commands->ResourceBarrier(_countof(drawBarriers), drawBarriers);

	memoryPool_->NewFrame();
	EffekseerRendererDX12::BeginCommandList(commandList_, commands);
	renderer_->SetCommandList(commandList_);
	Effekseer::Manager::DrawParameter parameters;
	parameters.ZNear = 0.0f;
	parameters.ZFar = 1.0f;
	parameters.ViewProjectionMatrix = renderer_->GetCameraProjectionMatrix();
	renderer_->BeginRendering();
	manager_->Draw(parameters);
	renderer_->EndRendering();
	renderer_->SetCommandList(nullptr);
	EffekseerRendererDX12::EndCommandList(commandList_);
	return true;
}
