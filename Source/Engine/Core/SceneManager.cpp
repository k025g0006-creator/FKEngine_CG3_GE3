#include "SceneManager.h"
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include "Logger.h"
#include "TextureManager.h"
#include <exception>
#include <format>
#include <utility>

SceneManager::SceneManager(std::ostream& log) : log_(log)
{
}

SceneManager::~SceneManager() = default;

void SceneManager::SetSceneFactory(std::unique_ptr<AbstractSceneFactory> factory)
{
    sceneFactory_ = std::move(factory);
}

bool SceneManager::ChangeScene(SceneId sceneId)
{
    if (!sceneFactory_) {
        Logger::Log(log_, "Scene change skipped: no scene factory is configured.");
        return false;
    }
    const auto id = static_cast<std::uint32_t>(sceneId);
    std::unique_ptr<BaseScene> nextScene;
    try {
        nextScene = sceneFactory_->CreateScene(sceneId);
    } catch (const std::exception& error) {
        Logger::Log(log_, std::format("Scene change skipped: factory failed for ID {} ({}).", id, error.what()));
        return false;
    } catch (...) {
        Logger::Log(log_, std::format("Scene change skipped: unexpected factory error for ID {}.", id));
        return false;
    }
    if (!nextScene) {
        Logger::Log(log_, std::format("Scene change skipped: unknown scene ID {}.", id));
        return false;
    }
    nextScene_ = std::move(nextScene);
    return true;
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
    sceneFactory_.reset();
    sceneFrame_ = 0;
}
