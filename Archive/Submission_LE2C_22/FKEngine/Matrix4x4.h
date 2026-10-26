#pragma once

// 2次元ベクトルクラス
class Vector2
{
public:
    float x, y;

    Vector2();
    Vector2(float x, float y);
};

class Vector3
{
public:
    float x, y, z;

    Vector3();
    Vector3(float x, float y, float z);

    static Vector3 Add(const Vector3& v1, const Vector3& v2);
    static Vector3 Subtract(const Vector3& v1, const Vector3& v2);
    static Vector3 Multiply(float scalar, const Vector3& v);
    static float Dot(const Vector3& v1, const Vector3& v2);
    static float Length(const Vector3& v);
    static Vector3 Normalize(const Vector3& v);
};

struct Matrix4x4
{
    float m[4][4];
};

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

// 積
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);
Matrix4x4& operator*=(Matrix4x4& m1, const Matrix4x4& m2);