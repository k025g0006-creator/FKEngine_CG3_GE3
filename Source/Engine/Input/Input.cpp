#include "Input.h"
#include <cstring>
#include <utility>

Input::~Input()
{
	Finalize();
}

bool Input::Initialize(IDirectInput8* directInput, HWND windowHandle)
{
	if (directInput == nullptr || windowHandle == nullptr) return false;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard;
	HRESULT hr = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), nullptr);
	if (FAILED(hr)) return false;
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	if (FAILED(hr)) return false;
	hr = keyboard->SetCooperativeLevel(windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	if (FAILED(hr)) return false;

	Finalize();
	keyboard_ = std::move(keyboard);
	return true;
}

void Input::Update()
{
	std::memcpy(previousKeys_, keys_, sizeof(keys_));
	if (!keyboard_ || FAILED(keyboard_->Acquire()) || FAILED(keyboard_->GetDeviceState(sizeof(keys_), keys_)))
		std::memset(keys_, 0, sizeof(keys_));
}

void Input::Finalize()
{
	if (keyboard_) {
		keyboard_->Unacquire();
		keyboard_.Reset();
	}
	std::memset(keys_, 0, sizeof(keys_));
	std::memset(previousKeys_, 0, sizeof(previousKeys_));
}
bool Input::PushKey(uint8_t keyNumber) const { return keys_[keyNumber] != 0; }
bool Input::ReleaseKey(uint8_t keyNumber) const { return keys_[keyNumber] == 0; }
bool Input::TriggerKey(uint8_t keyNumber) const { return keys_[keyNumber] != 0 && previousKeys_[keyNumber] == 0; }
bool Input::ReleaseTriggerKey(uint8_t keyNumber) const { return keys_[keyNumber] == 0 && previousKeys_[keyNumber] != 0; }
