#pragma once

#include "Matrix4x4.h"
#include "Transform.h"

class ViewCamera
{
public:
    ViewCamera();
    // 設定を変更した後、オブジェクトの更新より先に呼ぶ。
    void Update();
    // 累積回転で操作するデバッグカメラなどから、計算済みの姿勢を受け取る。
    void UpdateFromWorldMatrix(const Matrix4x4& worldMatrix);
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    void SetPerspective(float fovY, float aspectRatio, float nearClip, float farClip);
    void SetFovY(float fovY) { SetPerspective(fovY, aspectRatio_, nearClip_, farClip_); }
    void SetAspectRatio(float ratio) { SetPerspective(fovY_, ratio, nearClip_, farClip_); }
    void SetNearClip(float nearClip) { SetPerspective(fovY_, aspectRatio_, nearClip, farClip_); }
    void SetFarClip(float farClip) { SetPerspective(fovY_, aspectRatio_, nearClip_, farClip); }

    const Vector3& GetRotate() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }
    float GetFovY() const { return fovY_; }
    float GetAspectRatio() const { return aspectRatio_; }
    float GetNearClip() const { return nearClip_; }
    float GetFarClip() const { return farClip_; }
    const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
    Vector3 GetWorldPosition() const;

private:
    FKEngine::Transform transform_;
    float fovY_ = 0.45f; // 垂直視野角（ラジアン）
    float aspectRatio_;
    float nearClip_ = 0.1f;
    float farClip_ = 100.0f;
    Matrix4x4 worldMatrix_{};
    Matrix4x4 viewMatrix_{};
    Matrix4x4 projectionMatrix_{};
    Matrix4x4 viewProjectionMatrix_{};
};
