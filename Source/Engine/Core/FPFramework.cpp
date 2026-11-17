#include "FPFramework.h"
#include "BaseScene.h"

#include "Logger.h"
#include "TextureManager.h"
#include <dbghelp.h>
#include <mfapi.h>
#include <strsafe.h>
#include <chrono>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xaudio2.lib")

namespace
{
std::filesystem::path ExecutableDirectory()
{
    wchar_t executablePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    return std::filesystem::path(executablePath).parent_path();
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
    // 時刻を取得して、時刻を名前に入れたファイルを作成、Dumpsディレクタ以下に出力
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t filePath[MAX_PATH] = { 0 };
    const std::filesystem::path dumpDirectory = ExecutableDirectory() / "Dumps";
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
    const auto directory = ExecutableDirectory() / "Logs";
    std::filesystem::create_directories(directory);
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::chrono::zoned_time localTime{ std::chrono::current_zone(), now };
    return std::ofstream(directory / std::format("{:%Y%m%d_%H%M%S}.log", localTime));
}

bool InitializeInput(const WinApp& window, Input& input, Microsoft::WRL::ComPtr<IDirectInput8>& directInput,
    Microsoft::WRL::ComPtr<IDirectInputDevice8>& mouse)
{
    if (FAILED(DirectInput8Create(window.GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void**>(directInput.GetAddressOf()), nullptr))) 
    {
        return false;
    }
    const HWND hwnd = window.GetHwnd();
    return input.Initialize(directInput.Get(), hwnd) &&
        SUCCEEDED(directInput->CreateDevice(GUID_SysMouse, &mouse, nullptr)) &&
        SUCCEEDED(mouse->SetDataFormat(&c_dfDIMouse)) &&
        SUCCEEDED(mouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE));
}

}

std::filesystem::path FPFramework::GetExecutableDirectory()
{
    return ExecutableDirectory();
}

FPFramework::PlatformLifetime::~PlatformLifetime()
{
    if (mediaStarted)
    {
        MFShutdown(); 
    }
    if (comStarted) 
    {
        CoUninitialize();
    }
}

bool FPFramework::Initialize()
{
    log_ = OpenLog();
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) 
    {
        Logger::Log(log_, std::format("COM initialization failed (0x{:08X}).", static_cast<unsigned>(comResult)));
        return false;
    }
    platform_.comStarted = true;
    SetUnhandledExceptionFilter(ExportDump);
    const HRESULT mediaResult = MFStartup(MF_VERSION);
    if (FAILED(mediaResult))
    {
        Logger::Log(log_, std::format("Media Foundation initialization failed (0x{:08X}).", static_cast<unsigned>(mediaResult)));
        return false;
    }
    platform_.mediaStarted = true;

    windowStarted_ = true;
    window_.Initialize();
    const HWND hwnd = window_.GetHwnd();
    Logger::Log(log_, std::format("Application Started. Window Size: {}x{}", WinApp::kClientWidth, WinApp::kClientHeight));
    if (!directX_.Initialize(hwnd, WinApp::kClientWidth, WinApp::kClientHeight))
    {
        Logger::Log(log_, "DirectXBase initialization failed.");
        return false;
    }
    Logger::Log(log_, "DirectXBase initialized.");
    if (!InitializeInput(window_, input_, directInput_, mouse_)) 
    {
        Logger::Log(log_, "Input initialization failed.");
        return false;
    }

    srvManager_.Initialize(&directX_);
#ifdef USE_IMGUI
    imguiManager_.Initialize(hwnd, directX_, srvManager_, GetExecutableDirectory() / "Config/imgui.ini");
#endif
    textures_ = TextureManager::GetInstance();
    textures_->Initialize(srvManager_);
    spriteBase_.emplace();
    spriteBase_->Initialize(&directX_, float(WinApp::kClientWidth), float(WinApp::kClientHeight));
    audioReady_ = SUCCEEDED(XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR)) &&
        SUCCEEDED(audio_->CreateMasteringVoice(&masterVoice_));
    return true;
}

bool FPFramework::StartScenes(std::unique_ptr<AbstractSceneFactory> factory, std::string_view initialScene,
    bool smokeTest, std::vector<std::wstring> arguments)
{
    if (!factory) { throw std::invalid_argument("A scene factory is required."); }
    smokeTest_ = smokeTest;
    sceneArguments_ = std::move(arguments);
    resourceDirectory_ = GetExecutableDirectory() / "Resources";
    sceneFactory_ = std::move(factory);
    sceneManager_.SetSceneFactory(*sceneFactory_);
    sceneManager_.ChangeScene(initialScene);
    if (sceneManager_.ApplyPendingScene(GetSceneContext(), *textures_)) { return true; }
    RequestEnd(2);
    return false;
}

SceneContext FPFramework::GetSceneContext()
{
    return {directX_, srvManager_, *spriteBase_, audioReady_ ? audio_.Get() : nullptr,
        smokeTest_, log_, resourceDirectory_, sceneArguments_};
}

bool FPFramework::Update(float deltaTime)
{
    if (window_.ProcessMessage()) 
    {
        RequestEnd();
        return false;
    }
    const auto context = GetSceneContext();
    if (!sceneManager_.ApplyPendingScene(context, *textures_)) {
        RequestEnd(2);
        return false;
    }
    input_.Update();
    mouse_->Acquire();
    mouseState_ = {};
    mouse_->GetDeviceState(sizeof(mouseState_), &mouseState_);
#ifdef USE_IMGUI
    imguiManager_.BeginFrame();
#endif
    sceneManager_.Update(context, deltaTime, input_, mouseState_.lX, mouseState_.lY,
        (mouseState_.rgbButtons[0] & 0x80) != 0);
    return true;
}

void FPFramework::Draw()
{
    sceneManager_.Draw();
}

void FPFramework::Finalize()
{
    sceneManager_.Finalize();
    sceneFactory_.reset();
    if (masterVoice_) 
    {
        masterVoice_->DestroyVoice();
    }
    audio_.Reset();
#ifdef USE_IMGUI
    imguiManager_.Finalize();
#endif
    spriteBase_.reset();
    if (textures_) 
    {
        TextureManager::Finalize(); 
    }
    srvManager_.Finalize();
    directX_.Finalize();
    input_.Finalize();
    if (mouse_) 
    { 
        mouse_->Unacquire(); 
    }
    mouse_.Reset();
    directInput_.Reset();
    if (windowStarted_)
    { 
        window_.Finalize();
    }
}

int FPFramework::Run()
{
    bool initialized = false;
    try 
    {
        initialized = Initialize();
        if (!initialized && !IsEndRequest()) 
        {
            RequestEnd(-1); 
        }

        auto previousFrameTime = std::chrono::steady_clock::now();
        while (initialized && !IsEndRequest()) 
        {
            const auto frameTime = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(frameTime - previousFrameTime).count();
            previousFrameTime = frameTime;
            if (!Update(deltaTime) || IsEndRequest()) 
            {
                break;
            }

#ifdef USE_IMGUI
            imguiManager_.EndFrame();
#endif
            directX_.PreDraw(clearColor_.data());
            srvManager_.PreDraw();
            Draw();
#ifdef USE_IMGUI
            imguiManager_.Draw();
#endif
            directX_.PostDraw();
            textures_->ReleaseCompletedResources();
            sceneManager_.OnFrameRendered();
            OnFrameComplete();
        }
    } catch (const std::exception& error)
    {
        if (log_)
        { 
            Logger::Log(log_, error.what()); 
        }
        RequestEnd(-1);
    }
    Finalize();
    if (initialized && exitCode_ >= 0) 
    {
        Logger::Log(log_, "Hello, DirectX! Shutdown successful.");
    }
    return exitCode_;
}
