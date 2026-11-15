#include "Vector3.h"
#include <cmath>

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

