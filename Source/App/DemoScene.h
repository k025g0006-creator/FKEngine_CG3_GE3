#pragma once

#include "BlendMode.h"
#include "DebugCamera.h"
#include "MaltiCamera.h"
#include "Model.h"
#include "Object3DBase.h"
#include "ParticleSystem.h"
#include "Sprite.h"
#include "SpriteBase.h"
#include <array>
#include <iosfwd>
#include <memory>
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
#ifdef USE_IMGUI
    void DrawParticleSettings();
    void DrawModelSettings();
    void DrawLightingSettings();
    void DrawSpriteSettings();
    void DrawCameraSettings();
#endif

    DirectXBase* directXBase_ = nullptr;
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

    enum ParticleGroup : std::size_t { kCircleParticles, kPlaneParticles };
    ParticleSystem particleSystem_;
#ifdef USE_IMGUI
    int selectedParticleGroup_ = kCircleParticles;
#endif
    bool useInstancing_ = true;
};
