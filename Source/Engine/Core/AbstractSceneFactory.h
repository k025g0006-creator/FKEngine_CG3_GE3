#pragma once

#include <memory>
#include <string_view>

class BaseScene;

class AbstractSceneFactory
{
public:
    virtual ~AbstractSceneFactory() = default;
    virtual std::unique_ptr<BaseScene> CreateScene(std::string_view sceneName) = 0;
};
