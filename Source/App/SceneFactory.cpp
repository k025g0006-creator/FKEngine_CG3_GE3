#include "SceneFactory.h"
#include "GameScene.h"
#include "SceneId.h"
#include "TitleScene.h"

std::unique_ptr<BaseScene> SceneFactory::CreateScene(SceneId sceneId)
{
    switch (sceneId) {
    case SceneId::Title: return std::make_unique<TitleScene>();
    case SceneId::Gameplay: return std::make_unique<GameScene>();
    }
    return nullptr;
}
