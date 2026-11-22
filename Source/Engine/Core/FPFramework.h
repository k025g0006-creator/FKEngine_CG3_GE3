#pragma once

#include "AbstractSceneFactory.h"
#include "D3DResourceLeakChecker.h"
#include "DirectXBase.h"
#include "Input.h"
#include "SceneManager.h"
#include "SRVInformationManager.h"
#include "SpriteBase.h"
#include "WinApp.h"
#ifdef USE_IMGUI
#include "ImGuiManager.h"
#endif
#include <xaudio2.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

class TextureManager;

class FPFramework
{
public:
    virtual ~FPFramework() = default;
    int Run();

protected:
    virtual bool Initialize();
    virtual bool Update(float deltaTime);
    virtual void Draw();
    virtual void Finalize();
    virtual void OnFrameComplete() {}
    virtual bool IsEndRequest() const { return endRequested_; }
    void RequestEnd(int exitCode = 0) { endRequested_ = true; exitCode_ = exitCode; }
    static std::filesystem::path GetExecutableDirectory();
    bool StartScenes(std::unique_ptr<AbstractSceneFactory> factory, SceneId initialScene,
        bool smokeTest, std::vector<std::wstring> arguments);
    bool IsSceneSmokeTest() const { return smokeTest_; }

private:
    // COM はエンジンの各リソースが破棄された後に終了する。
    struct PlatformLifetime
    {
        bool comStarted = false;
        bool mediaStarted = false;
        ~PlatformLifetime();
    } platform_;
    D3DResourceLeakChecker leakCheck_;

protected:
    std::ofstream log_;
    DirectXBase directX_;
    Input input_;
    DIMOUSESTATE mouseState_{};
    SRVInformationManager srvManager_;
    TextureManager* textures_ = nullptr;
    std::optional<SpriteBase> spriteBase_;
    Microsoft::WRL::ComPtr<IXAudio2> audio_;
    bool audioReady_ = false;
    std::array<float, 4> clearColor_{0.1f, 0.25f, 0.5f, 1.0f};
    SceneManager sceneManager_{log_};

private:
    SceneContext GetSceneContext();
    std::filesystem::path resourceDirectory_;
    std::vector<std::wstring> sceneArguments_;
    bool smokeTest_ = false;
    WinApp window_;
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;
#ifdef USE_IMGUI
    ImGuiManager imguiManager_;
#endif
    bool windowStarted_ = false;
    bool endRequested_ = false;
    int exitCode_ = 0;
};
