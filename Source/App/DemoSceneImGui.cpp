#include "DemoScene.h"

#ifdef USE_IMGUI
#include "TextureManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace
{
const char* const kBlendModeNames[] = { "なし", "通常", "加算", "減算", "乗算", "スクリーン" };
}

void DemoScene::DrawSettings()
{
    ImGui::Begin("設定");
    ImGui::ColorEdit4("色", colorArray_);
    ImGui::Combo("オブジェクトの合成方法", &currentBlendMode_, kBlendModeNames, IM_ARRAYSIZE(kBlendModeNames));
    ImGui::Combo("パーティクルの合成方法", &currentParticleBlendMode_, kBlendModeNames, IM_ARRAYSIZE(kBlendModeNames));
    ImGui::Checkbox("モンスターボール用テクスチャ", &useMonsterBall_);
    DrawParticleSettings();
    DrawModelSettings();
    DrawLightingSettings();
    DrawSpriteSettings();
    auto& transform = sphereObject_.GetTransform();
    ImGui::Separator();
    ImGui::Text("モデルの変形");

    ImGui::SliderAngle("X軸回転", &transform.rotate.x);
    ImGui::SliderAngle("Y軸回転", &transform.rotate.y);
    ImGui::SliderAngle("Z軸回転", &transform.rotate.z);
    DrawCameraSettings();
    ImGui::End();
}

void DemoScene::DrawParticleSettings()
{
    ImGui::Checkbox("ビルボード表示", &useBillboard_);
    ImGui::Checkbox("更新する", &updateParticles_);
    // Emitterの設定
    ImGui::DragFloat3("発生位置", &emitter_.position.x, 0.05f, -100.0f, 100.0f);
    int emitCount = int(emitter_.count);
    if (ImGui::DragInt("発生数", &emitCount, 0.1f, 0, 20))
    {
        emitter_.count = uint32_t(emitCount);
    }
    ImGui::DragFloat("発生間隔", &emitter_.frequency, 0.01f, 0.05f, 5.0f);
    // ボタンを押したらEmitterの設定どおりにParticleが追加される
    if (ImGui::Button("パーティクルを追加"))
    {
        EmitParticles();
    }
    ImGui::Text("加速度フィールド");
    ImGui::Checkbox("加速度フィールドを使う", &useAccelerationField_);
    ImGui::DragFloat3("加速度", &accelerationField_.acceleration.x, 0.1f);
    ImGui::DragFloat3("影響範囲 最小", &accelerationField_.area.min.x, 0.05f);
    ImGui::DragFloat3("影響範囲 最大", &accelerationField_.area.max.x, 0.05f);
    ImGui::Checkbox("パーティクルのインスタンシング", &useInstancing_);
}

void DemoScene::DrawModelSettings()
{
    auto& transform = sphereObject_.GetTransform();
    ImGui::Separator();
    ImGui::Text("モンスターボール");
    ImGui::Checkbox("表示する", &showBall_);
    const char* modelChoices[] = { "球（共有モデル）", "plane.obj", "terrain.obj" };
    int selectedModel = secondSphere_.GetModel() == planeModel_ ? 1 : secondSphere_.GetModel() == terrainModel_ ? 2 : 0;
    if (ImGui::Combo("2体目のモデル", &selectedModel, modelChoices, IM_ARRAYSIZE(modelChoices))) {
        if (selectedModel == 0) { secondSphere_.SetModel(&sphereAppearance_); }
        else { secondSphere_.SetModel(selectedModel == 1 ? "plane.obj" : "terrain.obj"); }
    }
    ImGui::DragFloat3("位置", &transform.translate.x, 0.05f);
    // 全成分を同じ値でスケールする（X,Y,Zが同じ値になる）
    float uniformScale = transform.scale.x;
    if (ImGui::DragFloat("全体の大きさ", &uniformScale, 0.01f, 0.01f, 10.0f))
    {
        transform.scale = { uniformScale, uniformScale, uniformScale };
    }
    ImGui::DragFloat3("各軸の大きさ", &transform.scale.x, 0.01f, 0.01f, 10.0f);

}

void DemoScene::DrawLightingSettings()
{
    auto* materialData = &sphereAppearance_.GetMaterial();
    auto* directionalLightData = &sphereObject_.GetDirectionalLight();
    auto* pointLightData = &sphereObject_.GetPointLight();
    auto* spotLightData = &sphereObject_.GetSpotLight();
    ImGui::Separator();
    ImGui::Text("ライティング設定");

    bool enableLighting = (materialData->enableLighting != 0);
    if (ImGui::Checkbox("ライティングを有効にする", &enableLighting))
    {
        materialData->enableLighting = enableLighting ? 1 : 0;
    }

    // 光沢度（鏡面反射のハイライトの鋭さ）
    ImGui::SliderFloat("光沢度", &materialData->shininess, 1.0f, 200.0f);
    // 鏡面反射の計算方法の切り替え（ON:Blinn-Phong、OFF:Phong）
    bool useBlinnPhong = (materialData->useBlinnPhong != 0);
    if (ImGui::Checkbox("Blinn-Phong反射を使う", &useBlinnPhong))
    {
        materialData->useBlinnPhong = useBlinnPhong ? 1 : 0;
    }
    // 鏡面反射色（ハイライトの色）と、ライトの色
    ImGui::ColorEdit3("鏡面反射の色", &materialData->specularColor.x);
    ImGui::ColorEdit3("平行光の色", &directionalLightData->color.x);
    ImGui::Separator();
    ImGui::Text("ポイントライト");
    ImGui::ColorEdit3("ポイントライトの色", &pointLightData->color.x);
    ImGui::DragFloat3("ポイントライトの位置", &pointLightData->position.x, 0.05f);
    ImGui::SliderFloat("ポイントライトの明るさ", &pointLightData->intensity, 0.0f, 20.0f);
    ImGui::SliderFloat("影響範囲", &pointLightData->radius, 0.1f, 50.0f, "%.1f");
    ImGui::SliderFloat("ポイントライトの減衰率", &pointLightData->decay, 0.1f, 8.0f, "%.1f");
    ImGui::Separator();
    ImGui::Text("スポットライト");
    ImGui::ColorEdit3("スポットライトの色", &spotLightData->color.x);
    ImGui::DragFloat3("スポットライトの位置", &spotLightData->position.x, 0.05f);
    ImGui::SliderFloat("スポットライトの明るさ", &spotLightData->intensity, 0.0f, 20.0f);
    ImGui::SliderFloat("照射距離", &spotLightData->distance, 0.1f, 50.0f, "%.1f");
    ImGui::SliderFloat("スポットライトの減衰率", &spotLightData->decay, 0.1f, 8.0f, "%.1f");
    float spotDirection[3] = { spotLightData->direction.x, spotLightData->direction.y, spotLightData->direction.z };
    if (ImGui::SliderFloat3("照射方向", spotDirection, -1.0f, 1.0f))
    {
        Vector3 direction = { spotDirection[0], spotDirection[1], spotDirection[2] };
        if (direction.x != 0.0f || direction.y != 0.0f || direction.z != 0.0f)
        {
            spotLightData->direction = Vector3::Normalize(direction);
        }
    }
    float spotAngle = std::acos(std::clamp(spotLightData->cosAngle, -1.0f, 1.0f));
    if (ImGui::SliderAngle("コーンの角度", &spotAngle, 1.0f, 89.0f))
    {
        spotLightData->cosAngle = std::cos(spotAngle);
    }
    float spotConeAngleDegrees = spotAngle * (180.0f / std::numbers::pi_v<float>);
    float spotFalloffStartAngle = std::acos(std::clamp(spotLightData->cosFalloffStart, -1.0f, 1.0f));
    float maxFalloffStartAngle = spotConeAngleDegrees - 0.1f;
    spotFalloffStartAngle = std::clamp(spotFalloffStartAngle, 0.0f,
        maxFalloffStartAngle * std::numbers::pi_v<float> / 180.0f);
    ImGui::SliderAngle("減衰開始角度", &spotFalloffStartAngle, 0.0f, maxFalloffStartAngle);
    spotLightData->cosFalloffStart = std::cos(spotFalloffStartAngle);

    // 平行光源の向きの変更
    float lightDirection[3] = { directionalLightData->direction.x, directionalLightData->direction.y, directionalLightData->direction.z };
    if (ImGui::SliderFloat3("平行光の向き", lightDirection, -1.0f, 1.0f))
    {
        Vector3 dir = { lightDirection[0], lightDirection[1], lightDirection[2] };

        if (dir.x != 0.0f || dir.y != 0.0f || dir.z != 0.0f)
        {
            directionalLightData->direction = Vector3::Normalize(dir);
        }
        else
        {
            directionalLightData->direction = { 0.0f, -1.0f, 0.0f }; // オール0の時は下向きにする
        }
    }
    ImGui::SliderFloat("平行光の明るさ", &directionalLightData->intensity, 0.0f, 3.0f);
}

void DemoScene::DrawSpriteSettings()
{
    ImGui::Separator();
    ImGui::Text("スプライト");
    ImGui::Checkbox("スプライトを表示", &showSprites_);
    const char* spriteTextureNames[] = { "UVチェッカー", "モンスターボール画像", "円形画像", "草", "フェンス", "画像なし" };
    ImGui::Text("読み込み済み画像：%d（同じ画像は共有）", int(textureManager_->GetTextureCount()));
    ImGui::Text("生成数：%d / %d", int(sprites_.size()), int(kMaxSpriteCount));
    ImGui::Combo("追加する画像", &newSpriteTexture_, spriteTextureNames, IM_ARRAYSIZE(spriteTextureNames));
    ImGui::SliderInt("一度に生成する枚数", &spriteCreateCount_, 1, 20);
    ImGui::BeginDisabled(sprites_.size() >= kMaxSpriteCount);
    if (ImGui::Button("スプライトを追加")) {
        for (int index = 0; index < spriteCreateCount_ && sprites_.size() < kMaxSpriteCount; ++index) {
            const size_t number = sprites_.size();
            CreateSprite(newSpriteTexture_ == int(spriteTextures.size()) ? std::string{} : spriteTextures[newSpriteTexture_],
                { 16.0f + float(number % 6) * 144.0f, 16.0f + float((number / 6) % 4) * 144.0f },
                { 128.0f, 128.0f });
        }
        selectedSprite_ = int(sprites_.size()) - 1;
    }
    ImGui::EndDisabled();
    if (!sprites_.empty()) {
        selectedSprite_ = std::clamp(selectedSprite_, 0, int(sprites_.size()) - 1);
    } else {
        selectedSprite_ = -1;
    }
    std::string spritePreview = selectedSprite_ < 0 ? "スプライトなし" : std::format("スプライト {}", selectedSprite_ + 1);
    if (ImGui::BeginCombo("操作するスプライト", spritePreview.c_str())) {
        for (int index = 0; index < int(sprites_.size()); ++index) {
            const std::string name = std::format("スプライト {}", index + 1);
            if (ImGui::Selectable(name.c_str(), selectedSprite_ == index)) {
                selectedSprite_ = index;
            }
            if (selectedSprite_ == index) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(sprites_.empty() || sprites_.size() >= kMaxSpriteCount);
    if (ImGui::Button("選択したスプライトを複製")) {
        DuplicateSprite(size_t(selectedSprite_));
        selectedSprite_ = int(sprites_.size()) - 1;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(sprites_.empty());
    if (ImGui::Button("選択したスプライトを削除")) {
        // 前フレームのGPU処理はDirectXBase::PostDrawで完了待ち済み。
        sprites_.erase(sprites_.begin() + selectedSprite_);
        selectedSprite_ = sprites_.empty() ? -1 : std::clamp(selectedSprite_, 0, int(sprites_.size()) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("すべてのスプライトを削除")) {
        sprites_.clear();
        selectedSprite_ = -1;
    }
    ImGui::EndDisabled();
    if (selectedSprite_ >= 0) {
        Sprite& editedSprite = *sprites_[selectedSprite_];
        int currentTexture = editedSprite.HasTexture() ? 0 : int(spriteTextures.size());
        for (int index = 0; index < int(spriteTextures.size()); ++index) {
            if (editedSprite.GetTextureFilePath() == spriteTextures[index]) { currentTexture = index; break; }
        }
        if (ImGui::Combo("選択中の画像", &currentTexture, spriteTextureNames, IM_ARRAYSIZE(spriteTextureNames))) {
            if (currentTexture == int(spriteTextures.size())) { editedSprite.SetTextureIndex(UINT32_MAX); }
            else { editedSprite.SetTexture(spriteTextures[currentTexture]); }
        }
        if (editedSprite.HasTexture()) {
            const auto& imageMetadata = textureManager_->GetMetadata(editedSprite.GetTextureIndex());
            ImGui::Text("元画像：%d x %d", int(imageMetadata.width), int(imageMetadata.height));
            if (ImGui::Button("元画像サイズに合わせる")) {
                editedSprite.SetSize({ float(imageMetadata.width), float(imageMetadata.height) });
            }
        } else {
            ImGui::Text("画像未指定：上の欄で画像を設定すると描画されます。");
        }
        Vector2 spritePosition = editedSprite.GetPosition();
        if (ImGui::DragFloat2("画像の位置", &spritePosition.x, 1.0f)) {
            editedSprite.SetPosition(spritePosition);
        }
        Vector2 spriteSize = editedSprite.GetSize();
        if (ImGui::DragFloat2("画像のサイズ", &spriteSize.x, 1.0f, 1.0f, 2048.0f)) {
            editedSprite.SetSize(spriteSize);
        }
        float spriteRotation = editedSprite.GetRotation();
        if (ImGui::SliderAngle("画像の回転", &spriteRotation)) {
            editedSprite.SetRotation(spriteRotation);
        }
        auto spriteColor = editedSprite.GetColor();
        if (ImGui::ColorEdit4("画像の色・透明度", spriteColor.data())) {
            editedSprite.SetColor(spriteColor);
        }
        Vector2 spriteAnchor = editedSprite.GetAnchorPoint();
        if (ImGui::SliderFloat2("画像の基準点", &spriteAnchor.x, 0.0f, 1.0f)) {
            editedSprite.SetAnchorPoint(spriteAnchor);
        }
        bool flipX = editedSprite.GetFlipX();
        if (ImGui::Checkbox("左右反転", &flipX)) { editedSprite.SetFlipX(flipX); }
        ImGui::SameLine();
        bool flipY = editedSprite.GetFlipY();
        if (ImGui::Checkbox("上下反転", &flipY)) { editedSprite.SetFlipY(flipY); }
        if (ImGui::Button("左上を基準にする")) { editedSprite.SetAnchorPoint({ 0.0f, 0.0f }); }
        ImGui::SameLine();
        if (ImGui::Button("中央を基準にする")) { editedSprite.SetAnchorPoint({ 0.5f, 0.5f }); }
        ImGui::SameLine();
        if (ImGui::Button("右下を基準にする")) { editedSprite.SetAnchorPoint({ 1.0f, 1.0f }); }
        ImGui::Text("反転と回転は基準点を中心に行います。");
        ImGui::Separator();
        ImGui::Text("テクスチャの切り出し（元画像のピクセル単位）");
        ImGui::BeginDisabled(!editedSprite.HasTexture());
        Vector2 cropLeftTop = editedSprite.GetTextureLeftTop();
        if (ImGui::DragFloat2("切り出し左上(px)", &cropLeftTop.x, 1.0f)) {
            editedSprite.SetTextureLeftTop(cropLeftTop);
        }
        Vector2 cropSize = editedSprite.GetTextureSize();
        if (ImGui::DragFloat2("切り出しサイズ(px)", &cropSize.x, 1.0f)) {
            editedSprite.SetTextureSize(cropSize);
        }
        if (ImGui::Button("画像全体を使う")) { editedSprite.ResetTextureRegion(); }
        ImGui::SameLine();
        if (ImGui::Button("切り出しサイズで表示")) { editedSprite.SetSize(editedSprite.GetTextureSize()); }
        ImGui::EndDisabled();
        ImGui::Text("切り出し範囲は元画像の内側に制限されます。");
        ImGui::Separator();
        ImGui::Text("詳細UV変形");
        Vector2 uvPosition = editedSprite.GetUVPosition();
        if (ImGui::DragFloat2("UVオフセット", &uvPosition.x, 0.01f)) {
            editedSprite.SetUVPosition(uvPosition);
        }
        Vector2 uvScale = editedSprite.GetUVScale();
        if (ImGui::DragFloat2("UV倍率", &uvScale.x, 0.01f)) {
            editedSprite.SetUVScale(uvScale);
        }
        float uvRotation = editedSprite.GetUVRotation();
        if (ImGui::SliderAngle("UV回転", &uvRotation)) {
            editedSprite.SetUVRotation(uvRotation);
        }

    } else {
        ImGui::Text("上のボタンからスプライトを追加できます。");
    }
}

void DemoScene::DrawCameraSettings()
{
    ImGui::Separator();
    ImGui::Text("カメラ設定（F2で切り替え）");
    if (ImGui::BeginCombo("使用するカメラ", cameras_.GetName(cameras_.GetActiveIndex()).c_str())) {
        for (std::size_t index = 0; index < cameras_.GetCameraCount(); ++index) {
            if (ImGui::Selectable(cameras_.GetName(index).c_str(), index == cameras_.GetActiveIndex())) {
                cameras_.SetActiveCamera(index);
                isDebugCameraActive_ = false;
            }
        }
        ImGui::EndCombo();
    }
    auto& editableCamera = cameras_.GetActiveCamera();
    Vector3 position = editableCamera.GetTranslate();
    if (ImGui::DragFloat3("位置##ViewCamera", &position.x, 0.1f)) { editableCamera.SetTranslate(position); }
    Vector3 rotation = editableCamera.GetRotate();
    if (ImGui::DragFloat3("回転（ラジアン）", &rotation.x, 0.01f)) { editableCamera.SetRotate(rotation); }
    float fovY = editableCamera.GetFovY();
    if (ImGui::SliderAngle("垂直視野角", &fovY, 10.0f, 120.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp)) {
        editableCamera.SetFovY(fovY);
    }
    float aspect = editableCamera.GetAspectRatio();
    if (ImGui::DragFloat("アスペクト比", &aspect, 0.01f, 0.2f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        editableCamera.SetAspectRatio(aspect);
    }
    float nearClip = editableCamera.GetNearClip();
    float farClip = editableCamera.GetFarClip();
    if (ImGui::DragFloat("ニアクリップ", &nearClip, 0.01f, 0.01f, farClip - 0.01f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        editableCamera.SetNearClip(nearClip);
    }
    if (ImGui::DragFloat("ファークリップ", &farClip, 0.5f, nearClip + 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        editableCamera.SetFarClip(farClip);
    }

    ImGui::Text("デバッグカメラ");
    ImGui::Text(isDebugCameraActive_ ? "状態：有効（F1で無効化）" : "状態：無効（F1で有効化）");
    ImGui::Checkbox("デバッグカメラを有効にする", &isDebugCameraActive_);
    ImGui::Text("移動：W/A/S/D、上下：E/Q");
    ImGui::Text("視点操作：マウス");
    {
        Vector3 debugCamPos = debugCamera_.GetTranslation();
        if (ImGui::DragFloat3("カメラ位置", &debugCamPos.x, 0.1f))
        {
            debugCamera_.SetTranslation(debugCamPos);
        }
    }
}

#endif
