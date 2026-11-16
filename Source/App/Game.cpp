#include "Game.h"

#include "DemoScene.h"
#include "Logger.h"
#include "Sound.h"
#include "TextureManager.h"
#include <shellapi.h>
#include <algorithm>
#include <format>
#include <string_view>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace
{
#ifdef USE_IMGUI
std::vector<std::filesystem::path> FindSoundFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    const std::filesystem::recursive_directory_iterator end;
    for (std::filesystem::recursive_directory_iterator it(directory,
        std::filesystem::directory_options::skip_permission_denied, error); !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file(error))
        {
            continue;
        }
        const auto extension = it->path().extension();
        if (_wcsicmp(extension.c_str(), L".mp3") == 0 || _wcsicmp(extension.c_str(), L".wav") == 0) 
        {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

void DrawSoundControls(const char* label, Sound& sound, IXAudio2* audio,
    const std::vector<std::filesystem::path>& files, const std::filesystem::path& directory,
    std::filesystem::path& selected, std::filesystem::path& loaded, bool loop, std::string& status)
{
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    const auto preview = selected.filename().u8string();
    if (ImGui::BeginCombo("音源", selected.empty() ? "音源を選択" : reinterpret_cast<const char*>(preview.c_str())))
    {
        for (const auto& path : files) 
        {
            const auto name = path.lexically_relative(directory).generic_u8string();
            if (ImGui::Selectable(reinterpret_cast<const char*>(name.c_str()), path == selected)) { selected = path; }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!audio || selected.empty());
    if (ImGui::Button(loop ? "ループ再生" : "再生")) 
    {
        HRESULT result = S_OK;
        if (selected != loaded) 
        {
            result = sound.Load(selected);
            if (SUCCEEDED(result))
            {
                loaded = selected;
            }
        }
        if (SUCCEEDED(result))
        { 
            result = loop ? sound.PlayBgm(audio) : sound.PlaySe(audio);
        }
        status = SUCCEEDED(result) ? "再生しました" : std::format("再生できませんでした (0x{:08X})", static_cast<unsigned>(result));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("停止")) 
    {
        sound.Stop();
        status = "停止しました";
    }
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopID();
}
#endif

}

struct Game::Runtime
{
    explicit Runtime(bool smoke) : smokeTest(smoke) {}

    bool smokeTest;
    uint32_t smokeFrame = 0;
    DemoScene scene;
    bool sceneStarted = false;
    Sound alarm;
    Sound bgm;
    HRESULT loadResult = S_OK;
    HRESULT bgmLoadResult = S_OK;
#ifdef USE_IMGUI
    std::filesystem::path soundDirectory;
    std::vector<std::filesystem::path> soundFiles;
    std::filesystem::path selectedSe, selectedBgm, loadedSe, loadedBgm;
    std::string seStatus, bgmStatus;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> smokeHeap;
#endif
};

Game::Game() = default;
Game::~Game() = default;

bool Game::Initialize()
{
    int argumentCount = 0;
    std::unique_ptr<LPWSTR, decltype(&LocalFree)> arguments
    {
        CommandLineToArgvW(GetCommandLineW(), &argumentCount), &LocalFree
    };
    if (!arguments)
    { 
        return false; 
    }
    bool smokeTest = false;
    auto soundPath = GetExecutableDirectory() / "Resources/Alarm01.mp3";
    std::filesystem::path bgmPath;
    for (int index = 1; index < argumentCount; ++index) 
    {
        const std::wstring_view argument = arguments.get()[index];
        if (argument == L"--smoke-test") 
        { 
            smokeTest = true;
        }
        if (argument == L"--sound" || argument == L"--bgm")
        {
            if (++index == argumentCount) 
            {
                RequestEnd(2);
                return false;
            }
            auto& path = argument == L"--bgm" ? bgmPath : soundPath;
            path = arguments.get()[index];
        }
    }

    if (!FPFramework::Initialize()) { return false; }

    runtime_ = std::make_unique<Runtime>(smokeTest);
    auto& runtime = *runtime_;
    auto& log = log_;
    runtime.sceneStarted = true;
    runtime.scene.Initialize(directX_, srvManager_, *spriteBase_, runtime.smokeTest, log);

    runtime.loadResult = runtime.alarm.Load(soundPath);
    if (const auto* format = runtime.alarm.GetFormat()) 
    {
        Logger::Log(log, std::format("Sound decoded: {} Hz, {} ch, {} bits per sample.",
            format->nSamplesPerSec, format->nChannels, format->wBitsPerSample));
    }
    else
    {
        Logger::Log(log, std::format("Sound load failed (0x{:08X}).", static_cast<unsigned>(runtime.loadResult)));
    }
    if (!bgmPath.empty())
    {
        runtime.bgmLoadResult = runtime.bgm.Load(bgmPath);
        if (FAILED(runtime.bgmLoadResult))
        {
            Logger::Log(log, std::format("BGM load failed (0x{:08X}).", static_cast<unsigned>(runtime.bgmLoadResult)));
        } 
        else 
        {
            Logger::Log(log, "BGM decoded.");
        }
    }

#ifdef USE_IMGUI
    runtime.soundDirectory = GetExecutableDirectory() / "Resources";
    runtime.soundFiles = FindSoundFiles(runtime.soundDirectory);
    runtime.selectedSe = soundPath;
    runtime.selectedBgm = bgmPath;
    if (SUCCEEDED(runtime.loadResult)) 
    {
        runtime.loadedSe = soundPath;
    }
    if (SUCCEEDED(runtime.bgmLoadResult))
    {
        runtime.loadedBgm = bgmPath;
    }
    if (runtime.smokeTest)
    {
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc
        {
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
        };

        if (FAILED(directX_.GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&runtime.smokeHeap)))) 
        {
            Logger::Log(log, "Failed to create the smoke test descriptor heap.");
            return false;
        }
    }
#endif
    if (!runtime.smokeTest && audioReady_ && runtime.bgm.GetFormat())
    {
        const HRESULT result = runtime.bgm.PlayBgm(audio_.Get());
        if (FAILED(result))
        {
            Logger::Log(log, std::format("BGM playback failed (0x{:08X}).", static_cast<unsigned>(result)));
        }
    }
    PlaySe();
    return true;
}

void Game::PlaySe()
{
    auto& runtime = *runtime_;
    if (runtime.smokeTest || !audioReady_ || !runtime.alarm.GetFormat()) 
    {
        return;
    }
    const HRESULT result = runtime.alarm.PlaySe(audio_.Get());
    if (FAILED(result)) 
    {
        Logger::Log(log_, std::format("SE playback failed (0x{:08X}).", static_cast<unsigned>(result)));
    }
}

bool Game::Update(float deltaTime)
{
    if (!FPFramework::Update(deltaTime))
    { 
        return false;
    }

    auto& runtime = *runtime_;
    if (runtime.smokeTest)
    { 
        runtime.scene.PrepareSmokeFrame(runtime.smokeFrame, log_);
    }
    if (input_.TriggerKey(DIK_SPACE)) 
    { 
        PlaySe(); 
    }
    runtime.scene.UpdateCameraInput(input_, mouseState_.lX, mouseState_.lY, (mouseState_.rgbButtons[0] & 0x80) != 0);
#ifdef USE_IMGUI
    runtime.scene.DrawSettings();
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 16.0f, 16.0f),
        ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    if (ImGui::Begin("サウンド", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) 
    {
        if (ImGui::Button("音源一覧を更新")) 
        {
            runtime.soundFiles = FindSoundFiles(runtime.soundDirectory);
        }
        auto* playback = audioReady_ && !runtime.smokeTest ? audio_.Get() : nullptr;
        DrawSoundControls("BGM", runtime.bgm, playback, runtime.soundFiles, runtime.soundDirectory,
            runtime.selectedBgm, runtime.loadedBgm, true, runtime.bgmStatus);
        DrawSoundControls("SE", runtime.alarm, playback, runtime.soundFiles, runtime.soundDirectory,
            runtime.selectedSe, runtime.loadedSe, false, runtime.seStatus);
    }
    ImGui::End();
#endif
    constexpr float smokeDeltaTimes[] = {1.0f / 90.0f, 1.0f / 45.0f, 1.0f / 60.0f};
    runtime.scene.Update(runtime.smokeTest ? smokeDeltaTimes[runtime.smokeFrame] : deltaTime);
    return true;
}

void Game::Draw()
{
    auto& runtime = *runtime_;
    runtime.scene.Draw();
#ifdef USE_IMGUI
    if (runtime.smokeHeap && runtime.smokeFrame == 1) 
    {
        ID3D12DescriptorHeap* heaps[] = {runtime.smokeHeap.Get()};
        directX_.GetCommandList()->SetDescriptorHeaps(1, heaps);
    }
#endif
}

void Game::OnFrameComplete()
{
    auto& runtime = *runtime_;
    if (!runtime.smokeTest) 
    {
        return; 
    }
    if (textures_->GetPendingUploadCount() != 0) 
    {
        Logger::Log(log_, "Texture upload resources were not released after GPU completion.");
        RequestEnd(-1);
        return;
    }
    if (++runtime.smokeFrame == 3) 
    {
        Logger::Log(log_, "Smoke test completed: 3 frames rendered.");
        RequestEnd(FAILED(runtime.loadResult) || FAILED(runtime.bgmLoadResult) ? 1 : 0);
    }
}

void Game::Finalize()
{
    if (runtime_) 
    {
        runtime_->alarm.Unload();
        runtime_->bgm.Unload();
        if (runtime_->sceneStarted)
        {
            runtime_->scene.Finalize();
        }
        runtime_.reset();
    }
    FPFramework::Finalize();
}
