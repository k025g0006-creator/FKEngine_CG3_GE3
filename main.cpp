#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>

void Log(const std::string& message)
{
	OutputDebugStringA(message.c_str());
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){

	switch (msg) {

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	WNDCLASS wc{};

	wc.lpfnWndProc = WindowProc;

	wc.lpszClassName = L"CG02WindowClass";

	wc.hInstance = GetModuleHandle(nullptr);

	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);

	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		L"CG2-01",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr);

	ShowWindow(hwnd, nCmdShow);

	MSG msg{};

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{

		}
	}

	// 文字列を格納する
	std::string str0{ "STRING!!!" };

	// 整数を文字列にする
	std::string str1{ std::to_string(10) };

	// string->wstring
	std::wstring ConvertString(const std::string & str);

	// wstring->string
	std::string ConvertString(const std::wstring & str);

	// 出力ウィンドウへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");


	return 0;
}