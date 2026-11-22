#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>

class AbstractSceneFactory;
class BaseScene;
class Input;
class TextureManager;
struct SceneContext;
enum class SceneId : std::uint32_t;

class SceneManager final
{
public:
    explicit SceneManager(std::ostream& log);
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void SetSceneFactory(std::unique_ptr<AbstractSceneFactory> factory);
    // 生成できなかった場合は現在のシーンと既存の予約を維持する。
    bool ChangeScene(SceneId sceneId);
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
    std::ostream& log_;
    std::unique_ptr<AbstractSceneFactory> sceneFactory_;
    std::unique_ptr<BaseScene> scene_;
    std::unique_ptr<BaseScene> nextScene_;
    uint32_t sceneFrame_ = 0;
};
