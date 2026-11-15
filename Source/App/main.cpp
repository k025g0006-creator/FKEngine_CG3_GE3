#include <Windows.h>
#include <dbghelp.h>
#include <strsafe.h>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>

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

int RunApplication(bool smokeTest)
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
    const bool audioReady = SUCCEEDED(XAudio2Create(&audio, 0, XAUDIO2_DEFAULT_PROCESSOR)) &&
        SUCCEEDED(audio->CreateMasteringVoice(&masterVoice));
    alarm.Load("Resources/Alarm01.wav");
    if (!smokeTest && audioReady) { alarm.Play(audio.Get()); }

    uint32_t smokeFrame = 0;
    constexpr float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    while (!window.ProcessMessage()) {
        if (smokeTest) { scene.PrepareSmokeFrame(smokeFrame, log); }
        input.Update();
        mouse->Acquire();
        DIMOUSESTATE mouseState{};
        mouse->GetDeviceState(sizeof(mouseState), &mouseState);
        scene.UpdateCameraInput(input, mouseState.lX, mouseState.lY, (mouseState.rgbButtons[0] & 0x80) != 0);
#ifdef USE_IMGUI
        imguiManager.BeginFrame();
        scene.DrawSettings();
        imguiManager.EndFrame();
#endif
        scene.Update();
        directX.PreDraw(clearColor);
        srvManager.PreDraw();
        scene.Draw();
#ifdef USE_IMGUI
        imguiManager.Draw();
#endif
        directX.PostDraw();
        textures.ReleaseUploadResources();
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
    if (masterVoice) { masterVoice->DestroyVoice(); }
    audio.Reset();
    Logger::Log(log, "Hello, DirectX! Shutdown successful.");
    return 0;
}
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR commandLine, _In_ int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) { return -1; }
    SetUnhandledExceptionFilter(ExportDump);
    const bool smokeTest = std::string_view(commandLine).find("--smoke-test") != std::string_view::npos;
    const int result = RunApplication(smokeTest);
    CoUninitialize();
    return result;
}
