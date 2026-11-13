#include "Object3DBase.h"
#include "DirectXBase.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

std::weak_ptr<Object3DBase::RenderState> Object3DBase::sharedRenderState_;

namespace
{
void CheckObject3DResult(HRESULT result)
{
    if (FAILED(result)) {
        throw std::runtime_error("Failed to create Object3DBase rendering resources.");
    }
}

template<class T>
void CreateConstantBuffer(DirectXBase* dx, Microsoft::WRL::ComPtr<ID3D12Resource>& resource, T*& data)
{
    constexpr size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    constexpr size_t size = (sizeof(T) + alignment - 1) & ~(alignment - 1);
    resource = dx->CreateBufferResource(size);
    const D3D12_RANGE noRead{ 0, 0 };
    if (!resource || FAILED(resource->Map(0, &noRead, reinterpret_cast<void**>(&data)))) {
        throw std::runtime_error("Failed to create Object3D constant buffer.");
    }
}

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

void Object3DBase::Initialize(DirectXBase* directXBase, const std::string& directoryPath, const std::string& filename)
{
    Initialize(directXBase, LoadObjFile(directoryPath, filename));
}

void Object3DBase::Initialize(DirectXBase* directXBase, ModelData modelData)
{
    if (directXBase_ || !directXBase || !directXBase->GetDevice() ||
        !directXBase->GetCommandList() || !directXBase->GetSRVDescriptorHeap() || modelData.vertices.empty()) {
        throw std::invalid_argument("Object3DBase requires initialized DirectXBase, vertices and one Initialize call.");
    }
    if (modelData.vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(VertexData)) {
        throw std::length_error("Object3D vertex buffer exceeds the supported size.");
    }
    directXBase_ = directXBase;
    try {
        renderState_ = sharedRenderState_.lock();
        if (!renderState_ || renderState_->device != directXBase_->GetDevice().Get()) {
            renderState_ = std::make_shared<RenderState>();
            renderState_->device = directXBase_->GetDevice().Get();
            CreateGraphicsPipelineState();
        }
        modelData_ = std::move(modelData);
        CreateVertexData();
        CreateConstantBuffers();
        SetTexture(modelData_.material.textureFilePath.empty() ? "Resources/uvChecker.png" : modelData_.material.textureFilePath);
        Update();
        // 完全な描画状態だけを公開する。最後のインスタンスの終了で自動解放される。
        sharedRenderState_ = renderState_;
    } catch (...) {
        Finalize();
        throw;
    }
}

void Object3DBase::Finalize()
{
    vertexResource_.Reset();
    materialResource_.Reset();
    transformationResource_.Reset();
    directionalLightResource_.Reset();
    cameraResource_.Reset();
    pointLightResource_.Reset();
    spotLightResource_.Reset();
    materialData_ = nullptr;
    transformationData_ = nullptr;
    directionalLightData_ = nullptr;
    cameraData_ = nullptr;
    pointLightData_ = nullptr;
    spotLightData_ = nullptr;
    vertexBufferView_ = {};
    textureIndex_ = UINT32_MAX;
    modelData_ = {};
    renderState_.reset();
    directXBase_ = nullptr;
}

void Object3DBase::CreateVertexData()
{
    const size_t size = sizeof(VertexData) * modelData_.vertices.size();
    vertexResource_ = directXBase_->CreateBufferResource(size);
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

void Object3DBase::CreateConstantBuffers()
{
    auto* dx = directXBase_;
    CreateConstantBuffer(dx, materialResource_, materialData_);
    CreateConstantBuffer(dx, transformationResource_, transformationData_);
    CreateConstantBuffer(dx, directionalLightResource_, directionalLightData_);
    CreateConstantBuffer(dx, cameraResource_, cameraData_);
    CreateConstantBuffer(dx, pointLightResource_, pointLightData_);
    CreateConstantBuffer(dx, spotLightResource_, spotLightData_);
}

void Object3DBase::SetTexture(const std::string& filePath)
{
    if (!directXBase_) { throw std::logic_error("Initialize Object3DBase before setting a texture."); }
    auto* textures = TextureManager::GetInstance();
    textures->LoadTexture(filePath);
    textureIndex_ = textures->GetTextureIndexByFilePath(filePath);
}

void Object3DBase::Update()
{
    const Matrix4x4 cameraMatrix = Math::MakeAffineMatrix(cameraTransform_.scale, cameraTransform_.rotate, cameraTransform_.translate);
    const Matrix4x4 viewMatrix = Math::Inverse(cameraMatrix);
    const Matrix4x4 projectionMatrix = Math::MakePerspectiveFovMatrix(0.45f,
        static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight), 0.1f, 100.0f);
    Update(viewMatrix, projectionMatrix);
}

void Object3DBase::Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
    if (!directXBase_ || !transformationData_) { throw std::logic_error("Initialize Object3DBase before Update."); }
    const Matrix4x4 worldMatrix = Math::MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
    transformationMatrix_.World = worldMatrix;
    transformationMatrix_.WVP = Math::Multiply(worldMatrix, Math::Multiply(viewMatrix, projectionMatrix));
    transformationMatrix_.WorldInverseTranspose = Math::Transpose(Math::Inverse(worldMatrix));
    const Matrix4x4 cameraWorld = Math::Inverse(viewMatrix);
    camera_.worldPosition = { cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2] };
    *transformationData_ = transformationMatrix_;
    *materialData_ = material_;
    *directionalLightData_ = directionalLight_;
    *cameraData_ = camera_;
    *pointLightData_ = pointLight_;
    *spotLightData_ = spotLight_;
}

void Object3DBase::Draw() const
{
    if (!directXBase_ || !vertexResource_ || textureIndex_ == UINT32_MAX) {
        throw std::logic_error("Initialize Object3DBase before Draw.");
    }
    auto* commands = directXBase_->GetCommandList().Get();
    commands->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commands->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commands->SetGraphicsRootConstantBufferView(1, transformationResource_->GetGPUVirtualAddress());
    commands->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(textureIndex_));
    commands->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());
    commands->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
    commands->SetGraphicsRootConstantBufferView(5, pointLightResource_->GetGPUVirtualAddress());
    commands->SetGraphicsRootConstantBufferView(6, spotLightResource_->GetGPUVirtualAddress());
    commands->DrawInstanced(static_cast<UINT>(modelData_.vertices.size()), 1, 0, 0);
}

Object3DBase::MaterialData Object3DBase::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
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

Object3DBase::ModelData Object3DBase::LoadObjFile(const std::string& directoryPath, const std::string& filename)
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

void Object3DBase::CreateRootSignature()
{
    auto& device = directXBase_->GetDevice();
    HRESULT hr = S_OK;
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	// RootParameterの設定
	D3D12_ROOT_PARAMETER rootParameters[7] = {};

	// [0] CBV  PixelShaderで利用
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	// [1] CBV  VertexShaderで利用
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	// [2] DescriptorTable  PixelShaderで利用
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1;                    // レジスタ番号1を使う

	// カメラの位置（b2）。PixelShaderで使う
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[4].Descriptor.ShaderRegister = 2;

	// [5] PointLight CBV (b3)
	rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[5].Descriptor.ShaderRegister = 3;

	// [6] SpotLight CBV (b4)
	rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[6].Descriptor.ShaderRegister = 4;

	// StaticSamplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;        // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;      // リピート設定
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                      // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;                              // レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 4. RootSignatureDesc全体の結合
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);


	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	CheckObject3DResult(hr);
	// バイナリを元に生成
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&renderState_->rootSignature));
	CheckObject3DResult(hr);

}

void Object3DBase::CreateGraphicsPipelineState()
{
    CreateRootSignature();
    auto& device = directXBase_->GetDevice();
    HRESULT hr = S_OK;
	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	// 裏面（時計回り）を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = directXBase_->CompileShader(L"Resources/Shaders/Object3d.VS.hlsl", L"vs_6_0");
	if (!vertexShaderBlob) { throw std::runtime_error("Failed to compile Object3D vertex shader."); }

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = directXBase_->CompileShader(L"Resources/Shaders/Object3d.PS.hlsl", L"ps_6_0");
	if (!pixelShaderBlob) { throw std::runtime_error("Failed to compile Object3D pixel shader."); }

	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	// Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;

	// 書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = renderState_->rootSignature.Get(); // RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;  // InputLayout
	graphicsPipelineStateDesc.VS =
	{ vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; // VertexShader
	graphicsPipelineStateDesc.PS =
	{ pixelShaderBlob->GetBufferPointer(),pixelShaderBlob->GetBufferSize() }; // PixelShader
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;  // RasterizerState

	// DepthStencilStateの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジ（形状）のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むかの設定（気にしなくていい）
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成
	// BlendModeごとにPSOを生成する（BlendModeを変える = PSOを変える）
	for (int i = 0; i < kCountOfBlendMode; ++i)
	{
		graphicsPipelineStateDesc.BlendState = CreateBlendDesc(static_cast<BlendMode>(i));
		hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
			IID_PPV_ARGS(&renderState_->pipelineStates[i]));
		CheckObject3DResult(hr);
	}

}

void Object3DBase::SetCommonRenderState(BlendMode blendMode) const
{
    if (!directXBase_ || !renderState_) { throw std::logic_error("Initialize Object3DBase before drawing."); }
    if (blendMode < kBlendModeNone || blendMode >= kCountOfBlendMode)
	{
        throw std::out_of_range("Invalid Object3D blend mode.");
    }
    auto* commandList = directXBase_->GetCommandList().Get();
    ID3D12DescriptorHeap* heaps[] = { directXBase_->GetSRVDescriptorHeap().Get() };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    commandList->SetGraphicsRootSignature(renderState_->rootSignature.Get());
    commandList->SetPipelineState(renderState_->pipelineStates[blendMode].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
