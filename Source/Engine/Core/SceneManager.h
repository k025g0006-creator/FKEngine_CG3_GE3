#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

class AbstractSceneFactory;
class BaseScene;
class Input;
class TextureManager;
struct SceneContext;

class SceneManager final
{
public:
    static SceneManager& GetInstance();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void SetSceneFactory(AbstractSceneFactory& factory) { sceneFactory_ = &factory; }
    void ChangeScene(std::string_view sceneName);
    // 前のフレームのGPU処理が完了してから呼ぶ。
    bool ApplyPendingScene(const SceneContext& context, TextureManager& textures);
    void Update(const SceneContext& context, float deltaTime, const Input& input,
        long mouseX, long mouseY, bool isLeftMouseDown);
    void Draw();
    void OnFrameRendered();
    void Finalize();

    bool HasPendingScene() const { return nextScene_ != nullptr; }
    uint32_t GetSceneFrame() const { return sceneFrame_; }
    bool DidSmokeTestPass() const;

private:
    SceneManager() = default;
    ~SceneManager();

    AbstractSceneFactory* sceneFactory_ = nullptr; // FPFramework が所有する。
    std::unique_ptr<BaseScene> scene_;
    std::unique_ptr<BaseScene> nextScene_;
    uint32_t sceneFrame_ = 0;
};
