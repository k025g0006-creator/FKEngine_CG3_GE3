#pragma once
#include "FPFramework.h"

class Game final : public FPFramework
{
private:
    bool Initialize() override;
    void Draw() override;
    void OnFrameComplete() override;
    enum class SmokePhase { Title, Gameplay, ReturnToTitle };
    SmokePhase smokePhase_ = SmokePhase::Title;
#ifdef USE_IMGUI
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> smokeHeap_;
#endif
};
