#include "DemoScene.h"
#include "DirectXBase.h"
#include "Logger.h"
#include "SRVInformationManager.h"
#include "TextureManager.h"
#include <numbers>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

void DemoScene::InitializeParticlePipeline(std::ostream& logStream)
{
    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    const auto instanceRange = textureRange;

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].DescriptorTable = { 1, &instanceRange };
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].DescriptorTable = { 1, &textureRange };

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootDesc.NumParameters = _countof(parameters);
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) {
        if (errors) { Logger::Log(logStream, static_cast<const char*>(errors->GetBufferPointer())); }
        throw std::runtime_error("Failed to serialize the particle root signature.");
    }
    auto* device = directXBase_->GetDevice().Get();
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&particleRootSignature_)))) {
        throw std::runtime_error("Failed to create the particle root signature.");
    }

    const auto vertexShader = directXBase_->CompileShader(L"Resources/Shaders/Particle.VS.hlsl", L"vs_6_0");
    const auto pixelShader = directXBase_->CompileShader(L"Resources/Shaders/Particle.PS.hlsl", L"ps_6_0");
    if (!vertexShader || !pixelShader) { throw std::runtime_error("Failed to compile particle shaders."); }

    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = particleRootSignature_.Get();
    pipeline.InputLayout = { inputElements, _countof(inputElements) };
    pipeline.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    pipeline.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.DepthStencilState.DepthEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pipeline.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.SampleDesc.Count = 1;
    pipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    for (int mode = 0; mode < kCountOfBlendMode; ++mode) {
        pipeline.BlendState = CreateBlendDesc(static_cast<BlendMode>(mode));
        if (FAILED(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&particlePipelineStates_[mode])))) {
            throw std::runtime_error("Failed to create a particle pipeline state.");
        }
    }
}

void DemoScene::InitializeParticleBuffers()
{
    // Instancing用のTransformationMatrixリソースを作る
    instancingResource_ =
        directXBase_->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);

    // 書き込むためのアドレスを取得
    if (!instancingResource_ || FAILED(instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_)))) {
        throw std::runtime_error("Failed to map particle instances.");
    }

    // 単位行列を書き込んでおく
    for (uint32_t index = 0; index < kNumMaxInstance; ++index)
    {
        instancingData_[index].WVP = Math::MakeIdentity4x4();
        instancingData_[index].World = Math::MakeIdentity4x4();
        instancingData_[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    // Particleも共有ヒープから必要な番号を確保する。
    instancingSrvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVForStructuredBuffer(instancingSrvIndex_, instancingResource_.Get(), kNumMaxInstance, sizeof(ParticleForGPU));


    particleTextureSrvIndex_ = textureManager_->GetSrvIndex("Resources/circle.png");
    materialResource_ = directXBase_->CreateBufferResource(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    if (!materialResource_ || FAILED(materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&particleMaterialData_)))) {
        throw std::runtime_error("Failed to map particle material.");
    }
}

void DemoScene::EmitParticles()
{
    std::uniform_real_distribution<float> offset(-1.0f, 1.0f);
    std::uniform_real_distribution<float> color(0.0f, 1.0f);
    std::uniform_real_distribution<float> lifetime(1.0f, 3.0f);
    for (uint32_t index = 0; index < emitter_.count; ++index) {
        Particle particle;
        const Vector3 displacement{offset(randomEngine_), offset(randomEngine_), offset(randomEngine_)};
        particle.transform.translate = Vector3::Add(emitter_.position, displacement);
        particle.velocity = {offset(randomEngine_), offset(randomEngine_), offset(randomEngine_)};
        particle.color = {color(randomEngine_), color(randomEngine_), color(randomEngine_), 1.0f};
        particle.lifeTime = lifetime(randomEngine_);
        particles_.push_back(particle);
    }
}

void DemoScene::UpdateParticles(const ViewCamera& camera)
{
    constexpr float kDeltaTime = 1.0f / 60.0f;
    const Matrix4x4& viewProjectionMatrix = camera.GetViewProjectionMatrix();
    // BillboardMatrixを作る（全てのParticleで同じ行列を使える）
    Matrix4x4 billboardMatrix = Math::MakeIdentity4x4(); // 利用しない場合は単位行列
    if (useBillboard_)
    {
        // 板ポリの裏が見えているので、Y軸でπ回転させて表をカメラ側へ向ける
        Matrix4x4 backToFrontMatrix = Math::MakeRotateYMatrix(std::numbers::pi_v<float>);
        billboardMatrix = Math::Multiply(backToFrontMatrix, camera.GetWorldMatrix());
        // 平行移動成分はいらない
        billboardMatrix.m[3][0] = 0.0f;
        billboardMatrix.m[3][1] = 0.0f;
        billboardMatrix.m[3][2] = 0.0f;
    }
    // Emitterの頻度に従ってParticleを発生させる
    if (updateParticles_)
    {
        emitter_.frequencyTime += kDeltaTime; // 時刻を進める
        if (emitter_.frequency <= emitter_.frequencyTime) // 頻度より大きいなら発生
        {
            // 発生処理。発生したParticleをparticles_の後ろに連結する
            EmitParticles();
            // 余計に過ぎた時間も加味して頻度用時刻を更新する
            emitter_.frequencyTime -= emitter_.frequency;
        }
    }

    numInstance_ = 0;
    for (auto it = particles_.begin(); it != particles_.end();) {
        auto& particle = *it;
        if (particle.currentTime >= particle.lifeTime) {
            it = particles_.erase(it);
            continue;
        }

        if (updateParticles_) {
            auto& position = particle.transform.translate;
            if (useAccelerationField_ && Math::IsCollision(accelerationField_.area, position)) {
                particle.velocity.x += accelerationField_.acceleration.x * kDeltaTime;
                particle.velocity.y += accelerationField_.acceleration.y * kDeltaTime;
                particle.velocity.z += accelerationField_.acceleration.z * kDeltaTime;
            }
            position.x += particle.velocity.x * kDeltaTime;
            position.y += particle.velocity.y * kDeltaTime;
            position.z += particle.velocity.z * kDeltaTime;
            particle.currentTime += kDeltaTime;
        }

        if (numInstance_ < kNumMaxInstance) {
            const auto scale = Math::MakeScaleMatrix(particle.transform.scale);
            const auto translation = Math::MakeTranslateMatrix(particle.transform.translate);
            const auto world = Math::Multiply(Math::Multiply(scale, billboardMatrix), translation);
            auto& instance = instancingData_[numInstance_++];
            instance.WVP = Math::Multiply(world, viewProjectionMatrix);
            instance.World = world;
            instance.color = particle.color;
            instance.color.w = 1.0f - particle.currentTime / particle.lifeTime;
        }
        ++it;
    }
}

void DemoScene::DrawParticles()
{
    if (numInstance_ == 0) { return; }
    *particleMaterialData_ = sphereAppearance_.GetMaterial();
    auto* commandList = directXBase_->GetCommandList().Get();
    commandList->SetGraphicsRootSignature(particleRootSignature_.Get());
    commandList->SetPipelineState(particlePipelineStates_[currentParticleBlendMode_].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    // Instancing用のStructuredBufferのSRVを設定する
    srvManager_->SetGraphicsRootDescriptorTable(1, instancingSrvIndex_);
    srvManager_->SetGraphicsRootDescriptorTable(2, particleTextureSrvIndex_); // circle.png

    commandList->IASetVertexBuffers(0, 1, &planeModel_->GetVertexBufferView());

    commandList->DrawInstanced(static_cast<UINT>(planeModel_->GetModelData().vertices.size()), numInstance_, 0, 0);
}
