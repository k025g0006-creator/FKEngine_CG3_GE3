#pragma once

#include "BlendMode.h"
#include "Model.h"
#include "Transform.h"
#include <array>
#include <iosfwd>
#include <optional>
#include <random>
#include <span>
#include <vector>

class DirectXBase;
class SRVInformationManager;
class TextureManager;
class ViewCamera;

class ParticleSystem
{
public:
    static constexpr uint32_t kMaxParticlesPerGroup = 100;

    struct Settings
    {
        Vector3 acceleration{15.0f, 0.0f, 0.0f};
        AABB accelerationArea{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        bool useAccelerationField = true;
        bool useBillboard = true;
        bool updateParticles = true;
        int blendMode = kBlendModeAdd;
    };

    struct GroupSettings
    {
        Vector3 position{};
        int emissionCount = 3;
        float emissionInterval = 0.5f;
        bool autoEmit = true;
    };

    ParticleSystem() = default;
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // グループはテクスチャ番号で参照する。テクスチャはFinalizeまで保持する。
    void Initialize(DirectXBase& directX, SRVInformationManager& srvManager,
        TextureManager& textures, std::span<const uint32_t> textureIndices, std::ostream& log);
    // GPUの利用が完了した後、共有ヒープを破棄する前に呼ぶ。
    void Finalize();
    uint32_t Emit(std::size_t groupIndex);
    void Update(const ViewCamera& camera, float deltaTime);
    void Draw(const Model& mesh, const Model::Material& material);
    Settings& GetSettings() { return settings_; }
    GroupSettings& GetGroupSettings(std::size_t groupIndex) { return groups_.at(groupIndex).settings; }
    uint32_t GetInstanceCount(std::size_t groupIndex) const { return groups_.at(groupIndex).numInstances; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(std::size_t groupIndex) const;

private:
    // Particle.VS.hlslと同じ配置にする。
    struct ParticleForGPU { Matrix4x4 WVP; Matrix4x4 World; Vector4 color; };
    struct Particle
    {
        FKEngine::Transform transform;
        Vector3 velocity;
        Vector4 color;
        float lifeTime;
        float currentTime = 0.0f;
    };
    struct Group
    {
        GroupSettings settings;
        std::vector<Particle> particles;
        double emissionElapsed = 0.0;
        uint32_t textureIndex = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
        ParticleForGPU* instancingData = nullptr;
        std::optional<uint32_t> instancingSrvIndex;
        uint32_t numInstances = 0;
    };

    void InitializePipeline(std::ostream& log);
    void InitializeGroup(Group& group, uint32_t textureIndex);
    uint32_t Emit(Group& group, uint32_t requestedCount);
    void AdvanceGroup(Group& group, float deltaTime);

    Settings settings_;
    std::vector<Group> groups_;
    std::mt19937 randomEngine_{std::random_device{}()};
    DirectXBase* directXBase_ = nullptr;
    SRVInformationManager* srvManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> particlePipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Model::Material* particleMaterialData_ = nullptr;
};
