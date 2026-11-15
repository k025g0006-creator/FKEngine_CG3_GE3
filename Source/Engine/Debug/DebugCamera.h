#pragma once
#include "ViewCamera.h"
#include <cstdint>

class Input;

class DebugCamera
{
public:

	
	void Initialize(uint32_t clientWidth, uint32_t clientHeight);

	
	void Update(const Input* input, long mouseX, long mouseY, bool isLeftMouseDown);

	// ビュー行列を取得
	const Matrix4x4& GetViewMatrix() const { return camera_.GetViewMatrix(); }

	// 射影行列を取得
	const Matrix4x4& GetProjectionMatrix() const { return camera_.GetProjectionMatrix(); }

	// カメラ座標の取得・設定
	const Vector3& GetTranslation() const { return translation_; }

	void SetTranslation(const Vector3& translation);
	const ViewCamera& GetViewCamera() const { return camera_; }


private:

	// ローカル座標
	Vector3 translation_ = { 0.0f, 0.0f, -50.0f };

	ViewCamera camera_;
	void UpdateView();

	// 累積回転行列
	Matrix4x4 matRot_ = Math::MakeIdentity4x4();

	// 移動速度
	float moveSpeed_ = 0.2f;

	// マウス回転速度
	float rotateSpeed_ = 0.003f;

	// キー入力による回転速度
	float keyRotateSpeed_ = 0.02f;
};
