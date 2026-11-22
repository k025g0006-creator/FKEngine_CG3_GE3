#include "BaseScene.h"
#include "Logger.h"
#include "SceneManager.h"

bool BaseScene::RequestSceneChange(SceneId sceneId)
{
    if (!sceneManager_) {
        Logger::Log("Scene change skipped: the scene is not attached to a SceneManager.");
        return false;
    }
    return sceneManager_->ChangeScene(sceneId);
}
