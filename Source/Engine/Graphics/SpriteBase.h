#pragma once

#include <d3d12.h>
#include <wrl.h>
#include "Matrix4x4.h"
#include <cstdint>

class DirectXBase;

// 全スプライトで共有する描画設定。ゲーム中に1つ生成し、Spriteより長く保持する。
class SpriteBase
{
public:
    SpriteBase() = default;
    SpriteBase(const SpriteBase&) = delete;
    SpriteBase& operator=(const SpriteBase&) = delete;

    // mainが所有するDirectXBaseを借りて、共通の描画ルールを一度だけ作成する。
    void Initialize(DirectXBase* directXBase, float screenWidth, float screenHeight);
    // DirectXBase::PreDrawの後、すべてのSprite::Drawの直前に1回呼ぶ。
    void SetCommonRenderState() const;

    DirectXBase* GetDirectXBase() const { return directXBase_; }
    float GetScreenWidth() const { return screenWidth_; }
    float GetScreenHeight() const { return screenHeight_; }
    // 論理解像度が変わった場合だけ再計算する。GPUのviewport変更はDirectXBase側の役割。
    void SetScreenSize(float width, float height);
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    uint64_t GetProjectionRevision() const { return projectionRevision_; }

private:
    void CreateRootSignature();
    // 先にCreateRootSignatureを呼び、生成したルートシグネチャでPSOを作成する。
    void CreateGraphicsPipelineState();

    DirectXBase* directXBase_ = nullptr; // 所有しない
    float screenWidth_ = 0.0f;
    float screenHeight_ = 0.0f;
    Matrix4x4 projectionMatrix_ = Math::MakeIdentity4x4();
    uint64_t projectionRevision_ = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
