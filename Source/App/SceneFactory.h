#pragma once

#include "AbstractSceneFactory.h"

class SceneFactory final : public AbstractSceneFactory
{
public:
    std::unique_ptr<BaseScene> CreateScene(std::string_view sceneName) override;
};
