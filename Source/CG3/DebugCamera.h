#pragma once
#include "Matrix4x4.h"
#include <cstdint>

class DebugCamera
{
public:

	
	void Initialize(uint32_t clientWidth, uint32_t clientHeight);

	
	void Update(const uint8_t* key, long mouseX, long mouseY, bool isLeftMouseDown);

	// ビュー行列を取得
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }

	// 射影行列を取得
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

	// カメラ座標の取得・設定
	const Vector3& GetTranslation() const { return translation_; }

	void SetTranslation(const Vector3& translation) { translation_ = translation; }


private:

	// ローカル座標
	Vector3 translation_ = { 0.0f, 0.0f, -50.0f };

	// ビュー行列
	Matrix4x4 viewMatrix_{};

	// 射影行列
	Matrix4x4 projectionMatrix_{};

	// 累積回転行列
	Matrix4x4 matRot_ = MakeIdentity4x4();

	// 移動速度
	float moveSpeed_ = 0.2f;

	// マウス回転速度
	float rotateSpeed_ = 0.003f;

	// キー入力による回転速度
	float keyRotateSpeed_ = 0.02f;
};
