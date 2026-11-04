#include "Model.h"
#include "ModelBase.h"
#include "DirectXBase.h"
#include "TextureManager.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
size_t ParseObjIndex(const std::string& value, size_t count)
{
    size_t used = 0;
    const int index = std::stoi(value, &used);
    if (used != value.size() || index <= 0 || static_cast<size_t>(index) > count) {
        throw std::runtime_error("OBJ index is outside the available vertex data.");
    }
    return static_cast<size_t>(index - 1);
}
}

DirectXBase* Model::GetDirectXBase() const { return modelBase_ ? modelBase_->GetDirectXBase() : nullptr; }

void Model::Initialize(ModelBase* modelBase, const std::string& directoryPath, const std::string& filename)
{
    Initialize(modelBase, LoadObjFile(directoryPath, filename));
}

void Model::Initialize(ModelBase* modelBase, ModelData modelData)
{
    if (modelBase_ || !modelBase || !modelBase->GetDirectXBase() || modelData.vertices.empty()) {
        throw std::invalid_argument("Model requires initialized ModelBase, vertices and one Initialize call.");
    }
    if (modelData.vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(VertexData)) {
        throw std::length_error("Model vertex buffer exceeds supported size.");
    }
    modelBase_ = modelBase;
    try {
        modelData_ = std::move(modelData);
        CreateVertexData();
        materialResource_ = GetDirectXBase()->CreateBufferResource(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        const D3D12_RANGE noRead{0, 0};
        if (!materialResource_ || FAILED(materialResource_->Map(0, &noRead, reinterpret_cast<void**>(&materialData_)))) {
            throw std::runtime_error("Failed to create Model material buffer.");
        }
        *materialData_ = material_;
        SetTexture(modelData_.material.textureFilePath.empty() ? "Resources/uvChecker.png" : modelData_.material.textureFilePath);
    } catch (...) { Finalize(); throw; }
}

void Model::Finalize()
{
    vertexResource_.Reset(); materialResource_.Reset(); materialData_ = nullptr;
    vertexBufferView_ = {}; textureIndex_ = UINT32_MAX; modelData_ = {}; modelBase_ = nullptr;
}

void Model::CreateVertexData()
{
    const size_t size = sizeof(VertexData) * modelData_.vertices.size();
    vertexResource_ = GetDirectXBase()->CreateBufferResource(size);
    VertexData* vertices = nullptr;
    const D3D12_RANGE noRead{ 0, 0 };
    if (!vertexResource_ || FAILED(vertexResource_->Map(0, &noRead, reinterpret_cast<void**>(&vertices)))) {
        throw std::runtime_error("Failed to create Object3D vertex buffer.");
    }
    std::memcpy(vertices, modelData_.vertices.data(), size);
    vertexResource_->Unmap(0, nullptr);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(size);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}


void Model::SetTexture(const std::string& filePath)
{
    if (!GetDirectXBase()) { throw std::logic_error("Initialize Model before setting texture."); }
    auto* textures = TextureManager::GetInstance();
    textures->LoadTexture(filePath);
    textureIndex_ = textures->GetTextureIndexByFilePath(filePath);
}

void Model::Draw() const
{
    if (!GetDirectXBase() || !vertexResource_ || !materialData_ || textureIndex_ == UINT32_MAX) {
        throw std::logic_error("Initialize Model before Draw.");
    }
    *materialData_ = material_;
    auto* commands = GetDirectXBase()->GetCommandList().Get();
    commands->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commands->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commands->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(textureIndex_));
    commands->DrawInstanced(static_cast<UINT>(modelData_.vertices.size()), 1, 0, 0);
}

Model::MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
    const auto path = std::filesystem::path(directoryPath) / filename;
    std::ifstream file(path);
    if (!file) { throw std::runtime_error("Could not open material: " + path.string()); }
    MaterialData material;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input(line);
        std::string identifier;
        input >> identifier;
        if (identifier == "map_Kd") {
            std::string texture;
            if (!(input >> texture)) { throw std::runtime_error("Missing map_Kd texture in " + path.string()); }
            material.textureFilePath = (path.parent_path() / texture).generic_string();
        }
    }
    return material;
}

Model::ModelData Model::LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
    const auto path = std::filesystem::path(directoryPath) / filename;
    std::ifstream file(path);
    if (!file) { throw std::runtime_error("Could not open OBJ: " + path.string()); }
    ModelData model;
    std::vector<Vector4> positions;
    std::vector<Vector2> texcoords;
    std::vector<Vector3> normals;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input(line);
        std::string identifier;
        input >> identifier;
        if (identifier == "v") {
            Vector4 position{};
            if (!(input >> position.x >> position.y >> position.z)) { throw std::runtime_error("Invalid OBJ position."); }
            position.w = 1.0f;
            positions.push_back(position);
        } else if (identifier == "vt") {
            Vector2 texcoord{};
            if (!(input >> texcoord.x >> texcoord.y)) { throw std::runtime_error("Invalid OBJ texcoord."); }
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifier == "vn") {
            Vector3 normal{};
            if (!(input >> normal.x >> normal.y >> normal.z)) { throw std::runtime_error("Invalid OBJ normal."); }
            normals.push_back(normal);
        } else if (identifier == "f") {
            std::array<VertexData, 3> triangle{};
            for (auto& vertex : triangle) {
                std::string definition;
                if (!(input >> definition)) { throw std::runtime_error("OBJ requires triangular v/vt/vn faces."); }
                std::istringstream indices(definition);
                std::string position, texcoord, normal;
                std::getline(indices, position, '/');
                std::getline(indices, texcoord, '/');
                std::getline(indices, normal, '/');
                vertex.position = positions.at(ParseObjIndex(position, positions.size()));
                vertex.texcoord = texcoords.at(ParseObjIndex(texcoord, texcoords.size()));
                vertex.normal = normals.at(ParseObjIndex(normal, normals.size()));
                vertex.position.z *= -1.0f;
                vertex.normal.z *= -1.0f;
            }
            std::string extra;
            if (input >> extra && !extra.starts_with('#')) { throw std::runtime_error("OBJ supports triangular faces only."); }
            // 従来のCG2描画と同じく両方向の頂点順を保持する。
            model.vertices.insert(model.vertices.end(), triangle.begin(), triangle.end());
            model.vertices.push_back(triangle[2]);
            model.vertices.push_back(triangle[1]);
            model.vertices.push_back(triangle[0]);
        } else if (identifier == "mtllib") {
            std::string materialFile;
            if (!(input >> materialFile)) { throw std::runtime_error("Missing OBJ material filename."); }
            model.material = LoadMaterialTemplateFile(path.parent_path().string(), materialFile);
        }
    }
    if (model.vertices.empty()) { throw std::runtime_error("OBJ contains no triangular faces: " + path.string()); }
    return model;
}

