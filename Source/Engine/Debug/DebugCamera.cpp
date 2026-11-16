#include "DebugCamera.h"
#include "Input.h"

namespace
{
void OrthonormalizeRotation(Matrix4x4& rotation)
{
    const auto right = Vector3::Normalize({ rotation.m[0][0], rotation.m[0][1], rotation.m[0][2] });
    const Vector3 upRow{ rotation.m[1][0], rotation.m[1][1], rotation.m[1][2] };
    const auto up = Vector3::Normalize(Vector3::Subtract(upRow, Vector3::Multiply(Vector3::Dot(upRow, right), right)));
    const Vector3 forward{
        right.y * up.z - right.z * up.y,
        right.z * up.x - right.x * up.z,
        right.x * up.y - right.y * up.x
    };
    rotation = {{
        {right.x, right.y, right.z, 0.0f},
        {up.x, up.y, up.z, 0.0f},
        {forward.x, forward.y, forward.z, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    }};
}
}

void DebugCamera::Initialize(uint32_t clientWidth, uint32_t clientHeight)
{
	// カメラの初期姿勢
	translation_ = { 0.0f, 0.0f, -50.0f };
	matRot_ = Math::MakeIdentity4x4();

	camera_.SetAspectRatio(static_cast<float>(clientWidth) / static_cast<float>(clientHeight));
	UpdateView();
}

void DebugCamera::Update(const Input* input, long mouseX, long mouseY, bool isLeftMouseDown)
{
	if (input == nullptr)
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
	if (input->PushKey(DIK_UP))
	{
		rotDeltaX -= keyRotateSpeed_;
	}

	if (input->PushKey(DIK_DOWN))
	{
		rotDeltaX += keyRotateSpeed_;
	}

	// Y軸周り回転の入力があったら、Y軸周りの角度を加算する
	if (input->PushKey(DIK_LEFT))
	{
		rotDeltaY -= keyRotateSpeed_;
	}

	if (input->PushKey(DIK_RIGHT))
	{
		rotDeltaY += keyRotateSpeed_;
	}

	// Z軸周り回転の入力があったら、Z軸周りの角度を加算する
	if (input->PushKey(DIK_Z))
	{
		rotDeltaZ -= keyRotateSpeed_;
	}

	if (input->PushKey(DIK_C))
	{
		rotDeltaZ += keyRotateSpeed_;
	}


	if (rotDeltaX != 0.0f || rotDeltaY != 0.0f || rotDeltaZ != 0.0f)
	{
		const auto delta = Math::MakeRotateXMatrix(rotDeltaX) * Math::MakeRotateYMatrix(rotDeltaY) *
			Math::MakeRotateZMatrix(rotDeltaZ);
		matRot_ = delta * matRot_;
		// 累積乗算で蓄積する丸め誤差を補正する。
		OrthonormalizeRotation(matRot_);
	}


	// ---- 前後移動 ----
	if (input->PushKey(DIK_W)) // 前移動
	{
		const float speed = moveSpeed_;                  // 前進の速さ
		Vector3 move = { 0.0f, 0.0f, speed };            // カメラ移動ベクトル

		move = Math::Transform(move, matRot_);                 // 移動ベクトルを現在の姿勢分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (input->PushKey(DIK_S)) // 後ろ移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { 0.0f, 0.0f, speed };

		move = Math::Transform(move, matRot_);

		translation_ = Vector3::Add(translation_, move);
	}

	// ---- 左右移動 ----
	if (input->PushKey(DIK_D)) // 右移動
	{
		const float speed = moveSpeed_;                  // 右移動の速さ
		Vector3 move = { speed, 0.0f, 0.0f };            // カメラ移動ベクトル

		move = Math::Transform(move, matRot_);                 // 移動ベクトルを現在の姿勢分だけ回転させる

		translation_ = Vector3::Add(translation_, move); // 移動ベクトル分だけ座標を加算する
	}
	if (input->PushKey(DIK_A)) // 左移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { speed, 0.0f, 0.0f };

		move = Math::Transform(move, matRot_);

		translation_ = Vector3::Add(translation_, move);
	}

	// ---- 上下移動 ----
	if (input->PushKey(DIK_E)) // 上移動
	{
		const float speed = moveSpeed_;                   // 上移動の速さ
		Vector3 move = { 0.0f, speed, 0.0f };             // カメラ移動ベクトル

		move = Math::Transform(move, matRot_);                  // 移動ベクトルを現在の姿勢分だけ回転させる

		translation_ = Vector3::Add(translation_, move);  // 移動ベクトル分だけ座標を加算する
	}
	if (input->PushKey(DIK_Q)) // 下移動
	{
		const float speed = -moveSpeed_;
		Vector3 move = { 0.0f, speed, 0.0f };

		move = Math::Transform(move, matRot_);

		translation_ = Vector3::Add(translation_, move);
	}

	UpdateView();
}

void DebugCamera::SetTranslation(const Vector3& translation)
{
	translation_ = translation;
	UpdateView();
}

void DebugCamera::UpdateView()
{
	camera_.UpdateFromWorldMatrix(matRot_ * Math::MakeTranslateMatrix(translation_));
}
