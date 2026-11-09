#include "DemoScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <cmath>
#include <numbers>

namespace
{
Model::ModelData MakeSphereModel()
{
    constexpr uint32_t subdivisions = 32;
    constexpr float pi = std::numbers::pi_v<float>;
    const float latitudeStep = pi / subdivisions;
    const float longitudeStep = 2.0f * pi / subdivisions;
    Model::ModelData model;
    model.material.textureFilePath = "Resources/monsterBall.png";
    model.vertices.reserve(subdivisions * subdivisions * 6);
    const auto vertex = [](float theta, float phi, float u, float v) -> Model::VertexData {
        const Vector3 position{std::cos(theta) * std::cos(phi), std::sin(theta), std::cos(theta) * std::sin(phi)};
        return { {position.x, position.y, position.z, 1.0f}, {u, v}, position };
    };
    for (uint32_t latitude = 0; latitude < subdivisions; ++latitude) {
        const float theta = -pi / 2.0f + static_cast<float>(latitude) * latitudeStep;
        const float v1 = 1.0f - static_cast<float>(latitude) / subdivisions;
        const float v2 = 1.0f - static_cast<float>(latitude + 1) / subdivisions;
        for (uint32_t longitude = 0; longitude < subdivisions; ++longitude) {
            const float phi = static_cast<float>(longitude) * longitudeStep + pi;
            const float u1 = static_cast<float>(longitude) / subdivisions;
            const float u2 = static_cast<float>(longitude + 1) / subdivisions;
            const auto a = vertex(theta, phi, u1, v1);
            const auto b = vertex(theta + latitudeStep, phi, u1, v2);
            const auto c = vertex(theta, phi + longitudeStep, u2, v1);
            const auto d = vertex(theta + latitudeStep, phi + longitudeStep, u2, v2);
            model.vertices.insert(model.vertices.end(), { a, b, d, a, d, c });
        }
    }
    return model;
}
}

void DemoScene::Initialize(DirectXBase& directXBase, SRVInformationManager& srvManager, bool smokeTest, std::ostream& log)
{
    directXBase_ = &directXBase;
    srvManager_ = &srvManager;
    textureManager_ = TextureManager::GetInstance();
    InitializeParticlePipeline(log);
    InitializeModels();
    if (smokeTest) { CheckModels(log); }

    textureManager_->LoadTexture("Resources/uvChecker.png");
    textureManager_->LoadTexture("Resources/circle.png");
    textureManager_->LoadTexture("Resources/monsterBall.png");
    if (smokeTest) { CheckTextures(log); }

    InitializeSprites();
    if (smokeTest) { CheckSprites(log); }
    InitializeParticleBuffers();
}

void DemoScene::InitializeModels()
{
    modelManager_ = ModelManager::GetInstance();
    modelManager_->Initialize(directXBase_);
    modelManager_->LoadModel("plane.obj");
    modelManager_->LoadModel("terrain.obj");
    planeModel_ = modelManager_->FindModel("plane.obj");
    terrainModel_ = modelManager_->FindModel("terrain.obj");
    auto& defaultCamera = cameras_.AddCamera("標準");
    defaultCamera.SetTranslate({0.0f, 6.0f, -16.0f});
    defaultCamera.SetRotate({0.38f, 0.0f, 0.0f});
    defaultCamera.Update();
    auto& sideCamera = cameras_.AddCamera("斜め");
    sideCamera.SetTranslate({12.0f, 6.0f, -15.0f});
    sideCamera.SetRotate({0.3f, -0.65f, 0.0f});
    sideCamera.Update();
    auto& overheadCamera = cameras_.AddCamera("俯瞰");
    overheadCamera.SetTranslate({0.0f, 20.0f, -7.0f});
    overheadCamera.SetRotate({1.2f, 0.0f, 0.0f});
    overheadCamera.Update();

    planeObject_.Initialize(directXBase_, &defaultCamera);
    planeObject_.SetModel("plane.obj");
    terrainObject_.Initialize(directXBase_, &defaultCamera);
    terrainObject_.SetModel("terrain.obj");
    terrainObject_.GetTransform() = {}; // 地形は原点・等倍のまま描く。
    sphereAppearance_.Initialize(modelManager_->GetModelBase(), MakeSphereModel());
    sphereObject_.Initialize(directXBase_, &defaultCamera);
    sphereObject_.SetModel(&sphereAppearance_);
    secondSphere_.Initialize(directXBase_, &defaultCamera);
    secondSphere_.SetModel(&sphereAppearance_);
    secondSphere_.SetTranslate({3.0f, 1.0f, 0.0f});
    debugCamera_.Initialize(WinApp::kClientWidth, WinApp::kClientHeight);
    debugCamera_.SetTranslation(defaultCamera.GetTranslate());
}

void DemoScene::InitializeSprites()
{
    spriteBase_.Initialize(directXBase_, float(WinApp::kClientWidth), float(WinApp::kClientHeight));
    CreateSprite(spriteTextures[0], {16.0f, 16.0f}, {160.0f, 160.0f});
    CreateSprite(spriteTextures[1], {192.0f, 16.0f}, {96.0f, 96.0f});
}

Sprite& DemoScene::CreateSprite(const std::string& texture, const Vector2& position, const Vector2& size)
{
    auto sprite = std::make_unique<Sprite>();
    if (texture.empty()) { sprite->Initialize(&spriteBase_); }
    else { sprite->Initialize(&spriteBase_, texture); }
    sprite->SetPosition(position);
    sprite->SetSize(size);
    Sprite& result = *sprite;
    sprites_.push_back(std::move(sprite));
    return result;
}

Sprite& DemoScene::DuplicateSprite(std::size_t index)
{
    const Sprite& source = *sprites_.at(index);
    Sprite& copy = CreateSprite(source.GetTextureFilePath(),
        { source.GetPosition().x + 16.0f, source.GetPosition().y + 16.0f }, source.GetSize());
    copy.SetRotation(source.GetRotation());
    copy.SetColor(source.GetColor());
    copy.SetAnchorPoint(source.GetAnchorPoint());
    copy.SetFlipX(source.GetFlipX());
    copy.SetFlipY(source.GetFlipY());
    copy.SetTextureLeftTop(source.GetTextureLeftTop());
    copy.SetTextureSize(source.GetTextureSize());
    copy.SetUVPosition(source.GetUVPosition());
    copy.SetUVScale(source.GetUVScale());
    copy.SetUVRotation(source.GetUVRotation());
    return copy;
}

void DemoScene::UpdateCameraInput(const Input& input, long mouseX, long mouseY, bool isLeftMouseDown)
{
    // デバッグカメラのON/OFF切り替え
    if (input.TriggerKey(DIK_F1))
    {
        isDebugCameraActive_ = !isDebugCameraActive_;
    }

    if (input.TriggerKey(DIK_F2))
    {
        cameras_.SetActiveCamera((cameras_.GetActiveIndex() + 1) % cameras_.GetCameraCount());
        isDebugCameraActive_ = false;
    }

    // デバッグカメラの更新処理
    if (isDebugCameraActive_)
    {
        debugCamera_.Update(&input, mouseX, mouseY, isLeftMouseDown);
    }
}

void DemoScene::Update()
{
    // オブジェクトとParticleの更新前に、選択中のカメラの行列を確定する。
    auto& activeCamera = cameras_.GetActiveCamera();
    activeCamera.Update();
    const ViewCamera& renderCamera = isDebugCameraActive_ ? debugCamera_.GetViewCamera() : activeCamera;
    UpdateParticles(renderCamera);

    auto* materialData = &sphereAppearance_.GetMaterial();
    const auto& directionalLightData = sphereObject_.GetDirectionalLight();
    const auto& pointLightData = sphereObject_.GetPointLight();
    const auto& spotLightData = sphereObject_.GetSpotLight();
    materialData->uvTransform = Math::MakeIdentity4x4();

    // ImGuiでの色変更
    materialData->color.x = colorArray_[0];
    materialData->color.y = colorArray_[1];
    materialData->color.z = colorArray_[2];
    materialData->color.w = colorArray_[3];

    planeObject_.GetTransform() = sphereObject_.GetTransform();
    planeModel_->SetTexture(useMonsterBall_ ? planeModel_->GetModelData().material.textureFilePath : "Resources/uvChecker.png");
    for (Object3DBase* object : { &sphereObject_, &planeObject_, &terrainObject_, &secondSphere_ }) {
        if (object != &sphereObject_) {
            // 既存のUI操作を全モデルへ反映。バッファ自体は個別に所有する。
            object->GetModel()->GetMaterial() = *materialData;
            object->GetDirectionalLight() = directionalLightData;
            object->GetPointLight() = pointLightData;
            object->GetSpotLight() = spotLightData;
        }
        object->SetCamera(&renderCamera);
        object->Update();
    }
}

void DemoScene::Draw()
{
    planeObject_.SetCommonRenderState(static_cast<BlendMode>(currentBlendMode_));
    terrainObject_.Draw();
    if (!useInstancing_) { planeObject_.Draw(); }
    if (showBall_) { sphereObject_.Draw(); secondSphere_.Draw(); }

    if (useInstancing_) { DrawParticles(); }

    // 3DとParticleの後に、深度を使わないSprite専用パイプラインで描画する。
    if (showSprites_) {
        // Sprite全体の描画ルールを1回設定し、その後に各Spriteを描画する。
        spriteBase_.SetCommonRenderState();
        for (const auto& sprite : sprites_) {
            sprite->Update();
            sprite->Draw();
        }
        if (deferredSmokeSprite_) {
            deferredSmokeSprite_->Update();
            deferredSmokeSprite_->Draw();
        }
    }
}

void DemoScene::Finalize()
{
    sprites_.clear();
    deferredSmokeSprite_.reset();
    secondSphere_.Finalize();
    sphereObject_.Finalize();
    planeObject_.Finalize();
    terrainObject_.Finalize();
    sphereAppearance_.Finalize();
    ModelManager::Finalize();
    // 残りのComPtrはDemoSceneの破棄時に解放される。
}
