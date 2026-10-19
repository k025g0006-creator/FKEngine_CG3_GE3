#pragma once
#include <Windows.h>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <cstdint>
#pragma comment(lib, "dinput8.lib")

class Input
{
public:
	bool Initialize(IDirectInput8* directInput, HWND windowHandle);
	void Update();
	void Finalize();
	bool PushKey(uint8_t keyNumber) const;
	bool ReleaseKey(uint8_t keyNumber) const;
	bool TriggerKey(uint8_t keyNumber) const;
	bool ReleaseTriggerKey(uint8_t keyNumber) const;
	const BYTE* GetKeys() const { return keys_; }
private:
	IDirectInputDevice8* keyboard_ = nullptr;
	BYTE keys_[256]{};
	BYTE previousKeys_[256]{};
};
