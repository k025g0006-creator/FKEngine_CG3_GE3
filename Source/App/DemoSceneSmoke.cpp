#include "DemoScene.h"
#include "Logger.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>

void DemoScene::CheckModels(std::ostream& logStream)
{
    auto requireObject = [](bool valid, const char* message) {
        if (!valid) { throw std::runtime_error(message); }
    };
    requireObject(planeModel_->GetModelData().vertices.size() == 12 &&
        terrainModel_->GetModelData().vertices.size() == 5292 &&
        sphereAppearance_.GetModelData().vertices.size() == 32 * 32 * 6, "3D model vertex counts changed.");
    requireObject(planeObject_.GetDirectXBase() == directXBase_ &&
        planeModel_->GetVertexBufferView().BufferLocation != terrainModel_->GetVertexBufferView().BufferLocation &&
        planeModel_->GetVertexBufferView().BufferLocation != sphereAppearance_.GetVertexBufferView().BufferLocation,
        "3D objects must use the same DirectX device and own distinct vertex buffers.");
    const auto originalTransform = planeObject_.GetTransform();
    const auto originalMaterial = planeModel_->GetMaterial();
    planeObject_.GetTransform() = { {2.0f, 3.0f, 4.0f}, {}, {5.0f, 6.0f, 7.0f} };
    planeModel_->GetMaterial().color.x = 0.25f;
    planeObject_.SetCamera(nullptr);
    planeObject_.Update();
    const auto& matrix = planeObject_.GetTransformationMatrix();
    requireObject(matrix.World.m[0][0] == 2.0f && matrix.World.m[1][1] == 3.0f &&
        matrix.World.m[2][2] == 4.0f && matrix.World.m[3][0] == 5.0f &&
        matrix.WVP.m[3][1] == 6.0f && matrix.WVP.m[3][2] == 7.0f &&
        std::fabs(matrix.WorldInverseTranspose.m[0][0] - 0.5f) < 0.0001f &&
        std::fabs(matrix.WorldInverseTranspose.m[1][1] - 1.0f / 3.0f) < 0.0001f &&
        std::fabs(matrix.WorldInverseTranspose.m[2][2] - 0.25f) < 0.0001f,
        "3D nonuniform scale, translation or normal transformation failed.");
    requireObject(terrainObject_.GetTransform().translate.y == 0.0f &&
        sphereObject_.GetTransform().translate.y == 1.0f &&
        terrainModel_->GetMaterial().color.x == 1.0f && sphereAppearance_.GetMaterial().color.x == 1.0f,
        "Editing one 3D object changed another object.");
    planeObject_.GetTransform() = originalTransform;
    planeModel_->GetMaterial() = originalMaterial;
    planeObject_.SetCamera(&cameras_.GetActiveCamera());
    planeObject_.Update();
    const auto defaultWvp = planeObject_.GetTransformationMatrix().WVP;
    cameras_.SetActiveCamera(1);
    planeObject_.SetCamera(&cameras_.GetActiveCamera());
    planeObject_.Update();
    requireObject(std::fabs(planeObject_.GetTransformationMatrix().WVP.m[0][0] - defaultWvp.m[0][0]) > 0.01f &&
        planeObject_.GetTransformationMatrix().World.m[3][1] == originalTransform.translate.y,
        "Switching cameras must change WVP while preserving object placement.");
    cameras_.SetActiveCamera(0);
    planeObject_.SetCamera(&cameras_.GetActiveCamera());
    requireObject(secondSphere_.GetModel() == sphereObject_.GetModel() && secondSphere_.GetTranslate().x == 3.0f &&
        sphereObject_.GetTranslate().x == 0.0f, "Shared Model did not preserve independent placements.");
    secondSphere_.SetScale({0.75f, 0.75f, 0.75f});
    secondSphere_.SetRotate({0.0f, 0.5f, 0.0f});
    requireObject(secondSphere_.GetScale().x == 0.75f && secondSphere_.GetRotate().y == 0.5f &&
        sphereObject_.GetScale().x == 1.0f, "Transform setters affected another object.");
    secondSphere_.SetModel(nullptr);
    secondSphere_.Update();
    secondSphere_.Draw(); // Model未指定時は描画コマンドを発行しない。
    secondSphere_.SetModel(&sphereAppearance_);
    Model retryModel;
    bool rejectedMissing = false;
    try { retryModel.Initialize(modelManager_->GetModelBase(), "Resources", "__missing_model__.obj"); }
    catch (const std::runtime_error&) { rejectedMissing = true; }
    requireObject(rejectedMissing && retryModel.GetDirectXBase() == nullptr, "Missing OBJ was not rejected cleanly.");
    auto brokenModel = planeModel_->GetModelData();
    brokenModel.material.textureFilePath = "Resources/__missing_texture__.png";
    bool rejectedTexture = false;
    try { retryModel.Initialize(modelManager_->GetModelBase(), std::move(brokenModel)); }
    catch (const std::runtime_error&) { rejectedTexture = true; }
    requireObject(rejectedTexture && retryModel.GetDirectXBase() == nullptr &&
        retryModel.GetVertexBufferView().BufferLocation == 0, "Model initialization did not roll back.");
    retryModel.Initialize(modelManager_->GetModelBase());
    retryModel.Finalize();
    const size_t cachedModels = modelManager_->GetModelCount();
    Model* cachedPlane = modelManager_->FindModel("plane.obj");
    modelManager_->LoadModel("./Resources/../Resources/plane.obj");
    modelManager_->LoadModel(std::filesystem::absolute("Resources/plane.obj").string());
    modelManager_->LoadModel("Resources/PLANE.OBJ");
    requireObject(modelManager_->GetModelCount() == cachedModels &&
        modelManager_->FindModel("Resources/PLANE.OBJ") == cachedPlane, "Model cache duplicated an equivalent path.");
    bool managerRejectedMissing = false;
    try { modelManager_->LoadModel("__missing_model__.obj"); }
    catch (const std::runtime_error&) { managerRejectedMissing = true; }
    requireObject(managerRejectedMissing && modelManager_->GetModelCount() == cachedModels &&
        modelManager_->FindModel("__missing_model__.obj") == nullptr, "Failed model load changed the cache.");
    secondSphere_.SetModel("plane.obj");
    requireObject(secondSphere_.GetModel() == cachedPlane && sphereObject_.GetModel() == &sphereAppearance_,
        "Path model switching changed another placement.");
    secondSphere_.SetModel("terrain.obj");
    requireObject(secondSphere_.GetModel() == terrainModel_, "Second model selection failed.");
    secondSphere_.SetModel("__missing_model__.obj");
    secondSphere_.Draw();
    requireObject(secondSphere_.GetModel() == nullptr, "Missing model must clear the assignment.");
    secondSphere_.SetModel(&sphereAppearance_);
    Logger::Log(logStream, "ModelManager smoke: path cache, failed-load rollback, model switching and missing lookup passed.");
    Logger::Log(logStream, "Model smoke: shared appearance, independent placements, null assignment and recovery passed.");
    Logger::Log(logStream, "Object3D smoke: independent models/materials, transforms/normals, camera and failed-init recovery passed.");
}

void DemoScene::CheckTextures(std::ostream& logStream)
{
    const size_t count = textureManager_->GetTextureCount();
    const auto originalHandle = textureManager_->GetSrvHandleGPU("Resources/uvChecker.png");
    textureManager_->LoadTexture("./Resources/../Resources/uvChecker.png");
    textureManager_->LoadTexture("resources\\uvChecker.png");
    textureManager_->LoadTexture(std::filesystem::absolute("Resources/uvChecker.png").string());
    bool rejectedMissingTexture = false;
    try { textureManager_->LoadTexture("Resources/__missing_texture__.png"); }
    catch (const std::runtime_error&) { rejectedMissingTexture = true; }
    if (textureManager_->GetTextureCount() != count || !rejectedMissingTexture ||
        textureManager_->GetSrvHandleGPU("Resources/uvChecker.png").ptr != originalHandle.ptr) {
        Logger::Log(logStream, "Texture cache or failed-load rollback check failed.");
        std::terminate();
    }
    Logger::Log(logStream, "Texture smoke: duplicate paths shared; missing input rejected.");
    bool rejectedInvalidIndex = false;
    try { (void)textureManager_->GetMetadata(static_cast<uint32_t>(count)); }
    catch (const std::out_of_range&) { rejectedInvalidIndex = true; }
    if (!rejectedInvalidIndex) {
        Logger::Log(logStream, "Out-of-range texture metadata index was accepted.");
        std::terminate();
    }
}

void DemoScene::CheckSprites(std::ostream& logStream)
{
    const uint32_t index = textureManager_->GetTextureIndexByFilePath(spriteTextures[0]);
    Sprite indexedProbe;
    indexedProbe.Initialize(&spriteBase_, index);
    const auto desc = textureManager_->GetTextureBuffer(index)->GetDesc();
    if (indexedProbe.GetSize().x != float(desc.Width) || indexedProbe.GetSize().y != float(desc.Height)) {
        Logger::Log(logStream, "Texture-index initialization did not apply the GPU image size.");
        std::terminate();
    }
    // 静止フレーム・同値setter・変更時の再更新を確認する。
    bool optimizationOK = !indexedProbe.IsDirty();
    indexedProbe.Update();
    indexedProbe.SetPosition(indexedProbe.GetPosition());
    indexedProbe.SetSize(indexedProbe.GetSize());
    indexedProbe.SetRotation(indexedProbe.GetRotation());
    indexedProbe.SetColor(indexedProbe.GetColor());
    indexedProbe.SetAnchorPoint(indexedProbe.GetAnchorPoint());
    indexedProbe.SetFlipX(indexedProbe.GetFlipX());
    indexedProbe.SetFlipY(indexedProbe.GetFlipY());
    indexedProbe.SetUVPosition(indexedProbe.GetUVPosition());
    indexedProbe.SetUVScale(indexedProbe.GetUVScale());
    indexedProbe.SetUVRotation(indexedProbe.GetUVRotation());
    indexedProbe.SetTextureLeftTop(indexedProbe.GetTextureLeftTop());
    indexedProbe.SetTextureSize(indexedProbe.GetTextureSize());
    indexedProbe.SetTextureIndex(index);
    optimizationOK = optimizationOK && !indexedProbe.IsDirty();
    indexedProbe.SetPosition({ 12.0f, 34.0f });
    optimizationOK = optimizationOK && indexedProbe.IsDirty();
    indexedProbe.Update();
    optimizationOK = optimizationOK && !indexedProbe.IsDirty();
    indexedProbe.SetColor({ 0.2f, 0.3f, 0.4f, 0.5f });
    optimizationOK = optimizationOK && indexedProbe.IsDirty();
    indexedProbe.Update();
    optimizationOK = optimizationOK && !indexedProbe.IsDirty();

    indexedProbe.SetTextureLeftTop({ float(desc.Width) * 0.125f, float(desc.Height) * 0.25f });
    indexedProbe.SetTextureSize({ float(desc.Width) * 0.25f, float(desc.Height) * 0.5f });
    indexedProbe.SetUVScale({ 0.8f, -1.2f });
    indexedProbe.SetUVRotation(0.3f);
    indexedProbe.SetUVPosition({ 0.05f, 0.1f });
    indexedProbe.Update();
    const std::array<Vector2, 5> uvSamples{
        Vector2{ 0.0f, 0.0f }, Vector2{ 1.0f, 0.0f }, Vector2{ 0.0f, 1.0f },
        Vector2{ 1.0f, 1.0f }, Vector2{ 0.5f, 0.5f }
    };
    for (const Vector2& sample : uvSamples) {
        // 従来方式：頂点で切り出し、その後シェーダーで倍率・回転・移動。
        const float u = (0.125f + sample.x * 0.25f) * 0.8f;
        const float v = (0.25f + sample.y * 0.5f) * -1.2f;
        const float expectedU = u * std::cos(0.3f) - v * std::sin(0.3f) + 0.05f;
        const float expectedV = u * std::sin(0.3f) + v * std::cos(0.3f) + 0.1f;
        const Vector3 actual = Math::Transform({ sample.x, sample.y, 0.0f }, indexedProbe.GetUVTransform());
        optimizationOK = optimizationOK && std::abs(actual.x - expectedU) < 0.00001f &&
            std::abs(actual.y - expectedV) < 0.00001f;
    }
    // 共通射影の変更は、Sprite自身に変更がなくてもWVPを再計算する。
    spriteBase_.SetScreenSize(float(WinApp::kClientWidth) * 2.0f, float(WinApp::kClientHeight));
    optimizationOK = optimizationOK && indexedProbe.IsDirty();
    indexedProbe.Update();
    const uint64_t revision = spriteBase_.GetProjectionRevision();
    spriteBase_.SetScreenSize(float(WinApp::kClientWidth) * 2.0f, float(WinApp::kClientHeight));
    optimizationOK = optimizationOK && !indexedProbe.IsDirty() && spriteBase_.GetProjectionRevision() == revision;
    spriteBase_.SetScreenSize(float(WinApp::kClientWidth), float(WinApp::kClientHeight));
    indexedProbe.Update();
    if (!optimizationOK) {
        Logger::Log(logStream, "Sprite dirty flags, UV equivalence or shared projection regression failed.");
        std::terminate();
    }
    Logger::Log(logStream, "Sprite optimization: clean/same-value updates, UV equivalence and projection invalidation passed.");
    deferredSmokeSprite_ = std::make_unique<Sprite>();
    deferredSmokeSprite_->Initialize(&spriteBase_);
    if (deferredSmokeSprite_->HasTexture()) { std::terminate(); }
    Logger::Log(logStream, "Sprite smoke: index initialization matches GetDesc; omitted index accepted.");
}

void DemoScene::PrepareSmokeFrame(uint32_t frame, std::ostream& logStream)
{
    // plane単体とParticleの両描画経路、およびデバッグカメラ経路を実行する。
    // 同じ配置で、別モデルへの差し替えもフレームをまたいで描画する。
    if (frame == 0) { secondSphere_.SetModel("plane.obj"); }
    else if (frame == 1) { secondSphere_.SetModel("terrain.obj"); }
    else { secondSphere_.SetModel(&sphereAppearance_); }
    useInstancing_ = frame != 0;
    isDebugCameraActive_ = frame == 1;
    if (frame == 2) { cameras_.SetActiveCamera(1); }
    if (frame == 1) { EmitParticles(); }
    if (frame == 0) {
        Sprite& boundsCheck = *sprites_.front();
        const auto& imageBounds = textureManager_->GetMetadata(boundsCheck.GetTextureIndex());
        const Vector2 displaySize = boundsCheck.GetSize();
        boundsCheck.SetTextureLeftTop({ -20.0f, -30.0f });
        boundsCheck.SetTextureSize({ 100000.0f, 100000.0f });
        const bool fullBoundsOK = boundsCheck.GetTextureLeftTop().x == 0.0f &&
            boundsCheck.GetTextureLeftTop().y == 0.0f &&
            boundsCheck.GetTextureSize().x == float(imageBounds.width) &&
            boundsCheck.GetTextureSize().y == float(imageBounds.height);
        boundsCheck.SetTextureLeftTop({ float(imageBounds.width) + 20.0f, float(imageBounds.height) + 30.0f });
        boundsCheck.SetTextureSize({ 0.0f, -20.0f });
        const bool edgeBoundsOK = boundsCheck.GetTextureSize().x == 1.0f &&
            boundsCheck.GetTextureSize().y == 1.0f &&
            boundsCheck.GetTextureLeftTop().x == float(imageBounds.width) - 1.0f &&
            boundsCheck.GetTextureLeftTop().y == float(imageBounds.height) - 1.0f;
        boundsCheck.ResetTextureRegion();
        if (!fullBoundsOK || !edgeBoundsOK || boundsCheck.GetSize().x != displaySize.x ||
            boundsCheck.GetSize().y != displaySize.y) {
            Logger::Log(logStream, "Sprite crop bounds or independent display size check failed.");
            std::terminate();
        }
        Logger::Log(logStream, "Sprite smoke: crop bounds clamped; display size preserved.");
        for (int index = 0; index < 10; ++index) {
            Sprite& added = CreateSprite(spriteTextures[size_t(index) % spriteTextures.size()],
                { 16.0f + float(index) * 72.0f, 220.0f }, { 64.0f, 48.0f });
            added.SetRotation(0.1f * float(index));
            added.SetColor({ 0.7f, 0.9f, 1.0f, 0.5f });
            added.SetAnchorPoint({ float(index % 3) * 0.5f, float((index + 1) % 3) * 0.5f });
            added.SetFlipX((index & 1) != 0);
            added.SetFlipY((index & 2) != 0);
            const auto& image = textureManager_->GetMetadata(added.GetTextureIndex());
            added.SetTextureLeftTop({ float(image.width) * 0.25f, float(image.height) * 0.25f });
            added.SetTextureSize({ float(image.width) * 0.5f, float(image.height) * 0.5f });
        }
        Sprite& cloned = DuplicateSprite(sprites_.size() - 1);
        const Sprite& original = *sprites_[sprites_.size() - 2];
        if (cloned.GetFlipX() != original.GetFlipX() || cloned.GetFlipY() != original.GetFlipY() ||
            cloned.GetAnchorPoint().x != original.GetAnchorPoint().x ||
            cloned.GetTextureLeftTop().x != original.GetTextureLeftTop().x ||
            cloned.GetTextureLeftTop().y != original.GetTextureLeftTop().y ||
            cloned.GetTextureSize().x != original.GetTextureSize().x ||
            cloned.GetTextureSize().y != original.GetTextureSize().y) {
            Logger::Log(logStream, "Sprite anchor/flip/crop clone test failed.");
            std::terminate();
        }
        Logger::Log(logStream, "Sprite smoke: anchor, both flips, crop and clone settings verified.");
        Logger::Log(logStream, "Sprite smoke: created and duplicated, count=13.");
    } else if (frame == 1) {
        deferredSmokeSprite_->SetTextureIndex(textureManager_->GetTextureIndexByFilePath(spriteTextures[0]));
        deferredSmokeSprite_->SetPosition({ 16.0f, 400.0f });
        deferredSmokeSprite_->SetSize({ 64.0f, 64.0f });
        const uint32_t attachedIndex = deferredSmokeSprite_->GetTextureIndex();
        bool rejectedInvalidIndex = false;
        try { deferredSmokeSprite_->SetTextureIndex(UINT32_MAX - 1); }
        catch (const std::out_of_range&) { rejectedInvalidIndex = true; }
        if (!rejectedInvalidIndex || deferredSmokeSprite_->GetTextureIndex() != attachedIndex) {
            Logger::Log(logStream, "Invalid texture index did not preserve the original image.");
            std::terminate();
        }
        Logger::Log(logStream, "Sprite smoke: deferred image attached; invalid index rejected.");
        sprites_.erase(sprites_.begin());
        sprites_.clear();
        Logger::Log(logStream, "Sprite smoke: removed one and cleared all, count=0.");
    } else if (frame == 2) {
        CreateSprite(spriteTextures[0], { 16.0f, 16.0f }, { 160.0f, 160.0f });
        deferredSmokeSprite_->SetTextureIndex(UINT32_MAX);
        Logger::Log(logStream, "Sprite smoke: deferred image detached safely.");
        Logger::Log(logStream, "Sprite smoke: recreated after empty, count=1.");
    }
}
