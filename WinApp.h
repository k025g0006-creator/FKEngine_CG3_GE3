#pragma once
#include <Windows.h>
#include <cstdint>

class WinApp
{
public:
	// クライアント領域のサイズ
	static constexpr int32_t kClientWidth = 1280;
	static constexpr int32_t kClientHeight = 720;

	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// ウィンドウの生成と表示
	void Initialize();

	// ウィンドウの終了処理
	void Finalize();

	// メッセージ処理。終了メッセージ(WM_QUIT)が来たらtrueを返す
	bool ProcessMessage();

	// ウィンドウハンドルの取得
	HWND GetHwnd() const { return hwnd_; }

	// インスタンスハンドルの取得
	HINSTANCE GetHInstance() const { return wc_.hInstance; }

private:
	// ウィンドウハンドル
	HWND hwnd_ = nullptr;

	// ウィンドウクラス
	WNDCLASS wc_{};
};