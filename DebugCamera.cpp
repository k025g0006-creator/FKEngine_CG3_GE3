#include "DebugCamera.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

void DebugCamera::Initialize(uint32_t clientWidth, uint32_t clientHeight)
{
	// カメラの初期姿勢
	rotation_ = { 0.0f, 0.0f, 0.0f };
	translation_ = { 0.0f, 0.0f, -50.0f };

	// 射影行列の作成
	float aspectRatio = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
	projectionMatrix_ = MakePerspectiveFovMatrix(0.45f, aspectRatio, 0.1f, 100.0f);

	// ビュー行列の初期化
	Matrix4x4 rotateMatrix = MakeRotateXYZMatrix(rotation_);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translation_);
	Matrix4x4 worldMatrix = Multiply(rotateMatrix, translateMatrix);
	viewMatrix_ = Inverse(worldMatrix);
}

void DebugCamera::Update(const uint8_t* key, long mouseX, long mouseY, bool isLeftMouseDown)
{
	if (key == nullptr)
	{
		return;
	}

	// ============================================================
	// 入力によるカメラの移動や回転
	// ============================================================

	if (isLeftMouseDown)
	{
		rotation_.y += static_cast<float>(mouseX) * rotateSpeed_;
		rotation_.x += static_cast<float>(mouseY) * rotateSpeed_;
	}

	// X軸周り回転の入力があったら、X軸周りの角度を加算する
	if (key[DIK_UP]) 
	{ 
		rotation_.x -= keyRotateSpeed_;
	}

	if (key[DIK_DOWN])
	{ 
		rotation_.x += keyRotateSpeed_; 
	}

	// Y軸周り回転の入力があったら、Y軸周りの角度を加算する
	if (key[DIK_LEFT])
	{
		rotation_.y -= keyRotateSpeed_;
	}

	if (key[DIK_RIGHT])
	{
		rotation_.y += keyRotateSpeed_; 
	}

	// Z軸周り回転の入力があったら、Z軸周りの角度を加算する
	if (key[DIK_Z])
	{
		rotation_.z -= keyRotateSpeed_;
	}

	if (key[DIK_C])
	{ 
		rotation_.z += keyRotateSpeed_;
	}

	// ピッチ角を制限する（真上・真下を超えて反転しないように）
	if (rotation_.x > pitchLimit_)
	{
		rotation_.x = pitchLimit_;
	}
	if (rotation_.x < -pitchLimit_)
	{
		rotation_.x = -pitchLimit_;
	}

	// 現フレームの向きを表す回転行列（移動ベクトルの回転に使用）
	Matrix4x4 inputRotateMatrix = MakeRotateXYZMatrix(rotation_);

	// ---- 前後移動 ----
	if (key[DIK_W]) // 前移動
	{
		const float speed = moveSpeed_; // 前進の速さ
		Vector3 move = { 0.0f, 0.0f, speed }; // カメラ移動ベクトル

		move = Transform(move, inputRotateMatrix); // 移動ベクトルを角度分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (key[DIK_S]) // 後ろ移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { 0.0f, 0.0f, speed };

		move = Transform(move, inputRotateMatrix);

		translation_ = Vector3::Add(translation_, move);
	}

	// ---- 左右移動 ----
	if (key[DIK_D]) // 右移動
	{
		const float speed = moveSpeed_; // 右移動の速さ
		Vector3 move = { speed, 0.0f, 0.0f }; // カメラ移動ベクトル

		move = Transform(move, inputRotateMatrix); // 移動ベクトルを角度分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (key[DIK_A]) // 左移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { speed, 0.0f, 0.0f };

		move = Transform(move, inputRotateMatrix);

		translation_ = Vector3::Add(translation_, move);
	}

	// ---- 上下移動 ----
	if (key[DIK_E]) // 上移動
	{
		const float speed = moveSpeed_; // 上移動の速さ
		Vector3 move = { 0.0f, speed, 0.0f }; // カメラ移動ベクトル

		move = Transform(move, inputRotateMatrix); // 移動ベクトルを角度分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (key[DIK_Q]) // 下移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { 0.0f, speed, 0.0f };

		move = Transform(move, inputRotateMatrix);

		translation_ = Vector3::Add(translation_, move);
	}

	// ============================================================
	// ビュー行列の更新
	// 変化した座標や角度をもとにカメラのワールド行列を再計算し、
	// ワールド行列の逆行列をビュー行列に代入する。
	// ============================================================

	// 角度から回転行列を計算する
	Matrix4x4 rotateMatrix = MakeRotateXYZMatrix(rotation_);

	// 座標から平行移動行列を計算する
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translation_);

	// 回転行列と平行移動行列からワールド行列を計算する
	Matrix4x4 worldMatrix = Multiply(rotateMatrix, translateMatrix);

	// ワールド行列の逆行列をビュー行列に代入する
	viewMatrix_ = Inverse(worldMatrix);
}
