#pragma once

#include "AbstractSceneFactory.h"

class SceneFactory final : public AbstractSceneFactory
{
public:
    std::unique_ptr<BaseScene> CreateScene(SceneId sceneId) override;
};
