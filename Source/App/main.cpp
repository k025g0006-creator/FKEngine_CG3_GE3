#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <format>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <d3d12.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <exception>
#include <cmath>
#include <array>
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#include <cassert>
#include <algorithm>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#include <strsafe.h>
#pragma comment(lib,"dxguid.lib")
#include <dxcapi.h>

#include <xaudio2.h>
#pragma comment(lib,"xaudio2.lib")

#ifdef USE_IMGUI
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

#include <DirectXTex.h>

#include "Matrix4x4.h"
#include "Vector4.h"
#include "Transform.h"
#include "ResourceObject.h"
#include "Sound.h"
#include "DebugCamera.h"
#include "Input.h"
#include "WinApp.h"
#include "DirectXBase.h"
#include "Object3DBase.h"
#include "Model.h"
#include "ModelBase.h"
#include "ModelManager.h"
#include "SpriteBase.h"
#include "Sprite.h"
#include "TextureManager.h"
#include "D3DResourceLeakChecker.h"
#include "Logger.h"

#include <sstream>

#include <random>
#include <numbers>
#include <list>

using Microsoft::WRL::ComPtr;




// Particleも3Dオブジェクトと同じ頂点・マテリアルの配置を利用する。
using VertexData = Model::VertexData;
using Material = Model::Material;

// Instancing用（StructuredBufferに入れる）。HLSL側のParticleForGPUと並びを合わせる
struct ParticleForGPU
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

struct Particle
{
	FKEngine::Transform transform;
	Vector3 velocity;
	Vector4 color;
	float lifeTime;
	float currentTime;
};

// Particleを発生させる装置
struct Emitter
{
	FKEngine::Transform transform; // エミッタのTransform（translateが発生位置）
	uint32_t count;               // 発生数
	float frequency;              // 発生頻度（秒）
	float frequencyTime;          // 頻度用時刻
};

// 加速度フィールド（範囲内のParticleに一定の加速度を与える）
struct AccelerationField
{
	Vector3 acceleration; // 加速度
	AABB area;            // 範囲
};

struct ParticleGPU
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

static std::filesystem::path GetExecutableDirectory()
{
	wchar_t executablePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
	return std::filesystem::path(executablePath).parent_path();
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	// 時刻を取得して、時刻を名前に入れたファイルを作成、Dumpsディレクタ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	const std::filesystem::path dumpDirectory = GetExecutableDirectory() / "Dumps";
	std::filesystem::create_directories(dumpDirectory);
	StringCchPrintfW(filePath, MAX_PATH, L"%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	const std::filesystem::path dumpFilePath = dumpDirectory / filePath;
	HANDLE dumpFileHandle = CreateFileW(dumpFilePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	// processId(このexeのId)とクラッシュ(例外)の発生したthreadIdを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();

	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

	// 他に関連づけられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

std::random_device seedGenerator;
std::mt19937 randomEngine(seedGenerator());


Particle MakeNewParticle(std::mt19937& randomEngine, const Vector3& translate)
{
	std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
	std::uniform_real_distribution<float> distTime(1.0f, 3.0f);

	Particle particle;
	particle.transform.scale = { 1.0f,1.0f,1.0f };
	particle.transform.rotate = { 0.0f,0.0f,0.0f };
	// 発生位置(エミッタの位置)からランダムに少しずらす
	Vector3 randomTranslate{ distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };
	particle.transform.translate = Vector3::Add(translate, randomTranslate);
	particle.velocity = { distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };
	particle.color = { distColor(randomEngine),distColor(randomEngine),distColor(randomEngine),1.0f };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTime = 0.0f;
	return particle;
}

// Emitterの設定どおりにParticleを発生させる
std::list<Particle> Emit(const Emitter& emitter, std::mt19937& randomEngine)
{
	std::list<Particle> particles;
	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeNewParticle(randomEngine, emitter.transform.translate));
	}
	return particles;
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	// 統合確認用に3フレーム描画したら、通常の終了処理で終了する。
	const bool smokeTest = std::string(lpCmdLine).find("--smoke-test") != std::string::npos;
	uint32_t smokeTestFrames = 0;
	D3DResourceLeakChecker leakCheck;

	ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice;

	Sound sound1;


	CoInitializeEx(0, COINIT_MULTITHREADED);
	// 誰も補足しなかった場合に(Unhandled), 補足する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	// ログのディレクトリを用意
	const std::filesystem::path logDirectory = GetExecutableDirectory() / "Logs";
	std::filesystem::create_directories(logDirectory);

	// 現在時刻を取得
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	// ログファイルの名前にコンマ何秒は要らないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	// 日本時間（PCの設定時間）に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(),nowSeconds };

	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	// 時刻を使ってファイル名を決定
	std::filesystem::path logFilePath = logDirectory / (dateString + ".log");

	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

	// ウィンドウの生成
	WinApp* winApp = new WinApp();
	winApp->Initialize();
	HWND hwnd = winApp->GetHwnd();

	const int32_t kClientWidth = WinApp::kClientWidth;
	const int32_t kClientHeight = WinApp::kClientHeight;

	Logger::Log(logStream, std::format("Application Started. Window Size: {}x{}", kClientWidth, kClientHeight));

	// DirectXの基盤を初期化
	DirectXBase* directXBase = new DirectXBase();
	bool directXInitialized = directXBase->Initialize(hwnd, kClientWidth, kClientHeight);
	if (!directXInitialized)
	{
		assert(false && "DirectXBase initialization failed");
		delete directXBase;
		winApp->Finalize();
		delete winApp;
		CoUninitialize();
		return -1;
	}
	Logger::Log(logStream, "DirectXBase initialized.");
	HRESULT hr = S_OK;

	auto& device = directXBase->GetDevice();
	auto& commandList = directXBase->GetCommandList();

	auto& srvDescriptorHeap = directXBase->GetSRVDescriptorHeap();
	const uint32_t descriptorSizeSRV = directXBase->GetSRVDescriptorSize();
	const DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

#ifdef USE_IMGUI
	// ImGuiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device.Get(),
		DirectXBase::kBackBufferCount,
		rtvFormat,
		srvDescriptorHeap.Get(),
		GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, DirectXBase::kImGuiSRVIndex),
		GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, DirectXBase::kImGuiSRVIndex));
	const std::u8string imguiIniPathUtf8 = (GetExecutableDirectory() / "Config/imgui.ini").u8string();
	const std::string imguiIniPath(imguiIniPathUtf8.begin(), imguiIniPathUtf8.end());
	io.IniFilename = imguiIniPath.c_str();
	io.Fonts->Build();
#endif
	// 個別オブジェクトの初期化でもTextureManagerを利用する。
	TextureManager* textureManager = TextureManager::GetInstance();
	textureManager->Initialize(directXBase);

	// モデル類は通常の不透明描画、Particleは加算ブレンドで描画する
	int currentBlendMode = kBlendModeNone;
	int currentParticleBlendMode = kBlendModeAdd;

	////////////////////////////// Particle用 ////////////////////////////
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


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

	// Instancing用のStructuredBuffer(SRV)をVertexShaderのt0で使うためのDescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;   // 0から始まる
	descriptorRangeForInstancing[0].NumDescriptors = 1;       // 数は1つ
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// Particle用のRootParameter（Object3d用とは別に作って管理する）
	D3D12_ROOT_PARAMETER particleRootParameters[3] = {};

	// [0] Material CBV  PixelShaderで利用
	particleRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	particleRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	particleRootParameters[0].Descriptor.ShaderRegister = 0;

	// [1] Instancing用StructuredBuffer  DescriptorTable  VertexShaderで利用
	particleRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	particleRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	particleRootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
	particleRootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);

	// [2] Texture  DescriptorTable  PixelShaderで利用
	particleRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	particleRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	particleRootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	particleRootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	D3D12_ROOT_SIGNATURE_DESC particleRootSignatureDesc{};
	particleRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	particleRootSignatureDesc.pParameters = particleRootParameters;
	particleRootSignatureDesc.NumParameters = _countof(particleRootParameters);
	particleRootSignatureDesc.pStaticSamplers = staticSamplers;
	particleRootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

	Microsoft::WRL::ComPtr<ID3DBlob> particleSignatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> particleErrorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&particleRootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1, &particleSignatureBlob, &particleErrorBlob);
	if (FAILED(hr))
	{
		Logger::Log(logStream, reinterpret_cast<char*>(particleErrorBlob->GetBufferPointer()));
		assert(false);
	}
	ComPtr<ID3D12RootSignature> particleRootSignature = nullptr;
	hr = device->CreateRootSignature(0,
		particleSignatureBlob->GetBufferPointer(), particleSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&particleRootSignature));
	assert(SUCCEEDED(hr));

	// Particle用のShaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> particleVertexShaderBlob = directXBase->CompileShader(L"Resources/Shaders/Particle.VS.hlsl", L"vs_6_0");
	assert(particleVertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> particlePixelShaderBlob = directXBase->CompileShader(L"Resources/Shaders/Particle.PS.hlsl", L"ps_6_0");
	assert(particlePixelShaderBlob != nullptr);

	// Particle専用のPSOを生成する。
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

	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	// Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;

	// 書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePipelineStateDesc{};
	particlePipelineStateDesc.pRootSignature = particleRootSignature.Get(); // RootSignature
	particlePipelineStateDesc.InputLayout = inputLayoutDesc;  // InputLayout
	particlePipelineStateDesc.VS =
	{ particleVertexShaderBlob->GetBufferPointer(), particleVertexShaderBlob->GetBufferSize() }; // VertexShader
	particlePipelineStateDesc.PS =
	{ particlePixelShaderBlob->GetBufferPointer(),particlePixelShaderBlob->GetBufferSize() }; // PixelShader
	particlePipelineStateDesc.RasterizerState = rasterizerDesc;  // RasterizerState

	// DepthStencilStateの設定
	particlePipelineStateDesc.DepthStencilState = depthStencilDesc;
	particlePipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 書き込むRTVの情報
	particlePipelineStateDesc.NumRenderTargets = 1;
	particlePipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジ（形状）のタイプ。三角形
	particlePipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むかの設定（気にしなくていい）
	particlePipelineStateDesc.SampleDesc.Count = 1;
	particlePipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	particlePipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	ComPtr<ID3D12PipelineState> particlePipelineStates[kCountOfBlendMode];
	for (int i = 0; i < kCountOfBlendMode; ++i)
	{
		particlePipelineStateDesc.BlendState = CreateBlendDesc(static_cast<BlendMode>(i));
		hr = device->CreateGraphicsPipelineState(&particlePipelineStateDesc,
			IID_PPV_ARGS(&particlePipelineStates[i]));
		assert(SUCCEEDED(hr));
	}

	// 同じ共通設定から、独立したモデル・頂点・定数バッファを持つ3Dオブジェクトを生成する。
	ModelManager* modelManager = ModelManager::GetInstance();
	modelManager->Initialize(directXBase);
	modelManager->LoadModel("plane.obj");
	modelManager->LoadModel("terrain.obj");
	ModelBase& modelBase = *modelManager->GetModelBase();
	Model& planeModel = *modelManager->FindModel("plane.obj");
	Model& terrainModel = *modelManager->FindModel("terrain.obj");
	Object3DBase planeObject;
	planeObject.Initialize(directXBase);
	planeObject.SetModel("plane.obj");
	Object3DBase terrainObject;
	terrainObject.Initialize(directXBase);
	terrainObject.SetModel("terrain.obj");
	terrainObject.GetTransform() = {}; // 地形は原点・等倍のまま描く。
	// Particleはplaneの頂点を借り、専用のインスタンシング描画を行う。
	const auto& modelData = planeModel.GetModelData();
	const auto& vertexBufferViewModel = planeModel.GetVertexBufferView();

	const uint32_t kSubdivision = 32;
	const uint32_t kVertexCount = kSubdivision * kSubdivision * 6;

	// 手続き生成した球も、モデルデータを渡して同じ個別クラスで扱う。
	Model::ModelData sphereModel;
	sphereModel.material.textureFilePath = "Resources/monsterBall.png";
	sphereModel.vertices.resize(kVertexCount);
	VertexData* vertexData = sphereModel.vertices.data();

	const float pi = 3.14159265f;
	float latitudeStep = pi / kSubdivision;
	float longitudeStep = 2.0f * pi / kSubdivision;

	for (uint32_t latIndex = 0;latIndex < kSubdivision;++latIndex)
	{
		float theta = -pi / 2.0f + static_cast<float>(latIndex) * latitudeStep;

		for (uint32_t lonIndex = 0;lonIndex < kSubdivision;++lonIndex)
		{
			float phi = static_cast<float>(lonIndex) * longitudeStep + pi;

			uint32_t startIndex = (latIndex * kSubdivision + lonIndex) * 6;

			Vector4 a =
			{
				cosf(theta) * cosf(phi), sinf(theta), cosf(theta) * sinf(phi), 1.0f
			};

			Vector4 b =
			{
				cosf(theta + latitudeStep) * cosf(phi), sinf(theta + latitudeStep), cosf(theta + latitudeStep) * sinf(phi), 1.0f
			};

			Vector4 c =
			{
				cosf(theta) * cosf(phi + longitudeStep), sinf(theta), cosf(theta) * sinf(phi + longitudeStep), 1.0f
			};

			Vector4 d =
			{
				cosf(theta + latitudeStep) * cosf(phi + longitudeStep), sinf(theta + latitudeStep), cosf(theta + latitudeStep) * sinf(phi + longitudeStep), 1.0f
			};

			float fSubdivision = static_cast<float>(kSubdivision);
			float u1 = static_cast<float>(lonIndex) / fSubdivision;
			float u2 = static_cast<float>(lonIndex + 1) / fSubdivision;
			float v1 = 1.0f - static_cast<float>(latIndex) / fSubdivision;
			float v2 = 1.0f - static_cast<float>(latIndex + 1) / fSubdivision;

			// 三角形1
			vertexData[startIndex + 0].position = a;
			vertexData[startIndex + 0].texcoord = { u1, v1 };

			// 法線情報
			vertexData[startIndex + 0].normal.x = vertexData[startIndex + 0].position.x;
			vertexData[startIndex + 0].normal.y = vertexData[startIndex + 0].position.y;
			vertexData[startIndex + 0].normal.z = vertexData[startIndex + 0].position.z;

			vertexData[startIndex + 1].position = b;
			vertexData[startIndex + 1].texcoord = { u1, v2 };
			vertexData[startIndex + 1].normal.x = vertexData[startIndex + 1].position.x;
			vertexData[startIndex + 1].normal.y = vertexData[startIndex + 1].position.y;
			vertexData[startIndex + 1].normal.z = vertexData[startIndex + 1].position.z;

			vertexData[startIndex + 2].position = d;
			vertexData[startIndex + 2].texcoord = { u2, v2 };
			vertexData[startIndex + 2].normal.x = vertexData[startIndex + 2].position.x;
			vertexData[startIndex + 2].normal.y = vertexData[startIndex + 2].position.y;
			vertexData[startIndex + 2].normal.z = vertexData[startIndex + 2].position.z;

			// 三角形2
			vertexData[startIndex + 3].position = a;
			vertexData[startIndex + 3].texcoord = { u1, v1 };
			vertexData[startIndex + 3].normal.x = vertexData[startIndex + 3].position.x;
			vertexData[startIndex + 3].normal.y = vertexData[startIndex + 3].position.y;
			vertexData[startIndex + 3].normal.z = vertexData[startIndex + 3].position.z;

			vertexData[startIndex + 4].position = d;
			vertexData[startIndex + 4].texcoord = { u2, v2 };
			vertexData[startIndex + 4].normal.x = vertexData[startIndex + 4].position.x;
			vertexData[startIndex + 4].normal.y = vertexData[startIndex + 4].position.y;
			vertexData[startIndex + 4].normal.z = vertexData[startIndex + 4].position.z;

			vertexData[startIndex + 5].position = c;
			vertexData[startIndex + 5].texcoord = { u2, v1 };
			vertexData[startIndex + 5].normal.x = vertexData[startIndex + 5].position.x;
			vertexData[startIndex + 5].normal.y = vertexData[startIndex + 5].position.y;
			vertexData[startIndex + 5].normal.z = vertexData[startIndex + 5].position.z;
		}
	}

	Model sphereAppearance;
	sphereAppearance.Initialize(&modelBase, std::move(sphereModel));
	Object3DBase sphereObject;
	sphereObject.Initialize(directXBase);
	sphereObject.SetModel(&sphereAppearance);
	// 同じModelをもう1つの配置で使う。頂点・材質・テクスチャを再作成しない。
	Object3DBase secondSphere;
	secondSphere.Initialize(directXBase);
	secondSphere.SetModel(&sphereAppearance);
	secondSphere.SetTranslate({3.0f, 1.0f, 0.0f});
	// ImGuiはCPU側の設定を編集し、Updateで各オブジェクトのGPUバッファへ反映する。
	auto& transform = sphereObject.GetTransform();
	auto& cameraTransform = sphereObject.GetCameraTransform();
	auto* materialData = &sphereAppearance.GetMaterial();
	auto* directionalLightData = &sphereObject.GetDirectionalLight();
	auto* pointLightData = &sphereObject.GetPointLight();
	auto* spotLightData = &sphereObject.GetSpotLight();
	float colorArray[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	// Particleのマテリアルは専用バッファ。3Dオブジェクトの定数バッファとは別に保持する。
	ComPtr<ID3D12Resource> materialResource = directXBase->CreateBufferResource(256);
	Material* particleMaterialData = nullptr;
	hr = materialResource->Map(0, nullptr, reinterpret_cast<void**>(&particleMaterialData));
	if (FAILED(hr)) {
		Logger::Log(logStream, "Failed to map particle material.");
		std::terminate();
	}

	if (smokeTest) {
		auto requireObject = [](bool valid, const char* message) {
			if (!valid) { throw std::runtime_error(message); }
		};
		requireObject(planeModel.GetModelData().vertices.size() == 12 &&
			terrainModel.GetModelData().vertices.size() == 5292 &&
			sphereAppearance.GetModelData().vertices.size() == kVertexCount, "3D model vertex counts changed.");
		requireObject(planeObject.GetDirectXBase() == directXBase &&
			planeModel.GetVertexBufferView().BufferLocation != terrainModel.GetVertexBufferView().BufferLocation &&
			planeModel.GetVertexBufferView().BufferLocation != sphereAppearance.GetVertexBufferView().BufferLocation,
			"3D objects must use the same DirectX device and own distinct vertex buffers.");
		const auto originalTransform = planeObject.GetTransform();
		const auto originalMaterial = planeModel.GetMaterial();
		planeObject.GetTransform() = { {2.0f, 3.0f, 4.0f}, {}, {5.0f, 6.0f, 7.0f} };
		planeModel.GetMaterial().color.x = 0.25f;
		planeObject.Update(Math::MakeIdentity4x4(), Math::MakeIdentity4x4());
		const auto& matrix = planeObject.GetTransformationMatrix();
		requireObject(matrix.World.m[0][0] == 2.0f && matrix.World.m[1][1] == 3.0f &&
			matrix.World.m[2][2] == 4.0f && matrix.World.m[3][0] == 5.0f &&
			matrix.WVP.m[3][1] == 6.0f && matrix.WVP.m[3][2] == 7.0f &&
			std::fabs(matrix.WorldInverseTranspose.m[0][0] - 0.5f) < 0.0001f &&
			std::fabs(matrix.WorldInverseTranspose.m[1][1] - 1.0f / 3.0f) < 0.0001f &&
			std::fabs(matrix.WorldInverseTranspose.m[2][2] - 0.25f) < 0.0001f,
			"3D nonuniform scale, translation or normal transformation failed.");
		requireObject(terrainObject.GetTransform().translate.y == 0.0f &&
			sphereObject.GetTransform().translate.y == 1.0f &&
			terrainModel.GetMaterial().color.x == 1.0f && sphereAppearance.GetMaterial().color.x == 1.0f,
			"Editing one 3D object changed another object.");
		planeObject.GetTransform() = originalTransform;
		planeModel.GetMaterial() = originalMaterial;
		planeObject.Update();
		const Matrix4x4 defaultWvp = planeObject.GetTransformationMatrix().WVP;
		const auto& camera = planeObject.GetCameraTransform();
		planeObject.Update(Math::Inverse(Math::MakeAffineMatrix(camera.scale, camera.rotate, camera.translate)),
			Math::MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f));
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				requireObject(std::fabs(defaultWvp.m[row][column] - planeObject.GetTransformationMatrix().WVP.m[row][column]) < 0.0001f,
					"3D internal and supplied camera matrices disagree.");
			}
		}
		requireObject(secondSphere.GetModel() == sphereObject.GetModel() && secondSphere.GetTranslate().x == 3.0f &&
			sphereObject.GetTranslate().x == 0.0f, "Shared Model did not preserve independent placements.");
		secondSphere.SetScale({0.75f, 0.75f, 0.75f});
		secondSphere.SetRotate({0.0f, 0.5f, 0.0f});
		requireObject(secondSphere.GetScale().x == 0.75f && secondSphere.GetRotate().y == 0.5f &&
			sphereObject.GetScale().x == 1.0f, "Transform setters affected another object.");
		secondSphere.SetModel(nullptr);
		secondSphere.Update();
		secondSphere.Draw(); // Model未指定時は描画コマンドを発行しない。
		secondSphere.SetModel(&sphereAppearance);
		Model retryModel;
		bool rejectedMissing = false;
		try { retryModel.Initialize(&modelBase, "Resources", "__missing_model__.obj"); }
		catch (const std::runtime_error&) { rejectedMissing = true; }
		requireObject(rejectedMissing && retryModel.GetDirectXBase() == nullptr, "Missing OBJ was not rejected cleanly.");
		auto brokenModel = planeModel.GetModelData();
		brokenModel.material.textureFilePath = "Resources/__missing_texture__.png";
		bool rejectedTexture = false;
		try { retryModel.Initialize(&modelBase, std::move(brokenModel)); }
		catch (const std::runtime_error&) { rejectedTexture = true; }
		requireObject(rejectedTexture && retryModel.GetDirectXBase() == nullptr &&
			retryModel.GetVertexBufferView().BufferLocation == 0, "Model initialization did not roll back.");
		retryModel.Initialize(&modelBase);
		retryModel.Finalize();
		const size_t cachedModels = modelManager->GetModelCount();
		Model* cachedPlane = modelManager->FindModel("plane.obj");
		modelManager->LoadModel("./Resources/../Resources/plane.obj");
		modelManager->LoadModel(std::filesystem::absolute("Resources/plane.obj").string());
		modelManager->LoadModel("Resources/PLANE.OBJ");
		requireObject(modelManager->GetModelCount() == cachedModels &&
			modelManager->FindModel("Resources/PLANE.OBJ") == cachedPlane, "Model cache duplicated an equivalent path.");
		bool managerRejectedMissing = false;
		try { modelManager->LoadModel("__missing_model__.obj"); }
		catch (const std::runtime_error&) { managerRejectedMissing = true; }
		requireObject(managerRejectedMissing && modelManager->GetModelCount() == cachedModels &&
			modelManager->FindModel("__missing_model__.obj") == nullptr, "Failed model load changed the cache.");
		secondSphere.SetModel("plane.obj");
		requireObject(secondSphere.GetModel() == cachedPlane && sphereObject.GetModel() == &sphereAppearance,
			"Path model switching changed another placement.");
		secondSphere.SetModel("terrain.obj");
		requireObject(secondSphere.GetModel() == &terrainModel, "Second model selection failed.");
		secondSphere.SetModel("__missing_model__.obj");
		secondSphere.Draw();
		requireObject(secondSphere.GetModel() == nullptr, "Missing model must clear the assignment.");
		secondSphere.SetModel(&sphereAppearance);
		Logger::Log(logStream, "ModelManager smoke: path cache, failed-load rollback, model switching and missing lookup passed.");
		Logger::Log(logStream, "Model smoke: shared appearance, independent placements, null assignment and recovery passed.");
		Logger::Log(logStream, "Object3D smoke: independent models/materials, transforms/normals, camera and failed-init recovery passed.");
	}

	// デバッグカメラ
	DebugCamera debugCamera;
	debugCamera.Initialize(kClientWidth, kClientHeight);
	debugCamera.SetTranslation(cameraTransform.translate); // 通常カメラと同じ位置から開始

	// デバッグカメラが有効かどうか
	bool isDebugCameraActive = false;


	// パスで共有するテクスチャ管理。Spriteと3Dモデルから同じ画像を参照できる。
	textureManager->LoadTexture("Resources/uvChecker.png");
	textureManager->LoadTexture("Resources/circle.png");
	textureManager->LoadTexture("Resources/monsterBall.png");

	if (smokeTest) {
		const size_t count = textureManager->GetTextureCount();
		const auto originalHandle = textureManager->GetSrvHandleGPU("Resources/uvChecker.png");
		textureManager->LoadTexture("./Resources/../Resources/uvChecker.png");
		textureManager->LoadTexture("resources\\uvChecker.png");
		textureManager->LoadTexture(std::filesystem::absolute("Resources/uvChecker.png").string());
		bool rejectedMissingTexture = false;
		try { textureManager->LoadTexture("Resources/__missing_texture__.png"); }
		catch (const std::runtime_error&) { rejectedMissingTexture = true; }
		if (textureManager->GetTextureCount() != count || !rejectedMissingTexture ||
			textureManager->GetSrvHandleGPU("Resources/uvChecker.png").ptr != originalHandle.ptr) {
			Logger::Log(logStream, "Texture cache or failed-load rollback check failed.");
			std::terminate();
		}
		Logger::Log(logStream, "Texture smoke: duplicate paths shared; missing input rejected.");
		bool rejectedInvalidIndex = false;
		try { (void)textureManager->GetMetadata(static_cast<uint32_t>(count)); }
		catch (const std::out_of_range&) { rejectedInvalidIndex = true; }
		if (!rejectedInvalidIndex) {
			Logger::Log(logStream, "Out-of-range texture metadata index was accepted.");
			std::terminate();
		}
	}
	// DirectInputの初期化
	IDirectInput8* directInput = nullptr;
	hr = DirectInput8Create(winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, nullptr);
	assert(SUCCEEDED(hr));

	Input* input = new Input();
	bool inputInitialized = input->Initialize(directInput, hwnd);
	assert(inputInitialized);

	// マウスデバイスの生成
	IDirectInputDevice8* mouse = nullptr;
	hr = directInput->CreateDevice(GUID_SysMouse, &mouse, NULL);
	assert(SUCCEEDED(hr));

	// 入力データ形式のセット（マウスの標準形式）
	hr = mouse->SetDataFormat(&c_dfDIMouse);
	assert(SUCCEEDED(hr));

	// 排他制御レベルのセット
	hr = mouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));


	// SRVの割り当て・生成はTextureManagerに集約する。
	const auto textureSrvHandleGPU3 = textureManager->GetSrvHandleGPU("Resources/circle.png");
	// 共通設定は1つ、スプライト1枚ごとのデータは別インスタンスにする。
	SpriteBase spriteBase;
	spriteBase.Initialize(directXBase, float(kClientWidth), float(kClientHeight));
	// unique_ptrで所有するので、削除時・終了時に各Spriteのバッファも自動解放される。
	std::vector<std::unique_ptr<Sprite>> sprites;
	constexpr size_t kMaxSpriteCount = 256;
	const std::array<std::string, 5> spriteTextures{
		"Resources/uvChecker.png", "Resources/monsterBall.png", "Resources/circle.png",
		"Resources/grass.png", "Resources/fence.png"
	};
	auto createSprite = [&](const std::string& texture, const Vector2& position, const Vector2& size) -> Sprite& {
		auto sprite = std::make_unique<Sprite>();
		if (texture.empty()) { sprite->Initialize(&spriteBase); }
		else { sprite->Initialize(&spriteBase, texture); }
		sprite->SetPosition(position);
		sprite->SetSize(size);
		Sprite& result = *sprite;
		sprites.push_back(std::move(sprite));
		return result;
	};
	auto duplicateSprite = [&](size_t index) -> Sprite& {
		const Sprite& source = *sprites.at(index);
		Sprite& copy = createSprite(source.GetTextureFilePath(),
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
	};
	createSprite(spriteTextures[0], { 16.0f, 16.0f }, { 160.0f, 160.0f });
	createSprite(spriteTextures[1], { 192.0f, 16.0f }, { 96.0f, 96.0f });
	std::unique_ptr<Sprite> deferredSmokeSprite;
	if (smokeTest) {
		const uint32_t index = textureManager->GetTextureIndexByFilePath(spriteTextures[0]);
		Sprite indexedProbe;
		indexedProbe.Initialize(&spriteBase, index);
		const auto desc = textureManager->GetTextureBuffer(index)->GetDesc();
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
		spriteBase.SetScreenSize(float(kClientWidth) * 2.0f, float(kClientHeight));
		optimizationOK = optimizationOK && indexedProbe.IsDirty();
		indexedProbe.Update();
		const uint64_t revision = spriteBase.GetProjectionRevision();
		spriteBase.SetScreenSize(float(kClientWidth) * 2.0f, float(kClientHeight));
		optimizationOK = optimizationOK && !indexedProbe.IsDirty() && spriteBase.GetProjectionRevision() == revision;
		spriteBase.SetScreenSize(float(kClientWidth), float(kClientHeight));
		indexedProbe.Update();
		if (!optimizationOK) {
			Logger::Log(logStream, "Sprite dirty flags, UV equivalence or shared projection regression failed.");
			std::terminate();
		}
		Logger::Log(logStream, "Sprite optimization: clean/same-value updates, UV equivalence and projection invalidation passed.");
		deferredSmokeSprite = std::make_unique<Sprite>();
		deferredSmokeSprite->Initialize(&spriteBase);
		if (deferredSmokeSprite->HasTexture()) { std::terminate(); }
		Logger::Log(logStream, "Sprite smoke: index initialization matches GetDesc; omitted index accepted.");
	}
	bool showSprites = true;
#ifdef USE_IMGUI
	int selectedSprite = 0;
	int newSpriteTexture = 0;
	int spriteCreateCount = 1;
#endif
	////////////////////////////// Instancing ////////////////////////////
	const uint32_t kNumMaxInstance = 100; // 最大インスタンス数

	// Instancing用のTransformationMatrixリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
		directXBase->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);

	// 書き込むためのアドレスを取得
	ParticleForGPU* instancingData = nullptr;
	instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

	// 単位行列を書き込んでおく
	for (uint32_t index = 0; index < kNumMaxInstance; ++index)
	{
		instancingData[index].WVP = Math::MakeIdentity4x4();
		instancingData[index].World = Math::MakeIdentity4x4();
		instancingData[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	// StructuredBuffer用のSRVを作る
	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumMaxInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

	// 予約したInstancing用スロットへSRVを作成する（0:ImGui、1:Instancing、2以降:TextureManager）。
	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, DirectXBase::kInstancingSRVIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, DirectXBase::kInstancingSRVIndex);
	device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

	// CPU側のParticleはlistで管理する（最大数を予め決めなくてよい）
	std::list<Particle> particles;

	// Particleを発生させるEmitter（どこから・何個・どのくらいの頻度で発生するか）
	Emitter emitter{};
	emitter.transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	emitter.count = 3;          // 3個ずつ発生
	emitter.frequency = 0.5f;   // 0.5秒ごとに発生
	emitter.frequencyTime = 0.0f; // 頻度用時刻。0で初期化

	// Δtを定義。とりあえず60fps固定
	const float kDeltaTime = 1.0f / 60.0f;

	// 風を表現するField。+x方向に15m/sの加速度、範囲は原点中心で-1〜1
	AccelerationField accelerationField;
	accelerationField.acceleration = { 15.0f, 0.0f, 0.0f };
	accelerationField.area.min = { -1.0f, -1.0f, -1.0f };
	accelerationField.area.max = { 1.0f, 1.0f, 1.0f };

	// trueでFieldの加速度を適用する（ImGuiでON/OFF切り替え）
	bool useAccelerationField = true;

	// trueでParticle用のPipeline(Instancing描画)、falseで従来のObject3d用Pipelineで描画する
	bool useInstancing = true;

	bool useMonsterBall = false; // falseでuvChecker.pngを貼る

	// trueでParticleを常にカメラの方へ向ける(Billboard)
	bool useBillboard = true;

	// trueでモンスターボールを1個だけ表示する（ライティング付きのObject3d用Pipelineで描画）
	bool showBall = true;

	// falseにするとParticleの移動と経過時間が止まる（確認用）
	bool update = true;

	// xAudioエンジンのインスタンスを生成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);

	// マスターボイスを生成
	hr = xAudio2->CreateMasteringVoice(&masterVoice);

	// 音声読み込み
	sound1.Load("Resources/Alarm01.wav");

	// 音声再生
	if (!smokeTest)
	{
		sound1.Play(xAudio2.Get());
	}

	while (true)
	{
		// ウィンドウのメッセージ処理。終了メッセージが来たらループを抜ける
		if (winApp->ProcessMessage())
		{
			break;
		}
		else
		{
			// 検証モードでは、描画フレームをまたいだ生成・複製・削除・再生成を確認する。
			if (smokeTest) {
				// plane単体とParticleの両描画経路、およびデバッグカメラ経路を実行する。
				// 同じ配置で、別モデルへの差し替えもフレームをまたいで描画する。
				if (smokeTestFrames == 0) { secondSphere.SetModel("plane.obj"); }
				else if (smokeTestFrames == 1) { secondSphere.SetModel("terrain.obj"); }
				else { secondSphere.SetModel(&sphereAppearance); }
				useInstancing = smokeTestFrames != 0;
				isDebugCameraActive = smokeTestFrames == 1;
				if (smokeTestFrames == 0) {
					Sprite& boundsCheck = *sprites.front();
					const auto& imageBounds = textureManager->GetMetadata(boundsCheck.GetTextureIndex());
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
						Sprite& added = createSprite(spriteTextures[size_t(index) % spriteTextures.size()],
							{ 16.0f + float(index) * 72.0f, 220.0f }, { 64.0f, 48.0f });
						added.SetRotation(0.1f * float(index));
						added.SetColor({ 0.7f, 0.9f, 1.0f, 0.5f });
						added.SetAnchorPoint({ float(index % 3) * 0.5f, float((index + 1) % 3) * 0.5f });
						added.SetFlipX((index & 1) != 0);
						added.SetFlipY((index & 2) != 0);
						const auto& image = textureManager->GetMetadata(added.GetTextureIndex());
						added.SetTextureLeftTop({ float(image.width) * 0.25f, float(image.height) * 0.25f });
						added.SetTextureSize({ float(image.width) * 0.5f, float(image.height) * 0.5f });
					}
					Sprite& cloned = duplicateSprite(sprites.size() - 1);
					const Sprite& original = *sprites[sprites.size() - 2];
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
				} else if (smokeTestFrames == 1) {
					deferredSmokeSprite->SetTextureIndex(textureManager->GetTextureIndexByFilePath(spriteTextures[0]));
					deferredSmokeSprite->SetPosition({ 16.0f, 400.0f });
					deferredSmokeSprite->SetSize({ 64.0f, 64.0f });
					const uint32_t attachedIndex = deferredSmokeSprite->GetTextureIndex();
					bool rejectedInvalidIndex = false;
					try { deferredSmokeSprite->SetTextureIndex(UINT32_MAX - 1); }
					catch (const std::out_of_range&) { rejectedInvalidIndex = true; }
					if (!rejectedInvalidIndex || deferredSmokeSprite->GetTextureIndex() != attachedIndex) {
						Logger::Log(logStream, "Invalid texture index did not preserve the original image.");
						std::terminate();
					}
					Logger::Log(logStream, "Sprite smoke: deferred image attached; invalid index rejected.");
					sprites.erase(sprites.begin());
					sprites.clear();
					Logger::Log(logStream, "Sprite smoke: removed one and cleared all, count=0.");
				} else if (smokeTestFrames == 2) {
					createSprite(spriteTextures[0], { 16.0f, 16.0f }, { 160.0f, 160.0f });
					deferredSmokeSprite->SetTextureIndex(UINT32_MAX);
					Logger::Log(logStream, "Sprite smoke: deferred image detached safely.");
					Logger::Log(logStream, "Sprite smoke: recreated after empty, count=1.");
				}
			}
			input->Update();

			// マウス情報の取得開始
			mouse->Acquire();

			// マウスの入力状態（相対移動量・ボタン）を取得する
			DIMOUSESTATE mouseState{};
			mouse->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState);

			if (input->TriggerKey(DIK_0))
			{
				OutputDebugStringA("Hit 0\n"); // 出力ウィンドウに「Hit 0」と表示
			}

			// デバッグカメラのON/OFF切り替え
			if (input->TriggerKey(DIK_F1))
			{
				isDebugCameraActive = !isDebugCameraActive;
			}

			// デバッグカメラの更新処理
			if (isDebugCameraActive)
			{
				// マウス左ボタンが押されているか
				bool isLeftMouseDown = (mouseState.rgbButtons[0] & 0x80) != 0;
				debugCamera.Update(input, mouseState.lX, mouseState.lY, isLeftMouseDown);
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("設定");
			ImGui::ColorEdit4("色", colorArray);
			const char* blendModeNames[] = { "なし", "通常", "加算", "減算", "乗算", "スクリーン" };
			ImGui::Combo("オブジェクトの合成方法", &currentBlendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames));
			ImGui::Combo("パーティクルの合成方法", &currentParticleBlendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames));
			ImGui::Checkbox("モンスターボール用テクスチャ", &useMonsterBall);
			ImGui::Checkbox("ビルボード表示", &useBillboard);
			ImGui::Checkbox("更新する", &update);
			// Emitterの設定
			ImGui::DragFloat3("発生位置", &emitter.transform.translate.x, 0.05f, -100.0f, 100.0f);
			int emitCount = int(emitter.count);
			if (ImGui::DragInt("発生数", &emitCount, 0.1f, 0, 20))
			{
				emitter.count = uint32_t(emitCount);
			}
			ImGui::DragFloat("発生間隔", &emitter.frequency, 0.01f, 0.05f, 5.0f);
			// ボタンを押したらEmitterの設定どおりにParticleが追加される
			if (ImGui::Button("パーティクルを追加"))
			{
				particles.splice(particles.end(), Emit(emitter, randomEngine));
			}
			ImGui::Text("加速度フィールド");
			ImGui::Checkbox("加速度フィールドを使う", &useAccelerationField);
			ImGui::DragFloat3("加速度", &accelerationField.acceleration.x, 0.1f);
			ImGui::DragFloat3("影響範囲 最小", &accelerationField.area.min.x, 0.05f);
			ImGui::DragFloat3("影響範囲 最大", &accelerationField.area.max.x, 0.05f);
			ImGui::Checkbox("パーティクルのインスタンシング", &useInstancing);
			ImGui::Separator();
			ImGui::Text("モンスターボール");
			ImGui::Checkbox("表示する", &showBall);
			const char* modelChoices[] = { "球（共有モデル）", "plane.obj", "terrain.obj" };
			int selectedModel = secondSphere.GetModel() == &planeModel ? 1 : secondSphere.GetModel() == &terrainModel ? 2 : 0;
			if (ImGui::Combo("2体目のモデル", &selectedModel, modelChoices, IM_ARRAYSIZE(modelChoices))) {
				if (selectedModel == 0) { secondSphere.SetModel(&sphereAppearance); }
				else { secondSphere.SetModel(selectedModel == 1 ? "plane.obj" : "terrain.obj"); }
			}
			ImGui::DragFloat3("位置", &transform.translate.x, 0.05f);
			// 全成分を同じ値でスケールする（X,Y,Zが同じ値になる）
			float uniformScale = transform.scale.x;
			if (ImGui::DragFloat("全体の大きさ", &uniformScale, 0.01f, 0.01f, 10.0f))
			{
				transform.scale = { uniformScale, uniformScale, uniformScale };
			}
			ImGui::DragFloat3("各軸の大きさ", &transform.scale.x, 0.01f, 0.01f, 10.0f);
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

			ImGui::Separator();
			ImGui::Text("スプライト");
			ImGui::Checkbox("スプライトを表示", &showSprites);
			const char* spriteTextureNames[] = { "UVチェッカー", "モンスターボール画像", "円形画像", "草", "フェンス", "画像なし" };
			ImGui::Text("読み込み済み画像：%d（同じ画像は共有）", int(textureManager->GetTextureCount()));
			ImGui::Text("生成数：%d / %d", int(sprites.size()), int(kMaxSpriteCount));
			ImGui::Combo("追加する画像", &newSpriteTexture, spriteTextureNames, IM_ARRAYSIZE(spriteTextureNames));
			ImGui::SliderInt("一度に生成する枚数", &spriteCreateCount, 1, 20);
			ImGui::BeginDisabled(sprites.size() >= kMaxSpriteCount);
			if (ImGui::Button("スプライトを追加")) {
				for (int index = 0; index < spriteCreateCount && sprites.size() < kMaxSpriteCount; ++index) {
					const size_t number = sprites.size();
					createSprite(newSpriteTexture == int(spriteTextures.size()) ? std::string{} : spriteTextures[newSpriteTexture],
						{ 16.0f + float(number % 6) * 144.0f, 16.0f + float((number / 6) % 4) * 144.0f },
						{ 128.0f, 128.0f });
				}
				selectedSprite = int(sprites.size()) - 1;
			}
			ImGui::EndDisabled();
			if (!sprites.empty()) {
				selectedSprite = std::clamp(selectedSprite, 0, int(sprites.size()) - 1);
			} else {
				selectedSprite = -1;
			}
			std::string spritePreview = selectedSprite < 0 ? "スプライトなし" : std::format("スプライト {}", selectedSprite + 1);
			if (ImGui::BeginCombo("操作するスプライト", spritePreview.c_str())) {
				for (int index = 0; index < int(sprites.size()); ++index) {
					const std::string name = std::format("スプライト {}", index + 1);
					if (ImGui::Selectable(name.c_str(), selectedSprite == index)) {
						selectedSprite = index;
					}
					if (selectedSprite == index) { ImGui::SetItemDefaultFocus(); }
				}
				ImGui::EndCombo();
			}
			ImGui::BeginDisabled(sprites.empty() || sprites.size() >= kMaxSpriteCount);
			if (ImGui::Button("選択したスプライトを複製")) {
				duplicateSprite(size_t(selectedSprite));
				selectedSprite = int(sprites.size()) - 1;
			}
			ImGui::EndDisabled();
			ImGui::BeginDisabled(sprites.empty());
			if (ImGui::Button("選択したスプライトを削除")) {
				// 前フレームのGPU処理はDirectXBase::PostDrawで完了待ち済み。
				sprites.erase(sprites.begin() + selectedSprite);
				selectedSprite = sprites.empty() ? -1 : std::clamp(selectedSprite, 0, int(sprites.size()) - 1);
			}
			ImGui::SameLine();
			if (ImGui::Button("すべてのスプライトを削除")) {
				sprites.clear();
				selectedSprite = -1;
			}
			ImGui::EndDisabled();
			if (selectedSprite >= 0) {
				Sprite& editedSprite = *sprites[selectedSprite];
				int currentTexture = editedSprite.HasTexture() ? 0 : int(spriteTextures.size());
				for (int index = 0; index < int(spriteTextures.size()); ++index) {
					if (editedSprite.GetTextureFilePath() == spriteTextures[index]) { currentTexture = index; break; }
				}
				if (ImGui::Combo("選択中の画像", &currentTexture, spriteTextureNames, IM_ARRAYSIZE(spriteTextureNames))) {
					if (currentTexture == int(spriteTextures.size())) { editedSprite.SetTextureIndex(UINT32_MAX); }
					else { editedSprite.SetTexture(spriteTextures[currentTexture]); }
				}
				if (editedSprite.HasTexture()) {
					const auto& imageMetadata = textureManager->GetMetadata(editedSprite.GetTextureIndex());
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

			ImGui::Separator();
			ImGui::Text("モデルの変形");

			ImGui::SliderAngle("X軸回転", &transform.rotate.x);
			ImGui::SliderAngle("Y軸回転", &transform.rotate.y);
			ImGui::SliderAngle("Z軸回転", &transform.rotate.z);

			ImGui::Separator();
			ImGui::Text("デバッグカメラ");
			ImGui::Text(isDebugCameraActive ? "状態：有効（F1で無効化）" : "状態：無効（F1で有効化）");
			ImGui::Checkbox("デバッグカメラを有効にする", &isDebugCameraActive);
			ImGui::Text("移動：W/A/S/D、上下：E/Q");
			ImGui::Text("視点操作：マウス");
			{
				Vector3 debugCamPos = debugCamera.GetTranslation();
				if (ImGui::DragFloat3("カメラ位置", &debugCamPos.x, 0.1f))
				{
					debugCamera.SetTranslation(debugCamPos);
				}
			}

			ImGui::End();

			ImGui::Render();
#endif

			// カメラの行列（デバッグカメラが有効な場合はそちらを使用）
			Matrix4x4 viewMatrix{};
			if (isDebugCameraActive)
			{
				viewMatrix = debugCamera.GetViewMatrix();
			}
			else
			{
				Matrix4x4 cameraMatrix = Math::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				viewMatrix = Math::Inverse(cameraMatrix); // カメラの逆行列がビュー行列
			}

			// 投影行列
			Matrix4x4 projectionMatrix = Math::MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);

			// Instancing用のTransformationMatrixを更新する
			Matrix4x4 viewProjectionMatrix = Math::Multiply(viewMatrix, projectionMatrix);

			// BillboardMatrixを作る（全てのParticleで同じ行列を使える）
			Matrix4x4 billboardMatrix = Math::MakeIdentity4x4(); // 利用しない場合は単位行列
			if (useBillboard)
			{
				// ビュー行列の逆行列がカメラのワールド行列（デバッグカメラ使用時も同じ式で求まる）
				Matrix4x4 cameraMatrix = Math::Inverse(viewMatrix);
				// 板ポリの裏が見えているので、Y軸でπ回転させて表をカメラ側へ向ける
				Matrix4x4 backToFrontMatrix = Math::MakeRotateYMatrix(std::numbers::pi_v<float>);
				billboardMatrix = Math::Multiply(backToFrontMatrix, cameraMatrix);
				// 平行移動成分はいらない
				billboardMatrix.m[3][0] = 0.0f;
				billboardMatrix.m[3][1] = 0.0f;
				billboardMatrix.m[3][2] = 0.0f;
			}
			// Emitterの頻度に従ってParticleを発生させる
			if (update)
			{
				emitter.frequencyTime += kDeltaTime; // 時刻を進める
				if (emitter.frequency <= emitter.frequencyTime) // 頻度より大きいなら発生
				{
					// 発生処理。発生したParticleをparticlesの後ろに連結する
					particles.splice(particles.end(), Emit(emitter, randomEngine));
					// 余計に過ぎた時間も加味して頻度用時刻を更新する
					emitter.frequencyTime -= emitter.frequency;
				}
			}

			uint32_t numInstance = 0; // 描画すべきインスタンス数
			for (std::list<Particle>::iterator particleIterator = particles.begin();
				particleIterator != particles.end(); )
			{
				// 生存期間が過ぎたParticleはlistから消す。戻り値が次のイテレータとなる
				if ((*particleIterator).lifeTime <= (*particleIterator).currentTime)
				{
					particleIterator = particles.erase(particleIterator);
					continue;
				}

				if (update)
				{
					// Fieldの範囲内のParticleには加速度を適用する
					if (useAccelerationField && Math::IsCollision(accelerationField.area, (*particleIterator).transform.translate))
					{
						(*particleIterator).velocity.x += accelerationField.acceleration.x * kDeltaTime;
						(*particleIterator).velocity.y += accelerationField.acceleration.y * kDeltaTime;
						(*particleIterator).velocity.z += accelerationField.acceleration.z * kDeltaTime;
					}

					// 速度を反映して移動させる
					(*particleIterator).transform.translate.x += (*particleIterator).velocity.x * kDeltaTime;
					(*particleIterator).transform.translate.y += (*particleIterator).velocity.y * kDeltaTime;
					(*particleIterator).transform.translate.z += (*particleIterator).velocity.z * kDeltaTime;
					(*particleIterator).currentTime += kDeltaTime; // 経過時間を足す
				}

				// GPU側のバッファは最大数固定なので、範囲外に書き込まないようにする（バッファオーバーラン対策）
				if (numInstance < kNumMaxInstance)
				{
					// scale * billboard * translate
					Matrix4x4 scaleMatrix = Math::MakeScaleMatrix((*particleIterator).transform.scale);
					Matrix4x4 translateMatrix = Math::MakeTranslateMatrix((*particleIterator).transform.translate);
					Matrix4x4 instanceWorldMatrix = Math::Multiply(Math::Multiply(scaleMatrix, billboardMatrix), translateMatrix);
					Matrix4x4 instanceWVPMatrix = Math::Multiply(instanceWorldMatrix, viewProjectionMatrix);
					instancingData[numInstance].WVP = instanceWVPMatrix;
					instancingData[numInstance].World = instanceWorldMatrix;
					instancingData[numInstance].color = (*particleIterator).color;
					// 経過時間が生存時間に近づくほどαを0に近づける
					float alpha = 1.0f - ((*particleIterator).currentTime / (*particleIterator).lifeTime);
					instancingData[numInstance].color.w = alpha;
					++numInstance; // 生きているParticleの数を1つカウントする
				}
				++particleIterator; // 次のイテレータに進める
			}

			materialData->uvTransform = Math::MakeIdentity4x4();

			// ImGuiでの色変更
			materialData->color.x = colorArray[0];
			materialData->color.y = colorArray[1];
			materialData->color.z = colorArray[2];
			materialData->color.w = colorArray[3];

			planeObject.GetTransform() = transform;
			planeModel.SetTexture(useMonsterBall ? modelData.material.textureFilePath : "Resources/uvChecker.png");
			for (Object3DBase* object : { &sphereObject, &planeObject, &terrainObject, &secondSphere }) {
				if (object != &sphereObject) {
					// 既存のUI操作を全モデルへ反映。バッファ自体は個別に所有する。
					object->GetModel()->GetMaterial() = *materialData;
					object->GetDirectionalLight() = *directionalLightData;
					object->GetPointLight() = *pointLightData;
					object->GetSpotLight() = *spotLightData;
					object->GetCameraTransform() = cameraTransform;
				}
				object->Update(viewMatrix, projectionMatrix);
			}
			*particleMaterialData = *materialData;

			// 指定した色で画面全体をクリアする
			float clearColor[] = { 0.1f,0.25f,0.5f,1.0f };
			directXBase->PreDraw(clearColor);

			planeObject.SetCommonRenderState(static_cast<BlendMode>(currentBlendMode));
			terrainObject.Draw();
			if (!useInstancing) { planeObject.Draw(); }
			if (showBall) { sphereObject.Draw(); secondSphere.Draw(); }

			////////////////// Particle(Instancing)の描画
			// RootSignatureが変わるので、必要なものは全て設定し直す
			if (useInstancing)
			{
				assert(kNumMaxInstance <= 100); // 確保したResourceの数を超えないようにする

				commandList->SetGraphicsRootSignature(particleRootSignature.Get());
				commandList->SetPipelineState(particlePipelineStates[currentParticleBlendMode].Get());
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				// Instancing用のStructuredBufferのSRVを設定する
				commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU3); // circle.png

				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewModel);

				// 生きているParticleの数(numInstance)だけInstance描画を行う
				if (numInstance > 0)
				{
					commandList->DrawInstanced(UINT(modelData.vertices.size()), numInstance, 0, 0);
				}
			}
			///////////////////////////////////

			// 3DとParticleの後に、深度を使わないSprite専用パイプラインで描画する。
			if (showSprites) {
				// Sprite全体の描画ルールを1回設定し、その後に各Spriteを描画する。
				spriteBase.SetCommonRenderState();
				for (const auto& sprite : sprites) {
					sprite->Update();
					sprite->Draw();
				}
				if (deferredSmokeSprite) {
					deferredSmokeSprite->Update();
					deferredSmokeSprite->Draw();
				}
			}
#ifdef USE_IMGUI
			// 実際のcommandListのImGuiの描画コマンドを頼む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif
			//////////////////////////////////////////////////////////////////////////////////////////


			directXBase->PostDraw();
			// PostDrawのフェンス待ち後なので、転送用リソースを安全に解放できる。
			textureManager->ReleaseUploadResources();
			if (smokeTest && textureManager->GetPendingUploadCount() != 0) {
				Logger::Log(logStream, "Texture upload resources were not released after GPU completion.");
				std::terminate();
			}
			if (smokeTest && ++smokeTestFrames == 3)
			{
				Logger::Log(logStream, "Smoke test completed: 3 frames rendered.");
				break;
			}
		}
	}

	// 文字列を格納する
	std::string str0{ "STRING!!!" };
	int number = 10;

	// 整数を文字列にする
	std::string str1{ std::to_string(10) };

	Logger::Log(logStream, std::format("End Message: {}, Value: {}", str0, number));
	Logger::Log(logStream, "Hello, DirectX! Shutdown successful.");

#ifdef USE_IMGUI
	// ImGuiの終了処理
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	// 最終フレームのGPU完了後、借りているDirectXBaseを破棄する前にSpriteを解放する。
	sprites.clear();
	deferredSmokeSprite.reset();
	secondSphere.Finalize();
	sphereObject.Finalize();
	planeObject.Finalize();
	terrainObject.Finalize();
	sphereAppearance.Finalize();
	ModelManager::Finalize();
	TextureManager::Finalize();
	directXBase->Finalize();
	delete directXBase;


	input->Finalize();
	delete input;

	CloseWindow(hwnd);
	// ウィンドウの終了処理
	winApp->Finalize();
	delete winApp;

	CoUninitialize();

	// 音声データ解放
	sound1.Unload();

	// xAudio2解放
	xAudio2.Reset();

	return 0;
}
