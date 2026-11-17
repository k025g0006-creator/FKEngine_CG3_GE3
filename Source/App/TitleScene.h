#pragma once

#include "BaseScene.h"
#include "Sprite.h"
#include <optional>

class TitleScene final : public BaseScene
{
public:
    bool Initialize(const SceneContext& context) override;
    void UpdateInput(const Input& input, long, long, bool) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void Finalize() override;

private:
    SpriteBase* spriteBase_ = nullptr;
    std::optional<Sprite> titleSprite_;
    uint32_t textureIndex_ = UINT32_MAX;
    std::ostream* smokeLog_ = nullptr;
};
