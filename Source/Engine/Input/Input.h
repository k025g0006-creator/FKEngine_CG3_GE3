#pragma once
#include <Windows.h>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <wrl/client.h>
#include <cstdint>
#pragma comment(lib, "dinput8.lib")

class Input
{
public:
	Input() = default;
	~Input();
	Input(const Input&) = delete;
	Input& operator=(const Input&) = delete;

	bool Initialize(IDirectInput8* directInput, HWND windowHandle);
	void Update();
	void Finalize();
	bool PushKey(uint8_t keyNumber) const;
	bool ReleaseKey(uint8_t keyNumber) const;
	bool TriggerKey(uint8_t keyNumber) const;
	bool ReleaseTriggerKey(uint8_t keyNumber) const;
private:
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	BYTE keys_[256]{};
	BYTE previousKeys_[256]{};
};
