#include "SceneFactory.h"
#include "GameScene.h"
#include "SceneNames.h"
#include "TitleScene.h"

std::unique_ptr<BaseScene> SceneFactory::CreateScene(std::string_view sceneName)
{
    if (sceneName == SceneNames::Title) { return std::make_unique<TitleScene>(); }
    if (sceneName == SceneNames::Gameplay) { return std::make_unique<GameScene>(); }
    return nullptr;
}
