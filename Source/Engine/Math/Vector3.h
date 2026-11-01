#pragma once

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
