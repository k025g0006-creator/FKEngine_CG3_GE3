#include "BaseScene.h"
#include "SceneManager.h"
#include <stdexcept>

void BaseScene::RequestSceneChange(std::string_view sceneName)
{
    if (!sceneManager_) { throw std::logic_error("The scene is not attached to a SceneManager."); }
    sceneManager_->ChangeScene(sceneName);
}
