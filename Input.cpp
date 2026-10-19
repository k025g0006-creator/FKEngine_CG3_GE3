#include "Input.h"
#include <cstring>

bool Input::Initialize(IDirectInput8* directInput, HWND windowHandle)
{
	if (directInput == nullptr || windowHandle == nullptr) return false;
	HRESULT hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
	if (FAILED(hr)) return false;
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	if (FAILED(hr)) { Finalize(); return false; }
	hr = keyboard_->SetCooperativeLevel(windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	if (FAILED(hr)) { Finalize(); return false; }
	return true;
}

void Input::Update()
{
	std::memcpy(previousKeys_, keys_, sizeof(keys_));
	if (keyboard_ == nullptr || FAILED(keyboard_->Acquire()) || FAILED(keyboard_->GetDeviceState(sizeof(keys_), keys_)))
		std::memset(keys_, 0, sizeof(keys_));
}

void Input::Finalize()
{
	if (keyboard_ != nullptr) { keyboard_->Unacquire(); keyboard_->Release(); keyboard_ = nullptr; }
	std::memset(keys_, 0, sizeof(keys_));
	std::memset(previousKeys_, 0, sizeof(previousKeys_));
}
bool Input::PushKey(uint8_t keyNumber) const { return keys_[keyNumber] != 0; }
bool Input::ReleaseKey(uint8_t keyNumber) const { return keys_[keyNumber] == 0; }
bool Input::TriggerKey(uint8_t keyNumber) const { return keys_[keyNumber] != 0 && previousKeys_[keyNumber] == 0; }
bool Input::ReleaseTriggerKey(uint8_t keyNumber) const { return keys_[keyNumber] == 0 && previousKeys_[keyNumber] != 0; }
