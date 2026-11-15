#include "DebugCamera.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

void DebugCamera::Initialize(uint32_t clientWidth, uint32_t clientHeight)
{
	// カメラの初期姿勢
	translation_ = { 0.0f, 0.0f, -50.0f };
	matRot_ = MakeIdentity4x4();

	// 射影行列の作成
	float aspectRatio = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
	projectionMatrix_ = MakePerspectiveFovMatrix(0.45f, aspectRatio, 0.1f, 100.0f);

	// ビュー行列の初期化
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translation_);
	Matrix4x4 worldMatrix = Multiply(matRot_, translateMatrix);
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

	// 今回のフレーム分の回転角度
	float rotDeltaX = 0.0f; // X軸回転角度
	float rotDeltaY = 0.0f; // Y軸回転角度
	float rotDeltaZ = 0.0f; // Z軸回転角度

	if (isLeftMouseDown)
	{
		rotDeltaY += static_cast<float>(mouseX) * rotateSpeed_;
		rotDeltaX += static_cast<float>(mouseY) * rotateSpeed_;
	}

	// X軸周り回転の入力があったら、X軸周りの角度を加算する
	if (key[DIK_UP])
	{
		rotDeltaX -= keyRotateSpeed_;
	}

	if (key[DIK_DOWN])
	{
		rotDeltaX += keyRotateSpeed_;
	}

	// Y軸周り回転の入力があったら、Y軸周りの角度を加算する
	if (key[DIK_LEFT])
	{
		rotDeltaY -= keyRotateSpeed_;
	}

	if (key[DIK_RIGHT])
	{
		rotDeltaY += keyRotateSpeed_;
	}

	// Z軸周り回転の入力があったら、Z軸周りの角度を加算する
	if (key[DIK_Z])
	{
		rotDeltaZ -= keyRotateSpeed_;
	}

	if (key[DIK_C])
	{
		rotDeltaZ += keyRotateSpeed_;
	}


	// 回転行列の累積
	// 今回フレームでの追加回転分の回転行列を計算する。
	// 累積回転行列に乗算して合成する。

	// 追加回転分の回転行列を生成
	Matrix4x4 matRotDelta = MakeIdentity4x4();
	matRotDelta *= MakeRotateXMatrix(rotDeltaX);
	matRotDelta *= MakeRotateYMatrix(rotDeltaY);
	matRotDelta *= MakeRotateZMatrix(rotDeltaZ);

	// 累積の回転行列を合成
	matRot_ = matRotDelta * matRot_;


	// ---- 前後移動 ----
	if (key[DIK_W]) // 前移動
	{
		const float speed = moveSpeed_;                  // 前進の速さ
		Vector3 move = { 0.0f, 0.0f, speed };            // カメラ移動ベクトル

		move = Transform(move, matRot_);                 // 移動ベクトルを現在の姿勢分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (key[DIK_S]) // 後ろ移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { 0.0f, 0.0f, speed };

		move = Transform(move, matRot_);

		translation_ = Vector3::Add(translation_, move);
	}

	// ---- 左右移動 ----
	if (key[DIK_D]) // 右移動
	{
		const float speed = moveSpeed_;                  // 右移動の速さ
		Vector3 move = { speed, 0.0f, 0.0f };            // カメラ移動ベクトル

		move = Transform(move, matRot_);                 // 移動ベクトルを現在の姿勢分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (key[DIK_A]) // 左移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { speed, 0.0f, 0.0f };

		move = Transform(move, matRot_);

		translation_ = Vector3::Add(translation_, move);
	}

	// ---- 上下移動 ----
	if (key[DIK_E]) // 上移動
	{
		const float speed = moveSpeed_;                   // 上移動の速さ
		Vector3 move = { 0.0f, speed, 0.0f };             // カメラ移動ベクトル

		move = Transform(move, matRot_);                  // 移動ベクトルを現在の姿勢分だけ回転させる

		translation_ = Vector3::Add(translation_, move);  // 移動ベクトル分だけ座標を加算する
	}
	if (key[DIK_Q]) // 下移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { 0.0f, speed, 0.0f };

		move = Transform(move, matRot_);

		translation_ = Vector3::Add(translation_, move);
	}

	// ============================================================
	// ビュー行列の更新
	// 変化した座標や角度をもとにカメラのワールド行列を再計算し、
	// ワールド行列の逆行列をビュー行列に代入する。
	// ============================================================


	// 座標から平行移動行列を計算する
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translation_);

	// 回転行列と平行移動行列からワールド行列を計算する
	Matrix4x4 worldMatrix = matRot_ * translateMatrix;

	// ワールド行列の逆行列をビュー行列に代入する
	viewMatrix_ = Inverse(worldMatrix);
}