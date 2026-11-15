#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include "Matrix4x4.h"
#include "Vector4.h"

class ModelBase;
class DirectXBase;

// 見た目のデータ。複数のObject3DBaseから借りて使える。
class Model
{
public:
    struct VertexData
    {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };
    struct MaterialData { std::string textureFilePath; };
    struct ModelData
    {
        std::vector<VertexData> vertices;
        MaterialData material;
    };
    struct Material
    {
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32_t enableLighting = 1;
        float padding[3]{};
        Matrix4x4 uvTransform = Math::MakeIdentity4x4();
        float shininess = 40.0f;
        Vector3 specularColor{ 0.25f, 0.25f, 0.25f };
        int32_t useBlinnPhong = 1;
        float padding3[3]{};
    };

    static_assert(sizeof(VertexData) == 36);
    static_assert(sizeof(Material) == 128);
    Model() = default;
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    void Initialize(ModelBase* modelBase, const std::string& directoryPath = "Resources", const std::string& filename = "plane.obj");
    void Initialize(ModelBase* modelBase, ModelData modelData);
    void Finalize();
    // 配置・ライトのCBVはObject3DBaseが設定し、見た目の描画だけを担当する。
    void Draw() const;
    void SetTexture(const std::string& filePath);
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
    static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
    DirectXBase* GetDirectXBase() const;
    const ModelData& GetModelData() const { return modelData_; }
    const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView_; }
    Material& GetMaterial() { return material_; }
    const Material& GetMaterial() const { return material_; }
    uint32_t GetTextureIndex() const { return textureIndex_; }
private:
    void CreateVertexData();
    ModelBase* modelBase_ = nullptr; // 非所有。全ObjectとModelを解放するまで保持する。
    ModelData modelData_;
    Material material_;
    uint32_t textureIndex_ = UINT32_MAX;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
};
