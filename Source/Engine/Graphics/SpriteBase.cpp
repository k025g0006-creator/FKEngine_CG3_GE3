#include "SpriteBase.h"
#include "DirectXBase.h"
#include <cfloat>
#include <stdexcept>
#include <cmath>

namespace
{
void CheckSpriteResult(HRESULT result)
{
    if (FAILED(result)) {
        throw std::runtime_error("Failed to create SpriteBase rendering resources.");
    }
}
}

void SpriteBase::Initialize(DirectXBase* directXBase, float screenWidth, float screenHeight)
{
    if (directXBase == nullptr || !directXBase->GetDevice() ||
        screenWidth <= 0.0f || screenHeight <= 0.0f || directXBase_ != nullptr) {
        throw std::invalid_argument("SpriteBase requires an initialized device, screen size, and one Initialize call.");
    }

    // 所有権はmain側にある。このポインタをnew/deleteしない。
    directXBase_ = directXBase;
    try {
        CreateGraphicsPipelineState();
        SetScreenSize(screenWidth, screenHeight);
    } catch (...) {
        // 初期化失敗時は不完全な描画設定を残さない。
        pipelineState_.Reset();
        rootSignature_.Reset();
        directXBase_ = nullptr;
        screenWidth_ = screenHeight_ = 0.0f;
        projectionMatrix_ = Math::MakeIdentity4x4();
        projectionRevision_ = 0;
        throw;
    }
}

void SpriteBase::SetScreenSize(float width, float height)
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
        throw std::invalid_argument("Sprite projection size must be finite and positive.");
    }
    if (screenWidth_ == width && screenHeight_ == height) { return; }
    projectionMatrix_ = Math::MakeOrthographicMatrix(0.0f, 0.0f, width, height, 0.0f, 100.0f);
    screenWidth_ = width;
    screenHeight_ = height;
    ++projectionRevision_;
}

void SpriteBase::CreateRootSignature()
{
    auto* device = directXBase_->GetDevice().Get();
    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    textureRange.BaseShaderRegister = 0;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = FLT_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootDesc.NumParameters = _countof(parameters);
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    CheckSpriteResult(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &errors));
    CheckSpriteResult(device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)));

}

void SpriteBase::CreateGraphicsPipelineState()
{
    // RootSignatureはPSOの生成に必要なので、必ず先に作成する。
    CreateRootSignature();
    auto* device = directXBase_->GetDevice().Get();
    const auto vertexShader = directXBase_->CompileShader(L"Resources/Shaders/Sprite.VS.hlsl", L"vs_6_0");
    const auto pixelShader = directXBase_->CompileShader(L"Resources/Shaders/Sprite.PS.hlsl", L"ps_6_0");
    if (!vertexShader || !pixelShader) {
        throw std::runtime_error("Failed to compile sprite shaders.");
    }
    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = rootSignature_.Get();
    pipelineDesc.InputLayout = { inputElements, _countof(inputElements) };
    pipelineDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    pipelineDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    // Spriteは3Dシーンの後に描く2D画像。深度を読み書きしない。
    pipelineDesc.DepthStencilState.DepthEnable = FALSE;
    pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    auto& blend = pipelineDesc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.SampleDesc.Count = 1;
    pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    CheckSpriteResult(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState_)));
}

void SpriteBase::SetCommonRenderState() const
{
    if (!directXBase_) { throw std::logic_error("Initialize SpriteBase before drawing."); }
    auto* commandList = directXBase_->GetCommandList().Get();
    ID3D12DescriptorHeap* heaps[] = { directXBase_->GetSRVDescriptorHeap().Get() };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
