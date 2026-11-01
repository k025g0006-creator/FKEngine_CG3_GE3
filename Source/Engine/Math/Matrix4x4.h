#pragma once
#include "Vector2.h"
#include "Vector3.h"

struct Matrix4x4
{
    float m[4][4];
};

// 軸平行境界ボックス（Axis Aligned Bounding Box）
struct AABB
{
    Vector3 min;
    Vector3 max;
};

namespace Math
{
// 加法
Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);

// 減法
Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);

// 積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

// 逆行列
Matrix4x4 Inverse(const Matrix4x4& m);

// 転置行列
Matrix4x4 Transpose(const Matrix4x4& m);

// 単位行列の作成
Matrix4x4 MakeIdentity4x4();

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

// 拡大縮小行列
Matrix4x4 MakeScaleMatrix(const Vector3& scale);

// 座標変換
Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);

// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);

// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

// XYZ軸回転行列
Matrix4x4 MakeRotateXYZMatrix(const Vector3& rotate);

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

bool IsCollision(const AABB& aabb, const Vector3& point);

}
// Matrix4x4と同じスコープで演算子を公開し、内部計算はMathに委譲する。
// 積
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);
Matrix4x4& operator*=(Matrix4x4& m1, const Matrix4x4& m2);
