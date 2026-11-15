#pragma once

#include "BlendMode.h"
#include "DebugCamera.h"
#include "MaltiCamera.h"
#include "Model.h"
#include "Object3DBase.h"
#include "Sprite.h"
#include "SpriteBase.h"
#include <array>
#include <iosfwd>
#include <list>
#include <memory>
#include <random>
#include <vector>

class DirectXBase;
class Input;
class ModelManager;
class SRVInformationManager;
class TextureManager;

// FKAlphaのサンプルシーン。ウィンドウ・DirectX・ImGuiの起動と終了はmainが担当する。
class DemoScene
{
public:
    void Initialize(DirectXBase& directXBase, SRVInformationManager& srvManager, bool smokeTest, std::ostream& log);
    void UpdateCameraInput(const Input& input, long mouseX, long mouseY, bool isLeftMouseDown);
    void Update();
    void Draw();
    void Finalize();
    void PrepareSmokeFrame(uint32_t frame, std::ostream& log);
#ifdef USE_IMGUI
    void DrawSettings();
#endif

private:
    void InitializeModels();
    void InitializeSprites();
    Sprite& CreateSprite(const std::string& texture, const Vector2& position, const Vector2& size);
    Sprite& DuplicateSprite(std::size_t index);
    void CheckModels(std::ostream& log);
    void CheckTextures(std::ostream& log);
    void CheckSprites(std::ostream& log);
    void InitializeParticlePipeline(std::ostream& log);
    void InitializeParticleBuffers();
    void EmitParticles();
    void UpdateParticles(const ViewCamera& camera);
    void DrawParticles();
#ifdef USE_IMGUI
    void DrawParticleSettings();
    void DrawModelSettings();
    void DrawLightingSettings();
    void DrawSpriteSettings();
    void DrawCameraSettings();
#endif

    DirectXBase* directXBase_ = nullptr;
    SRVInformationManager* srvManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    ModelManager* modelManager_ = nullptr;
    Model* planeModel_ = nullptr;
    Model* terrainModel_ = nullptr;
    MaltiCamera cameras_;
    DebugCamera debugCamera_;
    bool isDebugCameraActive_ = false;
    Model sphereAppearance_;
    Object3DBase planeObject_;
    Object3DBase terrainObject_;
    Object3DBase sphereObject_;
    Object3DBase secondSphere_;
    int currentBlendMode_ = kBlendModeNone;
    float colorArray_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool useMonsterBall_ = false;
    bool showBall_ = true;

    static constexpr std::size_t kMaxSpriteCount = 256;
    inline static const std::array<std::string, 5> spriteTextures{
        "Resources/uvChecker.png", "Resources/monsterBall.png", "Resources/circle.png",
        "Resources/grass.png", "Resources/fence.png"
    };
    SpriteBase spriteBase_;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<Sprite> deferredSmokeSprite_;
    bool showSprites_ = true;
#ifdef USE_IMGUI
    int selectedSprite_ = 0;
    int newSpriteTexture_ = 0;
    int spriteCreateCount_ = 1;
#endif

    // ParticleのインスタンスデータはParticle.VS.hlslと同じ配置にする。
    struct ParticleForGPU { Matrix4x4 WVP; Matrix4x4 World; Vector4 color; };
    struct Particle
    {
        FKEngine::Transform transform;
        Vector3 velocity;
        Vector4 color;
        float lifeTime;
        float currentTime = 0.0f;
    };
    struct Emitter
    {
        Vector3 position{};
        uint32_t count = 3;
        float frequency = 0.5f;
        float frequencyTime = 0.0f;
    };
    struct AccelerationField
    {
        Vector3 acceleration{15.0f, 0.0f, 0.0f};
        AABB area{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    };
    static constexpr uint32_t kNumMaxInstance = 100;
    Emitter emitter_;
    AccelerationField accelerationField_;
    std::list<Particle> particles_;
    std::mt19937 randomEngine_{std::random_device{}()};
    bool useAccelerationField_ = true;
    bool useInstancing_ = true;
    bool useBillboard_ = true;
    bool updateParticles_ = true;
    int currentParticleBlendMode_ = kBlendModeAdd;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> particlePipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    ParticleForGPU* instancingData_ = nullptr;
    Model::Material* particleMaterialData_ = nullptr;
    uint32_t instancingSrvIndex_ = 0;
    uint32_t particleTextureSrvIndex_ = 0;
    uint32_t numInstance_ = 0;
};
