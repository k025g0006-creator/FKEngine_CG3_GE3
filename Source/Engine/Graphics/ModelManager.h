#pragma once
#include <map>
#include <memory>
#include <string>
#include "Model.h"
#include "ModelBase.h"

class DirectXBase;

// ファイルパスに紐づけてModelを所有する。メインスレッドから利用する。
class ModelManager
{
public:
    static ModelManager* GetInstance();
    // GPU完了後、全Objectの解放後、TextureManager/DirectXBaseの終了前に呼ぶ。
    static void Finalize();
    void Initialize(DirectXBase* directXBase);
    // ファイル名だけならResources内、ディレクトリ付きなら指定パスを使う。
    void LoadModel(const std::string& filePath);
    Model* FindModel(const std::string& filePath) const;
    size_t GetModelCount() const { return models_.size(); }
    // 手続き生成モデルも同じ基盤を借りられる。Finalizeまで有効。
    ModelBase* GetModelBase() const { return modelBase_.get(); }
private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    static std::wstring MakePathKey(const std::string& filePath);
    static ModelManager* instance_;
    // 逆順で破棄されるため、Modelが先、基盤が後に解放される。
    std::unique_ptr<ModelBase> modelBase_;
    std::map<std::wstring, std::unique_ptr<Model>> models_;
};
