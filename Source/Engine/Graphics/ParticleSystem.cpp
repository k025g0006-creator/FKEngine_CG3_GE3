#include "ParticleSystem.h"
#include "DirectXBase.h"
#include "Logger.h"
#include "SRVInformationManager.h"
#include "TextureManager.h"
#include "ViewCamera.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

void ParticleSystem::Initialize(DirectXBase& directX, SRVInformationManager& srvManager,
    TextureManager& textures, std::span<const uint32_t> textureIndices, std::ostream& log)
{
    if (directXBase_) { throw std::logic_error("Finalize ParticleSystem before initializing again."); }
    if (srvManager.GetDirectXBase() != &directX) {
        throw std::invalid_argument("ParticleSystem requires the shared SRV heap for its DirectXBase.");
    }
    if (textureIndices.empty()) { throw std::invalid_argument("ParticleSystem requires at least one texture."); }
    directXBase_ = &directX;
    srvManager_ = &srvManager;
    textureManager_ = &textures;
    try {
        InitializePipeline(log);
        materialResource_ = directX.CreateBufferResource(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (!materialResource_ || FAILED(materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&particleMaterialData_)))) {
            throw std::runtime_error("Failed to map particle material.");
        }
        groups_.resize(textureIndices.size());
        for (std::size_t index = 0; index < groups_.size(); ++index) {
            InitializeGroup(groups_[index], textureIndices[index]);
        }
    } catch (...) {
        Finalize();
        throw;
    }
}

void ParticleSystem::Finalize()
{
    for (auto& group : groups_) {
        if (group.instancingSrvIndex) {
            srvManager_->Free(srvManager_->GetCPUDescriptorHandle(*group.instancingSrvIndex));
        }
    }
    groups_.clear();
    particleMaterialData_ = nullptr;
    materialResource_.Reset();
    particlePipelineStates_ = {};
    particleRootSignature_.Reset();
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    directXBase_ = nullptr;
}

void ParticleSystem::InitializePipeline(std::ostream& logStream)
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

void ParticleSystem::InitializeGroup(Group& group, uint32_t textureIndex)
{
    textureManager_->GetSrvHandleGPU(textureIndex);
    group.textureIndex = textureIndex;
    group.particles.reserve(kMaxParticlesPerGroup);
    group.instancingResource = directXBase_->CreateBufferResource(sizeof(ParticleForGPU) * kMaxParticlesPerGroup);
    if (!group.instancingResource ||
        FAILED(group.instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&group.instancingData)))) {
        throw std::runtime_error("Failed to map particle instances.");
    }
    group.instancingSrvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVForStructuredBuffer(*group.instancingSrvIndex, group.instancingResource.Get(),
        kMaxParticlesPerGroup, sizeof(ParticleForGPU));
}

uint32_t ParticleSystem::Emit(std::size_t groupIndex)
{
    auto& group = groups_.at(groupIndex);
    return Emit(group, static_cast<uint32_t>(std::clamp(group.settings.emissionCount, 0,
        static_cast<int>(kMaxParticlesPerGroup))));
}

uint32_t ParticleSystem::Emit(Group& group, uint32_t requestedCount)
{
    const auto available = kMaxParticlesPerGroup - static_cast<uint32_t>(group.particles.size());
    const auto count = (std::min)(available, requestedCount);
    std::uniform_real_distribution<float> offset(-1.0f, 1.0f);
    std::uniform_real_distribution<float> color(0.0f, 1.0f);
    std::uniform_real_distribution<float> lifetime(1.0f, 3.0f);
    for (uint32_t index = 0; index < count; ++index) {
        Particle particle;
        const Vector3 displacement{offset(randomEngine_), offset(randomEngine_), offset(randomEngine_)};
        particle.transform.translate = Vector3::Add(group.settings.position, displacement);
        particle.velocity = {offset(randomEngine_), offset(randomEngine_), offset(randomEngine_)};
        particle.color = {color(randomEngine_), color(randomEngine_), color(randomEngine_), 1.0f};
        particle.lifeTime = lifetime(randomEngine_);
        group.particles.push_back(particle);
    }
    return count;
}

D3D12_GPU_DESCRIPTOR_HANDLE ParticleSystem::GetTextureHandle(std::size_t groupIndex) const
{
    const auto& group = groups_.at(groupIndex);
    return textureManager_->GetSrvHandleGPU(group.textureIndex);
}

void ParticleSystem::Update(const ViewCamera& camera, float deltaTime)
{
    if (groups_.empty()) { throw std::logic_error("Initialize ParticleSystem before Update."); }
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) { throw std::invalid_argument("Invalid particle delta time."); }
    const Matrix4x4& viewProjectionMatrix = camera.GetViewProjectionMatrix();
    // BillboardMatrixを作る（全てのParticleで同じ行列を使える）
    Matrix4x4 billboardMatrix = Math::MakeIdentity4x4(); // 利用しない場合は単位行列
    if (settings_.useBillboard)
    {
        // 板ポリの裏が見えているので、Y軸でπ回転させて表をカメラ側へ向ける
        Matrix4x4 backToFrontMatrix = Math::MakeRotateYMatrix(std::numbers::pi_v<float>);
        billboardMatrix = Math::Multiply(backToFrontMatrix, camera.GetWorldMatrix());
        // 平行移動成分はいらない
        billboardMatrix.m[3][0] = 0.0f;
        billboardMatrix.m[3][1] = 0.0f;
        billboardMatrix.m[3][2] = 0.0f;
    }
    for (auto& group : groups_) {
        if (settings_.updateParticles) { AdvanceGroup(group, deltaTime); }
        group.numInstances = 0;
        for (const auto& particle : group.particles) {
            const auto scale = Math::MakeScaleMatrix(particle.transform.scale);
            const auto translation = Math::MakeTranslateMatrix(particle.transform.translate);
            const auto world = Math::Multiply(Math::Multiply(scale, billboardMatrix), translation);
            auto& instance = group.instancingData[group.numInstances++];
            instance.WVP = Math::Multiply(world, viewProjectionMatrix);
            instance.World = world;
            instance.color = particle.color;
            instance.color.w = 1.0f - particle.currentTime / particle.lifeTime;
        }
    }
}

void ParticleSystem::AdvanceGroup(Group& group, float deltaTime)
{
    for (std::size_t index = 0; index < group.particles.size();) {
        auto& particle = group.particles[index];
        particle.currentTime += deltaTime;
        if (particle.currentTime >= particle.lifeTime) {
            particle = group.particles.back();
            group.particles.pop_back();
            continue;
        }

        auto& position = particle.transform.translate;
        if (settings_.useAccelerationField && Math::IsCollision(settings_.accelerationArea, position)) {
            particle.velocity = Vector3::Add(particle.velocity, Vector3::Multiply(deltaTime, settings_.acceleration));
        }
        position = Vector3::Add(position, Vector3::Multiply(deltaTime, particle.velocity));
        ++index;
    }

    const double interval = group.settings.emissionInterval;
    if (group.settings.autoEmit && std::isfinite(interval) && interval > 0.0) {
        group.emissionElapsed += deltaTime;
        if (group.emissionElapsed >= interval) {
            const auto batches = std::floor(group.emissionElapsed / interval);
            group.emissionElapsed = std::fmod(group.emissionElapsed, interval);
            const auto requested = std::clamp(batches * group.settings.emissionCount, 0.0,
                static_cast<double>(kMaxParticlesPerGroup));
            Emit(group, static_cast<uint32_t>(requested));
        }
    }
}

void ParticleSystem::Draw(const Model& mesh, const Model::Material& material)
{
    if (std::ranges::none_of(groups_, [](const Group& group) { return group.numInstances != 0; })) { return; }
    *particleMaterialData_ = material;
    auto* commandList = directXBase_->GetCommandList().Get();
    commandList->SetGraphicsRootSignature(particleRootSignature_.Get());
    commandList->SetPipelineState(particlePipelineStates_.at(settings_.blendMode).Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->IASetVertexBuffers(0, 1, &mesh.GetVertexBufferView());

    const auto vertexCount = static_cast<UINT>(mesh.GetModelData().vertices.size());
    for (const auto& group : groups_) {
        if (group.numInstances == 0) { continue; }
        srvManager_->SetGraphicsRootDescriptorTable(1, *group.instancingSrvIndex);
        textureManager_->BindTexture(2, group.textureIndex);
        commandList->DrawInstanced(vertexCount, group.numInstances, 0, 0);
    }
}
