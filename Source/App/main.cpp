#include <Windows.h>
#include <dbghelp.h>
#include <mfapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "DemoScene.h"
#include "D3DResourceLeakChecker.h"
#include "DirectXBase.h"
#include "Input.h"
#include "Logger.h"
#include "Sound.h"
#include "SRVInformationManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#ifdef USE_IMGUI
#include "ImGuiManager.h"
#include "imgui.h"
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xaudio2.lib")

namespace
{
static std::filesystem::path GetExecutableDirectory()
{
    wchar_t executablePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    return std::filesystem::path(executablePath).parent_path();
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
    // 時刻を取得して、時刻を名前に入れたファイルを作成、Dumpsディレクタ以下に出力
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t filePath[MAX_PATH] = { 0 };
    const std::filesystem::path dumpDirectory = GetExecutableDirectory() / "Dumps";
    std::filesystem::create_directories(dumpDirectory);
    StringCchPrintfW(filePath, MAX_PATH, L"%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
    const std::filesystem::path dumpFilePath = dumpDirectory / filePath;
    HANDLE dumpFileHandle = CreateFileW(dumpFilePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

    // processId(このexeのId)とクラッシュ(例外)の発生したthreadIdを取得
    DWORD processId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();

    // 設定情報を入力
    MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
    minidumpInformation.ThreadId = threadId;
    minidumpInformation.ExceptionPointers = exception;
    minidumpInformation.ClientPointers = TRUE;

    // Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
    MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
    CloseHandle(dumpFileHandle);

    // 他に関連づけられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
    return EXCEPTION_EXECUTE_HANDLER;
}


std::ofstream OpenLog()
{
    const auto directory = GetExecutableDirectory() / "Logs";
    std::filesystem::create_directories(directory);
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::chrono::zoned_time localTime{ std::chrono::current_zone(), now };
    return std::ofstream(directory / std::format("{:%Y%m%d_%H%M%S}.log", localTime));
}

bool InitializeInput(const WinApp& window, Input& input, Microsoft::WRL::ComPtr<IDirectInput8>& directInput,
    Microsoft::WRL::ComPtr<IDirectInputDevice8>& mouse)
{
    if (FAILED(DirectInput8Create(window.GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void**>(directInput.GetAddressOf()), nullptr))) { return false; }
    const HWND hwnd = window.GetHwnd();
    return input.Initialize(directInput.Get(), hwnd) &&
        SUCCEEDED(directInput->CreateDevice(GUID_SysMouse, &mouse, nullptr)) &&
        SUCCEEDED(mouse->SetDataFormat(&c_dfDIMouse)) &&
        SUCCEEDED(mouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE));
}

#ifdef USE_IMGUI
std::vector<std::filesystem::path> FindSoundFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    const std::filesystem::recursive_directory_iterator end;
    for (std::filesystem::recursive_directory_iterator it(directory,
        std::filesystem::directory_options::skip_permission_denied, error); !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) { continue; }
        const auto extension = it->path().extension();
        if (_wcsicmp(extension.c_str(), L".mp3") == 0 || _wcsicmp(extension.c_str(), L".wav") == 0) {
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
    if (ImGui::BeginCombo("音源", selected.empty() ? "音源を選択" : reinterpret_cast<const char*>(preview.c_str()))) {
        for (const auto& path : files) {
            const auto name = path.lexically_relative(directory).generic_u8string();
            if (ImGui::Selectable(reinterpret_cast<const char*>(name.c_str()), path == selected)) { selected = path; }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!audio || selected.empty());
    if (ImGui::Button(loop ? "ループ再生" : "再生")) {
        HRESULT result = S_OK;
        if (selected != loaded) {
            result = sound.Load(selected);
            if (SUCCEEDED(result)) { loaded = selected; }
        }
        if (SUCCEEDED(result)) { result = loop ? sound.PlayBgm(audio) : sound.PlaySe(audio); }
        status = SUCCEEDED(result) ? "再生しました" : std::format("再生できませんでした (0x{:08X})", static_cast<unsigned>(result));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("停止")) {
        sound.Stop();
        status = "停止しました";
    }
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopID();
}
#endif

int RunApplication(bool smokeTest, const std::filesystem::path& soundPath, const std::filesystem::path& bgmPath)
{
    D3DResourceLeakChecker leakCheck;
    auto log = OpenLog();
    WinApp window;
    window.Initialize();
    const HWND hwnd = window.GetHwnd();
    Logger::Log(log, std::format("Application Started. Window Size: {}x{}", WinApp::kClientWidth, WinApp::kClientHeight));

    DirectXBase directX;
    if (!directX.Initialize(hwnd, WinApp::kClientWidth, WinApp::kClientHeight)) {
        Logger::Log(log, "DirectXBase initialization failed.");
        window.Finalize();
        return -1;
    }
    Logger::Log(log, "DirectXBase initialized.");
    Input input;
    Microsoft::WRL::ComPtr<IDirectInput8> directInput;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse;
    if (!InitializeInput(window, input, directInput, mouse)) {
        Logger::Log(log, "Input initialization failed.");
        input.Finalize();
        mouse.Reset();
        directX.Finalize();
        window.Finalize();
        return -1;
    }

    SRVInformationManager srvManager;
    srvManager.Initialize(&directX);
#ifdef USE_IMGUI
    ImGuiManager imguiManager;
    imguiManager.Initialize(hwnd, directX, srvManager, GetExecutableDirectory() / "Config/imgui.ini");
#endif
    auto& textures = *TextureManager::GetInstance();
    textures.Initialize(srvManager);
    DemoScene scene;
    scene.Initialize(directX, srvManager, smokeTest, log);

    Microsoft::WRL::ComPtr<IXAudio2> audio;
    IXAudio2MasteringVoice* masterVoice = nullptr;
    Sound alarm;
    Sound bgm;
    const bool audioReady = SUCCEEDED(XAudio2Create(&audio, 0, XAUDIO2_DEFAULT_PROCESSOR)) &&
        SUCCEEDED(audio->CreateMasteringVoice(&masterVoice));
    const HRESULT loadResult = alarm.Load(soundPath);
    if (const auto* format = alarm.GetFormat()) {
        Logger::Log(log, std::format("Sound decoded: {} Hz, {} ch, {} bits per sample.",
            format->nSamplesPerSec, format->nChannels, format->wBitsPerSample));
    } else {
        Logger::Log(log, std::format("Sound load failed (0x{:08X}).", static_cast<unsigned>(loadResult)));
    }
    HRESULT bgmLoadResult = S_OK;
    if (!bgmPath.empty()) {
        bgmLoadResult = bgm.Load(bgmPath);
        if (FAILED(bgmLoadResult)) {
            Logger::Log(log, std::format("BGM load failed (0x{:08X}).", static_cast<unsigned>(bgmLoadResult)));
        } else {
            Logger::Log(log, "BGM decoded.");
            if (!smokeTest && audioReady) {
                const HRESULT playResult = bgm.PlayBgm(audio.Get());
                if (FAILED(playResult)) {
                    Logger::Log(log, std::format("BGM playback failed (0x{:08X}).", static_cast<unsigned>(playResult)));
                }
            }
        }
    }
    const auto playSe = [&] {
        if (smokeTest || !audioReady || !alarm.GetFormat()) { return; }
        const HRESULT playResult = alarm.PlaySe(audio.Get());
        if (FAILED(playResult)) {
            Logger::Log(log, std::format("SE playback failed (0x{:08X}).", static_cast<unsigned>(playResult)));
        }
    };
    playSe();

    uint32_t smokeFrame = 0;
    constexpr float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
#ifdef USE_IMGUI
    const auto soundDirectory = GetExecutableDirectory() / "Resources";
    auto soundFiles = FindSoundFiles(soundDirectory);
    auto selectedSe = soundPath;
    auto selectedBgm = bgmPath;
    auto loadedSe = SUCCEEDED(loadResult) ? soundPath : std::filesystem::path{};
    auto loadedBgm = SUCCEEDED(bgmLoadResult) ? bgmPath : std::filesystem::path{};
    std::string seStatus;
    std::string bgmStatus;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> smokeHeap;
    if (smokeTest) {
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(directX.GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&smokeHeap)))) {
            throw std::runtime_error("Failed to create the smoke test descriptor heap.");
        }
    }
#endif
    auto previousFrameTime = std::chrono::steady_clock::now();
    while (!window.ProcessMessage()) {
        const auto frameTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(frameTime - previousFrameTime).count();
        previousFrameTime = frameTime;
        if (smokeTest) { scene.PrepareSmokeFrame(smokeFrame, log); }
        input.Update();
        if (input.TriggerKey(DIK_SPACE)) { playSe(); }
        mouse->Acquire();
        DIMOUSESTATE mouseState{};
        mouse->GetDeviceState(sizeof(mouseState), &mouseState);
        scene.UpdateCameraInput(input, mouseState.lX, mouseState.lY, (mouseState.rgbButtons[0] & 0x80) != 0);
#ifdef USE_IMGUI
        imguiManager.BeginFrame();
        scene.DrawSettings();
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 16.0f, 16.0f),
            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
        if (ImGui::Begin("サウンド", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::Button("音源一覧を更新")) { soundFiles = FindSoundFiles(soundDirectory); }
            auto* playback = audioReady && !smokeTest ? audio.Get() : nullptr;
            DrawSoundControls("BGM", bgm, playback, soundFiles, soundDirectory, selectedBgm, loadedBgm, true, bgmStatus);
            DrawSoundControls("SE", alarm, playback, soundFiles, soundDirectory, selectedSe, loadedSe, false, seStatus);
        }
        ImGui::End();
        imguiManager.EndFrame();
#endif
        constexpr float smokeDeltaTimes[] = {1.0f / 90.0f, 1.0f / 45.0f, 1.0f / 60.0f};
        scene.Update(smokeTest ? smokeDeltaTimes[smokeFrame] : deltaTime);
        directX.PreDraw(clearColor);
        srvManager.PreDraw();
        scene.Draw();
#ifdef USE_IMGUI
        if (smokeHeap && smokeFrame == 1) {
            ID3D12DescriptorHeap* heaps[] = {smokeHeap.Get()};
            directX.GetCommandList()->SetDescriptorHeaps(1, heaps);
        }
        imguiManager.Draw();
#endif
        directX.PostDraw();
        textures.ReleaseCompletedResources();
        if (smokeTest && textures.GetPendingUploadCount() != 0) {
            Logger::Log(log, "Texture upload resources were not released after GPU completion.");
            std::terminate();
        }
        if (smokeTest && ++smokeFrame == 3) {
            Logger::Log(log, "Smoke test completed: 3 frames rendered.");
            break;
        }
    }

#ifdef USE_IMGUI
    imguiManager.Finalize();
#endif
    scene.Finalize();
    TextureManager::Finalize();
    srvManager.Finalize();
    directX.Finalize();
    input.Finalize();
    mouse->Unacquire();
    mouse.Reset();
    directInput.Reset();
    window.Finalize();
    alarm.Unload();
    bgm.Unload();
    if (masterVoice) { masterVoice->DestroyVoice(); }
    audio.Reset();
    Logger::Log(log, "Hello, DirectX! Shutdown successful.");
    return smokeTest && (FAILED(loadResult) || FAILED(bgmLoadResult)) ? 1 : 0;
}
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    int argumentCount = 0;
    std::unique_ptr<LPWSTR, decltype(&LocalFree)> arguments{
        CommandLineToArgvW(GetCommandLineW(), &argumentCount), &LocalFree};
    if (!arguments) { return -1; }
    bool smokeTest = false;
    auto soundPath = GetExecutableDirectory() / "Resources/Alarm01.mp3";
    std::filesystem::path bgmPath;
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring_view argument = arguments.get()[index];
        if (argument == L"--smoke-test") { smokeTest = true; }
        if (argument == L"--sound" || argument == L"--bgm") {
            if (++index == argumentCount) { return 2; }
            auto& path = argument == L"--bgm" ? bgmPath : soundPath;
            path = arguments.get()[index];
        }
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) { return -1; }
    SetUnhandledExceptionFilter(ExportDump);
    const HRESULT mediaResult = MFStartup(MF_VERSION);
    if (FAILED(mediaResult)) {
        auto log = OpenLog();
        Logger::Log(log, std::format("Media Foundation initialization failed (0x{:08X}).", static_cast<unsigned>(mediaResult)));
        CoUninitialize();
        return -1;
    }
    const int result = RunApplication(smokeTest, soundPath, bgmPath);
    MFShutdown();
    CoUninitialize();
    return result;
}
