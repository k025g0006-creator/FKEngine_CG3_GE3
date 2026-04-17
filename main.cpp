#include <Windows.h>

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	// 出力ウィンドウへの文字出力
	OutputDebugStringA("Hallo,DirectX!\n");

	return 0;
}