#include "Matrix4x4.h"
#include <cmath>

Vector2::Vector2() : x(0.0f), y(0.0f) 
{

}

Vector3::Vector3() : x(0.0f), y(0.0f), z(0.0f)
{

}
Vector3::Vector3(float x, float y, float z) : x(x), y(y), z(z)
{

}

Vector3 Vector3::Add(const Vector3& v1, const Vector3& v2)
{
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

Vector3 Vector3::Subtract(const Vector3& v1, const Vector3& v2)
{
    return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

Vector3 Vector3::Multiply(float scalar, const Vector3& v)
{
    return { scalar * v.x, scalar * v.y, scalar * v.z };
}

float Vector3::Dot(const Vector3& v1, const Vector3& v2)
{
    return (v1.x * v2.x) + (v1.y * v2.y) + (v1.z * v2.z);
}

float Vector3::Length(const Vector3& v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 Vector3::Normalize(const Vector3& v)
{
    float length = Length(v);
    if (length != 0.0f)
    {
        return { v.x / length, v.y / length, v.z / length };
    }
    return { 0.0f, 0.0f, 0.0f };
}

// 平行移動行列の作成
Matrix4x4 MakeTranslateMatrix(const Vector3& translate)
{
    Matrix4x4 result = MakeIdentity4x4();
    result.m[3][0] = translate.x;
    result.m[3][1] = translate.y;
    result.m[3][2] = translate.z;
    return result;
}

// 拡大縮小行列の作成
Matrix4x4 MakeScaleMatrix(const Vector3& scale)
{
    Matrix4x4 result = MakeIdentity4x4();
    result.m[0][0] = scale.x;
    result.m[1][1] = scale.y;
    result.m[2][2] = scale.z;
    return result;
}

// 座標変換（ベクトル * 行列）
Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix)
{
    Vector3 result;
    result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];
    result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];
    result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];
    float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];

    // wで割って同次座標から3D座標に戻す
    if (w != 0.0f)
    {
        result.x /= w;
        result.y /= w;
        result.z /= w;
    }
    return result;
}

Matrix4x4 MakeRotateXMatrix(float radian)
{
    Matrix4x4 result = MakeIdentity4x4();
    float cosTheta = cosf(radian);
    float sinTheta = sinf(radian);
    result.m[1][1] = cosTheta;
    result.m[1][2] = sinTheta;
    result.m[2][1] = -sinTheta;
    result.m[2][2] = cosTheta;
    return result;
}

Matrix4x4 MakeRotateYMatrix(float radian)
{
    Matrix4x4 result = MakeIdentity4x4();
    float cosTheta = cosf(radian);
    float sinTheta = sinf(radian);
    result.m[0][0] = cosTheta;
    result.m[0][2] = -sinTheta;
    result.m[2][0] = sinTheta;
    result.m[2][2] = cosTheta;
    return result;
}

Matrix4x4 MakeRotateZMatrix(float radian)
{
    Matrix4x4 result = MakeIdentity4x4();
    float cosTheta = cosf(radian);
    float sinTheta = sinf(radian);
    result.m[0][0] = cosTheta;
    result.m[0][1] = sinTheta;
    result.m[1][0] = -sinTheta;
    result.m[1][1] = cosTheta;
    return result;
}

Matrix4x4 MakeRotateXYZMatrix(const Vector3& rotate)
{
    Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
    Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
    Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
    return Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate)
{
    Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
    Matrix4x4 rotateMatrix = MakeRotateXYZMatrix(rotate);
    Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
    return Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
{
    Matrix4x4 result = {};
    float f = 1.0f / tanf(fovY / 2.0f);
    result.m[0][0] = f / aspectRatio;
    result.m[1][1] = f;
    result.m[2][2] = farClip / (farClip - nearClip);
    result.m[2][3] = 1.0f;
    result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);
    return result;
}

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip)
{
    Matrix4x4 result = {};
    result.m[0][0] = 2.0f / (right - left);
    result.m[1][1] = 2.0f / (top - bottom);
    result.m[2][2] = 1.0f / (farClip - nearClip);
    result.m[3][0] = -(right + left) / (right - left);
    result.m[3][1] = -(top + bottom) / (top - bottom);
    result.m[3][2] = -nearClip / (farClip - nearClip);
    result.m[3][3] = 1.0f;
    return result;
}

Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2)
{
    Matrix4x4 result;
    for (int i = 0;i < 4;i++)
    {
        for (int j = 0;j < 4;j++)
        {
            result.m[i][j] = m1.m[i][j] + m2.m[i][j];
        }
    }
    return result;
}

Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2)
{
    Matrix4x4 result;
    for (int i = 0;i < 4;i++)
    {
        for (int j = 0;j < 4;j++)
        {
            result.m[i][j] = m1.m[i][j] - m2.m[i][j];
        }
    }
    return result;
}

Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2)
{
    Matrix4x4 result = {};
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                result.m[i][j] += m1.m[i][k] * m2.m[k][j];
            }
        }
    }
    return result;
}

Matrix4x4 Transpose(const Matrix4x4& m)
{
    Matrix4x4 result;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            result.m[i][j] = m.m[j][i];
        }
    }
    return result;
}

Matrix4x4 MakeIdentity4x4()
{
    Matrix4x4 result = {};
    for (int i = 0; i < 4; i++)
    {
        result.m[i][i] = 1.0f;
    }
    return result;
}

Matrix4x4 Inverse(const Matrix4x4& m)
{
    float det =
        m.m[0][0] * m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[0][0] * m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[0][0] * m.m[1][3] * m.m[2][1] * m.m[3][2]
        - m.m[0][0] * m.m[1][3] * m.m[2][2] * m.m[3][1] - m.m[0][0] * m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[0][0] * m.m[1][1] * m.m[2][3] * m.m[3][2]
        - m.m[0][1] * m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[1][0] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[1][0] * m.m[2][1] * m.m[3][2]
        + m.m[0][3] * m.m[1][0] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[0][1] * m.m[1][0] * m.m[2][3] * m.m[3][2]
        + m.m[0][1] * m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[2][0] * m.m[3][2]
        - m.m[0][3] * m.m[1][2] * m.m[2][0] * m.m[3][1] - m.m[0][2] * m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] * m.m[3][2]
        - m.m[0][1] * m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[0][2] * m.m[1][3] * m.m[2][1] * m.m[3][0] - m.m[0][3] * m.m[1][1] * m.m[2][2] * m.m[3][0]
        + m.m[0][3] * m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[0][2] * m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[0][1] * m.m[1][3] * m.m[2][2] * m.m[3][0];

    if (det == 0.0f) return MakeIdentity4x4();

    float detRecp = 1.0f / det;
    Matrix4x4 result;

    // 1行目
    result.m[0][0] = (m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[1][3] * m.m[2][2] * m.m[3][1] - m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[1][1] * m.m[2][3] * m.m[3][2]) * detRecp;
    result.m[0][1] = (m.m[0][3] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[2][1] * m.m[3][3] + m.m[0][1] * m.m[2][3] * m.m[3][2] - m.m[0][1] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[2][1] * m.m[3][2]) * detRecp;
    result.m[0][2] = (m.m[0][1] * m.m[1][2] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[3][2] - m.m[0][3] * m.m[1][2] * m.m[3][1] - m.m[0][2] * m.m[1][1] * m.m[3][3] - m.m[0][1] * m.m[1][3] * m.m[3][2]) * detRecp;
    result.m[0][3] = (m.m[0][3] * m.m[1][2] * m.m[2][1] + m.m[0][2] * m.m[1][1] * m.m[2][3] + m.m[0][1] * m.m[1][3] * m.m[2][2] - m.m[0][1] * m.m[1][2] * m.m[2][3] - m.m[0][2] * m.m[1][3] * m.m[2][1] - m.m[0][3] * m.m[1][1] * m.m[2][2]) * detRecp;

    // 2行目
    result.m[1][0] = (m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[1][3] * m.m[2][2] * m.m[3][0] + m.m[1][0] * m.m[2][3] * m.m[3][2] - m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[1][3] * m.m[2][0] * m.m[3][2] - m.m[1][2] * m.m[2][3] * m.m[3][0]) * detRecp;
    result.m[1][1] = (m.m[0][0] * m.m[2][2] * m.m[3][3] + m.m[0][2] * m.m[2][3] * m.m[3][0] + m.m[0][3] * m.m[2][0] * m.m[3][2] - m.m[0][3] * m.m[2][2] * m.m[3][0] - m.m[0][2] * m.m[2][0] * m.m[3][3] - m.m[0][0] * m.m[2][3] * m.m[3][2]) * detRecp;
    result.m[1][2] = (m.m[0][3] * m.m[1][2] * m.m[3][0] + m.m[0][2] * m.m[1][0] * m.m[3][3] + m.m[0][0] * m.m[1][3] * m.m[3][2] - m.m[0][0] * m.m[1][2] * m.m[3][3] - m.m[0][3] * m.m[1][0] * m.m[3][2] - m.m[0][2] * m.m[1][3] * m.m[3][0]) * detRecp;
    result.m[1][3] = (m.m[0][0] * m.m[1][2] * m.m[2][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] + m.m[0][3] * m.m[1][0] * m.m[2][2] - m.m[0][3] * m.m[1][2] * m.m[2][0] - m.m[0][2] * m.m[1][0] * m.m[2][3] - m.m[0][0] * m.m[1][3] * m.m[2][2]) * detRecp;

    // 3行目
    result.m[2][0] = (m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[1][3] * m.m[2][0] * m.m[3][1] - m.m[1][3] * m.m[2][1] * m.m[3][0] - m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[1][0] * m.m[2][3] * m.m[3][1]) * detRecp;
    result.m[2][1] = (m.m[0][3] * m.m[2][1] * m.m[3][0] + m.m[0][1] * m.m[2][0] * m.m[3][3] + m.m[0][0] * m.m[2][3] * m.m[3][1] - m.m[0][0] * m.m[2][1] * m.m[3][3] - m.m[0][3] * m.m[2][0] * m.m[3][1] - m.m[0][1] * m.m[2][3] * m.m[3][0]) * detRecp;
    result.m[2][2] = (m.m[0][0] * m.m[1][1] * m.m[3][3] + m.m[0][1] * m.m[1][3] * m.m[3][0] + m.m[0][3] * m.m[1][0] * m.m[3][1] - m.m[0][3] * m.m[1][1] * m.m[3][0] - m.m[0][1] * m.m[1][0] * m.m[3][3] - m.m[0][0] * m.m[1][3] * m.m[3][1]) * detRecp;
    result.m[2][3] = (m.m[0][3] * m.m[1][1] * m.m[2][0] + m.m[0][1] * m.m[1][0] * m.m[2][3] + m.m[0][0] * m.m[1][3] * m.m[2][1] - m.m[0][0] * m.m[1][1] * m.m[2][3] - m.m[0][3] * m.m[1][0] * m.m[2][1] - m.m[0][1] * m.m[1][3] * m.m[2][0]) * detRecp;

    // 4行目
    result.m[3][0] = (m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[1][1] * m.m[2][0] * m.m[3][2] + m.m[1][0] * m.m[2][2] * m.m[3][1] - m.m[1][0] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][2] * m.m[3][0] - m.m[1][2] * m.m[2][0] * m.m[3][1]) * detRecp;
    result.m[3][1] = (m.m[0][0] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][2] * m.m[3][0] + m.m[0][2] * m.m[2][0] * m.m[3][1] - m.m[0][2] * m.m[2][1] * m.m[3][0] - m.m[0][1] * m.m[2][0] * m.m[3][2] - m.m[0][0] * m.m[2][2] * m.m[3][1]) * detRecp;
    result.m[3][2] = (m.m[0][2] * m.m[1][1] * m.m[3][0] + m.m[0][1] * m.m[1][0] * m.m[3][2] + m.m[0][0] * m.m[1][2] * m.m[3][1] - m.m[0][0] * m.m[1][1] * m.m[3][2] - m.m[0][2] * m.m[1][0] * m.m[3][1] - m.m[0][1] * m.m[1][2] * m.m[3][0]) * detRecp;
    result.m[3][3] = (m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] + m.m[0][2] * m.m[1][0] * m.m[2][1] - m.m[0][2] * m.m[1][1] * m.m[2][0] - m.m[0][1] * m.m[1][0] * m.m[2][2] - m.m[0][0] * m.m[1][2] * m.m[2][1]) * detRecp;

    return result;
}