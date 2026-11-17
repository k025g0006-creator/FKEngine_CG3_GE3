#pragma once

#include "BlendMode.h"
#include "DebugCamera.h"
#include "BaseScene.h"
#include "MaltiCamera.h"
#include "Model.h"
#include "Object3DBase.h"
#include "ParticleSystem.h"
#include "Sprite.h"
#include "SpriteBase.h"
#include "Sound.h"
#include <array>
#include <iosfwd>
#include <memory>
#include <vector>

class DirectXBase;
class Input;
class ModelManager;
class SRVInformationManager;
class TextureManager;

// FKAlphaのゲームプレイシーン。
class GameScene final : public BaseScene
{
public:
    bool Initialize(const SceneContext& context) override;
    void UpdateInput(const Input& input, long mouseX, long mouseY, bool isLeftMouseDown) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void Finalize() override;
    void PrepareSmokeFrame(uint32_t frame, std::ostream& log) override;
    bool DidSmokeTestPass() const override;
#ifdef USE_IMGUI
    void DrawSettings() override;
#endif

private:
    void InitializeModels();
    void InitializeSprites();
    bool InitializeAudio(const SceneContext& context);
    void PlaySe();
    void ReturnToTitle();
    Sprite& CreateSprite(const std::string& texture, const Vector2& position, const Vector2& size);
    Sprite& DuplicateSprite(std::size_t index);
    void CheckModels(std::ostream& log);
    void CheckTextures(std::ostream& log);
    void CheckSprites(std::ostream& log);
    void CheckDebugCamera(std::ostream& log);
#ifdef USE_IMGUI
    void DrawParticleSettings();
    void DrawModelSettings();
    void DrawLightingSettings();
    void DrawSpriteSettings();
    void DrawCameraSettings();
    void DrawSoundSettings();
    void RefreshSoundFiles();
#endif

    DirectXBase* directXBase_ = nullptr;
    IXAudio2* audio_ = nullptr;
    std::ostream* log_ = nullptr;
    Sound alarm_;
    Sound bgm_;
    HRESULT loadResult_ = S_OK;
    HRESULT bgmLoadResult_ = S_OK;
#ifdef USE_IMGUI
    std::filesystem::path soundDirectory_;
    std::vector<std::filesystem::path> soundFiles_;
    std::filesystem::path selectedSe_, selectedBgm_, loadedSe_, loadedBgm_;
    std::string seStatus_, bgmStatus_;
#endif
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
    SpriteBase* spriteBase_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<Sprite> deferredSmokeSprite_;
    bool showSprites_ = true;
#ifdef USE_IMGUI
    int selectedSprite_ = 0;
    int newSpriteTexture_ = 0;
    int spriteCreateCount_ = 1;
#endif

    enum ParticleGroup : std::size_t { kCircleParticles, kPlaneParticles };
    ParticleSystem particleSystem_;
#ifdef USE_IMGUI
    int selectedParticleGroup_ = kCircleParticles;
#endif
    bool useInstancing_ = true;
    uint32_t retiredTextureSrvForSmoke_ = UINT32_MAX;
};
