#include "ModelManager.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <stdexcept>

ModelManager* ModelManager::instance_ = nullptr;

ModelManager* ModelManager::GetInstance()
{
    if (!instance_) { instance_ = new ModelManager(); }
    return instance_;
}

void ModelManager::Finalize()
{
    delete instance_;
    instance_ = nullptr;
}

void ModelManager::Initialize(DirectXBase* directXBase)
{
    if (modelBase_) { throw std::logic_error("ModelManager is already initialized."); }
    auto base = std::make_unique<ModelBase>();
    base->Initialize(directXBase);
    modelBase_ = std::move(base);
}

std::wstring ModelManager::MakePathKey(const std::string& filePath)
{
    if (filePath.empty()) { throw std::invalid_argument("Model path is empty."); }
    std::filesystem::path path(filePath);
    if (!path.is_absolute() && !path.has_parent_path()) { path = std::filesystem::path("Resources") / path; }
    path = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    auto key = path.native();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return key;
}

void ModelManager::LoadModel(const std::string& filePath)
{
    if (!modelBase_) { throw std::logic_error("Initialize ModelManager before loading models."); }
    const auto key = MakePathKey(filePath);
    if (models_.contains(key)) { return; }
    const std::filesystem::path path(key);
    auto model = std::make_unique<Model>();
    model->Initialize(modelBase_.get(), path.parent_path().string(), path.filename().string());
    models_.emplace(key, std::move(model));
}

Model* ModelManager::FindModel(const std::string& filePath) const
{
    if (!modelBase_) { throw std::logic_error("Initialize ModelManager before finding models."); }
    const auto found = models_.find(MakePathKey(filePath));
    return found != models_.end() ? found->second.get() : nullptr;
}
