#include "TitleScene.h"

#include "Input.h"
#include "Logger.h"
#include "SceneNames.h"
#include "SpriteBase.h"
#include "StringUtility.h"
#include "TextureManager.h"

bool TitleScene::Initialize(const SceneContext& context)
{
    spriteBase_ = &context.spriteBase;
    auto* textures = TextureManager::GetInstance();
    const auto path = StringUtility::ConvertString((context.resourceDirectory / "Title.png").native());
    textures->LoadTexture(path);
    textureIndex_ = textures->GetTextureIndexByFilePath(path);

    titleSprite_.emplace();
    titleSprite_->Initialize(spriteBase_, textureIndex_);
    titleSprite_->SetAnchorPoint({0.0f, 0.0f});
    titleSprite_->SetSize({spriteBase_->GetScreenWidth(), spriteBase_->GetScreenHeight()});
    smokeLog_ = context.smokeTest ? &context.log : nullptr;
    return true;
}

void TitleScene::UpdateInput(const Input& input, long, long, bool)
{
    if (!smokeLog_ && (input.TriggerKey(DIK_RETURN) || input.TriggerKey(DIK_NUMPADENTER)))
    {
        RequestSceneChange(SceneNames::Gameplay);
    }
}

void TitleScene::Update(float)
{
    titleSprite_->Update();
}

void TitleScene::Draw()
{
    spriteBase_->SetCommonRenderState();
    titleSprite_->Draw();
    if (smokeLog_)
    {
        Logger::Log(*smokeLog_, "Title scene smoke: title sprite drawn.");
    }
}

void TitleScene::Finalize()
{
    titleSprite_.reset();
    if (textureIndex_ != UINT32_MAX)
    {
        TextureManager::GetInstance()->UnloadTexture(textureIndex_);
        textureIndex_ = UINT32_MAX;
    }
    if (smokeLog_)
    {
        Logger::Log(*smokeLog_, "Title scene smoke: finalized.");
    }
}
