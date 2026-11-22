#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>

class DirectXBase;
class Input;
class SceneManager;
class SRVInformationManager;
class SpriteBase;
struct IXAudio2;
enum class SceneId : std::uint32_t;

struct SceneContext
{
    DirectXBase& directX;
    SRVInformationManager& srvManager;
    SpriteBase& spriteBase;
    IXAudio2* audio;
    bool smokeTest;
    std::ostream& log;
    const std::filesystem::path& resourceDirectory;
    std::span<const std::wstring> arguments;
};

class BaseScene
{
public:
    virtual ~BaseScene() = default;
    virtual bool Initialize(const SceneContext& context) = 0;
    virtual void UpdateInput(const Input&, long, long, bool) {}
    virtual void Update(float deltaTime) = 0;
    virtual void Draw() = 0;
    virtual void Finalize() = 0;
    virtual void PrepareSmokeFrame(uint32_t, std::ostream&) {}
    virtual bool DidSmokeTestPass() const { return true; }
#ifdef USE_IMGUI
    virtual void DrawSettings() {}
#endif

protected:
    bool RequestSceneChange(SceneId sceneId);

private:
    friend class SceneManager;
    void SetSceneManager(SceneManager& manager) { sceneManager_ = &manager; }
    SceneManager* sceneManager_ = nullptr; // シーンマネージャーを借りる
};
