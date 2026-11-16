#include "ImGuiManager.h"

#ifdef USE_IMGUI
#include "DirectXBase.h"
#include "SRVInformationManager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <array>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace
{
void LoadInterfaceFont(ImGuiIO& io)
{
    std::array<wchar_t, MAX_PATH> windowsDirectory{};
    const auto length = GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    if (length != 0 && length < windowsDirectory.size()) {
        const auto fontsDirectory = std::filesystem::path(windowsDirectory.data()) / L"Fonts";
        ImFontConfig config;
        config.Flags |= ImFontFlags_NoLoadError;
        for (const auto* font : {L"meiryo.ttc", L"YuGothM.ttc"}) {
            const auto utf8Path = (fontsDirectory / font).u8string();
            io.FontDefault = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8Path.c_str()),
                16.0f, &config, io.Fonts->GetGlyphRangesJapanese());
            if (io.FontDefault) { return; }
        }
    }
    io.FontDefault = io.Fonts->AddFontDefault();
}
}

ImGuiManager::~ImGuiManager()
{
    Finalize();
}

void ImGuiManager::Initialize(HWND hwnd, DirectXBase& directXBase, SRVInformationManager& srvManager,
    const std::filesystem::path& iniPath)
{
    if (!hwnd || ImGui::GetCurrentContext() || !directXBase.GetCommandQueue() ||
        srvManager.GetDirectXBase() != &directXBase || !srvManager.GetDescriptorHeap()) {
        throw std::invalid_argument("ImGuiManager requires a window and initialized DirectX and SRV managers.");
    }
    const auto utf8Path = iniPath.u8string();
    iniFilename_.assign(utf8Path.begin(), utf8Path.end());
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    try {
        ImGui::StyleColorsDark();
        ImGuiIO& io = ImGui::GetIO();
        LoadInterfaceFont(io);
        win32Initialized_ = ImGui_ImplWin32_Init(hwnd);
        if (!win32Initialized_) { throw std::runtime_error("Failed to initialize ImGui Win32 backend."); }
        ImGui_ImplDX12_InitInfo imguiInit{};
        imguiInit.Device = directXBase.GetDevice().Get();
        imguiInit.CommandQueue = directXBase.GetCommandQueue();
        imguiInit.NumFramesInFlight = DirectXBase::kBackBufferCount;
        imguiInit.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        imguiInit.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        imguiInit.SrvDescriptorHeap = srvManager.GetDescriptorHeap();
        imguiInit.UserData = &srvManager;
        imguiInit.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
            auto& manager = *static_cast<SRVInformationManager*>(info->UserData);
            const UINT index = manager.Allocate();
            *cpu = manager.GetCPUDescriptorHandle(index);
            *gpu = manager.GetGPUDescriptorHandle(index);
        };
        imguiInit.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE) {
            static_cast<SRVInformationManager*>(info->UserData)->Free(cpu);
        };
        dx12Initialized_ = ImGui_ImplDX12_Init(&imguiInit);
        if (!dx12Initialized_) { throw std::runtime_error("Failed to initialize ImGui DirectX 12 backend."); }
        io.IniFilename = iniFilename_.c_str();
        directXBase_ = &directXBase;
        srvManager_ = &srvManager;
    } catch (...) {
        Finalize();
        throw;
    }
}

void ImGuiManager::BeginFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::EndFrame()
{
    ImGui::Render();
}

void ImGuiManager::Draw() const
{
    srvManager_->PreDraw();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), directXBase_->GetCommandList().Get());
}

void ImGuiManager::Finalize()
{
    if (!context_) { return; }
    if (dx12Initialized_) { ImGui_ImplDX12_Shutdown(); }
    if (win32Initialized_) { ImGui_ImplWin32_Shutdown(); }
    ImGui::DestroyContext(context_);
    context_ = nullptr;
    directXBase_ = nullptr;
    srvManager_ = nullptr;
    dx12Initialized_ = false;
    win32Initialized_ = false;
    iniFilename_.clear();
}

LRESULT ImGuiManager::HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
}
#endif
