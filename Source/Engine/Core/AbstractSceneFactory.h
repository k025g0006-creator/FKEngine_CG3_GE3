#pragma once

#include <cstdint>
#include <memory>

class BaseScene;
enum class SceneId : std::uint32_t;

class AbstractSceneFactory
{
public:
    virtual ~AbstractSceneFactory() = default;
    virtual std::unique_ptr<BaseScene> CreateScene(SceneId sceneId) = 0;
};
