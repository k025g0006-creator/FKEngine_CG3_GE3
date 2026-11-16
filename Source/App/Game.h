#pragma once
#include "FPFramework.h"
#include <memory>

class Game final : public FPFramework
{
public:
    Game();
    ~Game() override;

private:
    bool Initialize() override;
    bool Update(float deltaTime) override;
    void Draw() override;
    void Finalize() override;
    void OnFrameComplete() override;
    void PlaySe();
    struct Runtime;
    std::unique_ptr<Runtime> runtime_;
};
