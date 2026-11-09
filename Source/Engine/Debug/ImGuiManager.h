#pragma once

#include <Windows.h>
#include <filesystem>
#include <string>

class DirectXBase;
class SRVInformationManager;
struct ImGuiContext;

// Debugで使用するImGuiのライフサイクルを管理する。
class ImGuiManager
{
public:
    ImGuiManager() = default;
    ~ImGuiManager();
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    void Initialize(HWND hwnd, DirectXBase& directXBase, SRVInformationManager& srvManager,
        const std::filesystem::path& iniPath);
    void BeginFrame();
    void EndFrame();
    void Draw() const;
    // 最後の描画のGPU完了後、共有SRVヒープを解放する前に呼ぶ。
    void Finalize();
    static LRESULT HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    ImGuiContext* context_ = nullptr;
    DirectXBase* directXBase_ = nullptr; // 非所有
    std::string iniFilename_; // ImGuiIOが参照する文字列を終了まで保持する。
    bool win32Initialized_ = false;
    bool dx12Initialized_ = false;
};
