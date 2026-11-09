#include "ViewCamera.h"
#include "WinApp.h"
#include <cmath>
#include <numbers>
#include <stdexcept>

ViewCamera::ViewCamera()
    : aspectRatio_(static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight))
{
    Update();
}

void ViewCamera::SetPerspective(float fovY, float aspectRatio, float nearClip, float farClip)
{
    if (!std::isfinite(fovY) || !std::isfinite(aspectRatio) || !std::isfinite(nearClip) ||
        !std::isfinite(farClip) || fovY <= 0.0f || fovY >= std::numbers::pi_v<float> ||
        aspectRatio <= 0.0f || nearClip <= 0.0f || farClip <= nearClip) {
        throw std::invalid_argument("Invalid camera perspective settings.");
    }
    fovY_ = fovY;
    aspectRatio_ = aspectRatio;
    nearClip_ = nearClip;
    farClip_ = farClip;
}

void ViewCamera::Update()
{
    UpdateFromWorldMatrix(Math::MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate));
}

void ViewCamera::UpdateFromWorldMatrix(const Matrix4x4& worldMatrix)
{
    worldMatrix_ = worldMatrix;
    viewMatrix_ = Math::Inverse(worldMatrix_);
    projectionMatrix_ = Math::MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    viewProjectionMatrix_ = Math::Multiply(viewMatrix_, projectionMatrix_);
}

Vector3 ViewCamera::GetWorldPosition() const
{
    return { worldMatrix_.m[3][0], worldMatrix_.m[3][1], worldMatrix_.m[3][2] };
}
