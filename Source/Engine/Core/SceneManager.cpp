#include "SceneManager.h"
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include "TextureManager.h"
#include <stdexcept>
#include <string>
#include <utility>

SceneManager& SceneManager::GetInstance()
{
    static SceneManager instance;
    return instance;
}

SceneManager::~SceneManager() = default;

void SceneManager::ChangeScene(std::string_view sceneName)
{
    if (!sceneFactory_) { throw std::logic_error("A scene factory is required."); }
    auto nextScene = sceneFactory_->CreateScene(sceneName);
    if (!nextScene) {
        throw std::invalid_argument("Unknown scene: " + std::string(sceneName));
    }
    nextScene_ = std::move(nextScene);
}

bool SceneManager::ApplyPendingScene(const SceneContext& context, TextureManager& textures)
{
    if (!nextScene_) { return true; }
    auto next = std::move(nextScene_);
    if (scene_) {
        scene_->Finalize();
        scene_.reset();
        textures.ReleaseCompletedResources();
    }
    scene_ = std::move(next);
    sceneFrame_ = 0;
    scene_->SetSceneManager(*this);
    return scene_->Initialize(context);
}

void SceneManager::Update(const SceneContext& context, float deltaTime, const Input& input,
    long mouseX, long mouseY, bool isLeftMouseDown)
{
    if (!scene_) { return; }
    if (context.smokeTest) {
        scene_->PrepareSmokeFrame(sceneFrame_, context.log);
        constexpr float smokeDeltaTimes[] = {1.0f / 90.0f, 1.0f / 45.0f, 1.0f / 60.0f};
        deltaTime = smokeDeltaTimes[sceneFrame_ % 3];
    }
    scene_->UpdateInput(input, mouseX, mouseY, isLeftMouseDown);
#ifdef USE_IMGUI
    scene_->DrawSettings();
#endif
    scene_->Update(deltaTime);
}

void SceneManager::Draw()
{
    if (scene_) { scene_->Draw(); }
}

void SceneManager::OnFrameRendered()
{
    if (scene_) { ++sceneFrame_; }
}

bool SceneManager::DidSmokeTestPass() const
{
    return scene_ && scene_->DidSmokeTestPass();
}

void SceneManager::Finalize()
{
    if (scene_) {
        scene_->Finalize();
        scene_.reset();
    }
    nextScene_.reset();
    sceneFactory_ = nullptr;
    sceneFrame_ = 0;
}
