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
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#include <cassert>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#include <strsafe.h>
#pragma comment(lib,"dxguid.lib")
#include <dxcapi.h>

#include <xaudio2.h>
#pragma comment(lib,"xaudio2.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

#include <DirectXTex.h>

#include "Matrix4x4.h"
#include "ResourceObject.h"
#include "Sound.h"
#include "DebugCamera.h"
#include "Input.h"
#include "WinApp.h"
#include "DirectXBase.h"
#include "D3DResourceLeakChecker.h"
#include "Logger.h"

#include <sstream>

#include <random>

using Microsoft::WRL::ComPtr;


struct Vector4
{
	float x, y, z, w;
};

namespace FKEngine
{
	struct Particle
	{
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};
}

struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 World;
};

// Instancing用（StructuredBufferに入れる）。HLSL側のParticleForGPUと並びを合わせる
struct ParticleForGPU
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

struct VertexData
{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct Material
{
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct MaterialData
{
	std::string textureFilePath;
};

struct DirectionalLight
{
	Vector4 color;     // ライトの色
	Vector3 direction; // ライトの向き
	float intensity;   // 輝度
};


struct ModelData
{
	std::vector<VertexData> vertices;
	MaterialData material;
};

struct Particle
{
	FKEngine::Particle transform;
	Vector3 velocity;
	Vector4 color;
	float lifeTime;
	float currentTime;
};

struct ParticleGPU
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

// ---------------------------------------------
// BlendMode
// ---------------------------------------------
enum BlendMode
{
	kBlendModeNone,      // ブレンドなし
	kBlendModeNormal,    // 通常αブレンド。Src * SrcA + Dest * (1 - SrcA)
	kBlendModeAdd,       // 加算。Src * SrcA + Dest * 1
	kBlendModeSubtract,  // 減算。Dest * 1 - Src * SrcA
	kBlendModeMultiply,  // 乗算。Src * 0 + Dest * Src
	kBlendModeScreen,    // スクリーン。Src * (1 - Dest) + Dest * 1
	kCountOfBlendMode,   // 利用してはいけない
};

D3D12_BLEND_DESC CreateBlendDesc(BlendMode mode)
{
	D3D12_BLEND_DESC desc{};
	auto& rt = desc.RenderTarget[0];

	// 全ての色要素を書き込む
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// α値のブレンド設定は全モード共通
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.DestBlendAlpha = D3D12_BLEND_ZERO;

	if (mode == kBlendModeNone)
	{
		rt.BlendEnable = FALSE;
		return desc;
	}

	rt.BlendEnable = TRUE;

	switch (mode)
	{
	case kBlendModeNormal:
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		break;
	case kBlendModeAdd:
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_ONE;
		break;
	case kBlendModeSubtract:
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		rt.DestBlend = D3D12_BLEND_ONE;
		break;
	case kBlendModeMultiply:
		rt.SrcBlend = D3D12_BLEND_ZERO;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_SRC_COLOR;
		break;
	case kBlendModeScreen:
		rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.DestBlend = D3D12_BLEND_ONE;
		break;
	default:
		break;
	}
	return desc;
}

BYTE key[256] = {};

BYTE keyPre[256] = {};


static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	// 時刻を取得して、時刻を名前に入れたファイルを作成、Dumpsディレクタ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

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

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
	// 1. 中で必要となる変数の宣言
	MaterialData materialData; // 構築するMaterialData
	std::string line; //ファイルから読んだ1行を格納するもの

	// 2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める

	// 3. 実際にファイルを読み、MaterialDataを構築していく
	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		// identifierに応じた処理
		if (identifier == "map_Kd")
		{
			std::string textureFilename;
			s >> textureFilename;
			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	// 4. MaterialDataを返す
	return materialData;
}


ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
	// 1. 中で必要となる変数の宣言
	ModelData modelData; // 構築するModelData
	std::vector<Vector4> positions; // 位置
	std::vector<Vector3> normals;   // 法線
	std::vector<Vector2> texcoords; // テクスチャ座標
	std::string line; // ファイルを1行ずつ読むための変数

	// 2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める

	// 3. 実際にファイルを読み、ModelDataに構築していく
	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 先頭の識別子を読む

		// identifierに応じた処理
		if (identifier == "v")
		{
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);
		}
		else if (identifier == "vt")
		{
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		}
		else if (identifier == "vn")
		{
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		}
		else if (identifier == "f")
		{
			// 3頂点分を一時的に保存するための配列
			VertexData triangle[3];

			// 面は三角形限定。その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
			{
				std::string vertexDefinition;
				s >> vertexDefinition;

				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0; element < 3; ++element)
				{
					std::string index;
					std::getline(v, index, '/'); // 区切りでインデックスを読んでいく
					elementIndices[element] = std::stoi(index);
				}

				// 要素へのIndexから、実際の要素の値を取得する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];

				position.z *= -1.0f;
				normal.z *= -1.0f;

				VertexData vertex = { position, texcoord, normal };
				modelData.vertices.push_back(vertex);
				triangle[faceVertex] = { position,texcoord,normal };
			}

			// 頂点を逆順で登録することで、回り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		}
		else if (identifier == "mtllib")
		{
			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;

			// 基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}

	// 4. ModelDataを返す
	return modelData;
}


std::random_device seedGenerator;
std::mt19937 randomEngine(seedGenerator());


Particle MakeNewParticle(std::mt19937& randomEngine)
{
	std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
	std::uniform_real_distribution<float> distTime(1.0f, 3.0f);

	Particle particle;
	particle.transform.scale = { 1.0f,1.0f,1.0f };
	particle.transform.rotate = { 0.0f,0.0f,0.0f };
	particle.transform.translate = { distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };
	particle.velocity = { distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };
	particle.color = { distColor(randomEngine),distColor(randomEngine),distColor(randomEngine),1.0f };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTime = 0.0f;
	return particle;
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	D3DResourceLeakChecker leakCheck;

	ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice;

	Sound sound1;


	CoInitializeEx(0, COINIT_MULTITHREADED);
	// 誰も補足しなかった場合に(Unhandled), 補足する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	// ログのディレクトリを用意
	std::filesystem::create_directory("logs");

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
	std::string logFilePath = std::format("logs/") + dateString + ".log";

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
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device.Get(),
		DirectXBase::kBackBufferCount,
		rtvFormat,
		srvDescriptorHeap.Get(),
		GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0));
	ImGuiIO io = ImGui::GetIO();
	io.Fonts->Build();
#endif
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	// RootParameterの設定
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

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
	if (FAILED(hr))
	{
		Logger::Log(logStream, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリを元に生成
	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

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
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = directXBase->CompileShader(L"Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = directXBase->CompileShader(L"Object3d.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

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
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
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
	ComPtr<ID3D12PipelineState> graphicsPipelineStates[kCountOfBlendMode];
	for (int i = 0; i < kCountOfBlendMode; ++i)
	{
		graphicsPipelineStateDesc.BlendState = CreateBlendDesc(static_cast<BlendMode>(i));
		hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
			IID_PPV_ARGS(&graphicsPipelineStates[i]));
		assert(SUCCEEDED(hr));
	}

	// 現在のBlendMode
	int currentBlendMode = kBlendModeAdd; // Particleは加算ブレンドで描画する

	////////////////////////////// Particle用 ////////////////////////////
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
	Microsoft::WRL::ComPtr<IDxcBlob> particleVertexShaderBlob = directXBase->CompileShader(L"Particle.VS.hlsl", L"vs_6_0");
	assert(particleVertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> particlePixelShaderBlob = directXBase->CompileShader(L"Particle.PS.hlsl", L"ps_6_0");
	assert(particlePixelShaderBlob != nullptr);

	// Particle用のPSO（Object3d用の設定をコピーして、RootSignatureとShaderだけ差し替える）
	D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePipelineStateDesc = graphicsPipelineStateDesc;
	particlePipelineStateDesc.pRootSignature = particleRootSignature.Get();
	// Particleは深度を書き込まない（DepthTestはそのまま行うので、不透明なObjectとの前後関係は保たれる）
	particlePipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	particlePipelineStateDesc.VS = { particleVertexShaderBlob->GetBufferPointer(), particleVertexShaderBlob->GetBufferSize() };
	particlePipelineStateDesc.PS = { particlePixelShaderBlob->GetBufferPointer(), particlePixelShaderBlob->GetBufferSize() };

	ComPtr<ID3D12PipelineState> particlePipelineStates[kCountOfBlendMode];
	for (int i = 0; i < kCountOfBlendMode; ++i)
	{
		particlePipelineStateDesc.BlendState = CreateBlendDesc(static_cast<BlendMode>(i));
		hr = device->CreateGraphicsPipelineState(&particlePipelineStateDesc,
			IID_PPV_ARGS(&particlePipelineStates[i]));
		assert(SUCCEEDED(hr));
	}

	////////////////////////////// モデル読み込み ////////////////////////////
	ModelData modelData = LoadObjFile("Resources", "plane.obj");
	//ModelData modelDataAxis = LoadObjFile("Resources", "axis.obj");

	// 頂点リソースを作る
	ComPtr<ID3D12Resource> vertexResourceModel = directXBase->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewModel{};
	vertexBufferViewModel.BufferLocation = vertexResourceModel->GetGPUVirtualAddress(); // リソースの先頭アドレスから使う
	vertexBufferViewModel.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size()); // 使用するリソースのサイズは頂点のサイズ
	vertexBufferViewModel.StrideInBytes = sizeof(VertexData);

	// 頂点リソースにデータを書き込む
	VertexData* vertexDataModel = nullptr;
	vertexResourceModel->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataModel));
	std::memcpy(vertexDataModel, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size()); // 頂点データをリソースにコピー


	/////////////////////////////////////////////////////////////////////////


	///////////////////// 頂点リソース用のヒープの設定

	const uint32_t kSubdivision = 32;
	const uint32_t kVertexCount = kSubdivision * kSubdivision * 6;

	ComPtr<ID3D12Resource> vertexResource = directXBase->CreateBufferResource(sizeof(VertexData) * kVertexCount);

	// Sprite用の頂点リソースを作る
	ComPtr<ID3D12Resource> vertexResourceSprite = directXBase->CreateBufferResource(sizeof(VertexData) * 6);

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	ComPtr<ID3D12Resource> materialResource = directXBase->CreateBufferResource(sizeof(Material));

	// マテリアルにデータを書き込む
	Material* materialData = nullptr;

	// Sprite用のマテリアルリソースを作る
	ComPtr<ID3D12Resource> materialResourceSprite = directXBase->CreateBufferResource(sizeof(Material));

	Material* materialDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	hr = materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	assert(SUCCEEDED(hr));

	// 書き込むためのアドレスを取得
	hr = materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	assert(SUCCEEDED(hr));

	// WHITE
	materialData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = 1;

	materialDataSprite->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };

	// ImGuiでの初期化
	float colorArray[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	// WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ComPtr<ID3D12Resource> wvpResource = directXBase->CreateBufferResource(sizeof(TransformationMatrix));

	// データを書き込む
	Matrix4x4* wvpData = nullptr;

	TransformationMatrix* transformationMatrixData = nullptr;

	// 書き込むためのアドレスを取得

	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));

	// 単位行列を書き込んでおく
	transformationMatrixData->WVP = MakeIdentity4x4();
	transformationMatrixData->World = MakeIdentity4x4();

	FKEngine::Particle transformSprite{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	// Sprite用のtransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ComPtr<ID3D12Resource> transformationMatrixResourceSprite = directXBase->CreateBufferResource(sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* transformationMatrixDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	// 単位行列を書き込んでおく
	*transformationMatrixDataSprite = MakeIdentity4x4();

	ComPtr<ID3D12Resource> indexResourceSprite = directXBase->CreateBufferResource(sizeof(uint32_t) * 6);


	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};

	// リソースの先頭アドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();

	// 使用するリソースのサイズはインデックス6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;

	// インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	// インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));

	indexDataSprite[0] = 0;  indexDataSprite[1] = 1;   indexDataSprite[2] = 2;
	indexDataSprite[3] = 3;  indexDataSprite[4] = 4;   indexDataSprite[5] = 5;


	// VBV
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};

	// リソールの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * kVertexCount;
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;

	// 頂点1つ分のサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;
	VertexData* vertexDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	assert(SUCCEEDED(hr));

	ComPtr<ID3D12Resource> directionalLightResource = nullptr;
	DirectionalLight* directionalLightData = nullptr;

	directionalLightResource = directXBase->CreateBufferResource(sizeof(DirectionalLight));

	// MapしてCPU側のアドレスを取得し、初期データを書き込む
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };      // 白色の光
	directionalLightData->direction = { 0.0f, -1.0f, 1.0f };     // 斜め下・手前方向へ進む光
	directionalLightData->intensity = 1.0f;
	materialDataSprite->enableLighting = 0;

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

	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// 1枚目の三角形
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f }; // 左下
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[1].position = { 640.0f, 360.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[1].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[2].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite[2].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };

	// 2枚目の三角形 (左上 → 右下 → 右上 の順で結ぶ：コード上は反時計回り)
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[4].position = { 640.0f, 360.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[4].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[4].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[5].position = { 640.0f, 0.0f, 0.0f, 1.0f }; // 右上
	vertexDataSprite[5].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[5].normal = { 0.0f, 0.0f, -1.0f };

	// Transform変数を作る
	FKEngine::Particle transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	FKEngine::Particle cameraTransform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-10.0f} };

	// デバッグカメラ
	DebugCamera debugCamera;
	debugCamera.Initialize(kClientWidth, kClientHeight);
	debugCamera.SetTranslation(cameraTransform.translate); // 通常カメラと同じ位置から開始

	// デバッグカメラが有効かどうか
	bool isDebugCameraActive = false;


	FKEngine::Particle uvTransformSprite
	{
		{1.0f,1.0f,1.0f},
		{0.0f,0.0f,0.0f},
		{0.0f,0.0f,0.0f},
	};


	// Textureを読んで転送する
	DirectX::ScratchImage mipImages = DirectXBase::LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	ComPtr<ID3D12Resource> textureResource = directXBase->CreateTextureResource(metadata);

	ComPtr<ID3D12Resource> intermediateResource = directXBase->UploadTextureData(textureResource, mipImages);

	DirectX::ScratchImage mipImages2 = DirectXBase::LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	ComPtr<ID3D12Resource> textureResource2 = directXBase->CreateTextureResource(metadata2);
	ComPtr<ID3D12Resource> intermediateResource2 = directXBase->UploadTextureData(textureResource2, mipImages2);

	// Particle用のTexture(circle.png)
	DirectX::ScratchImage mipImages3 = DirectXBase::LoadTexture("resources/circle.png");
	const DirectX::TexMetadata& metadata3 = mipImages3.GetMetadata();
	ComPtr<ID3D12Resource> textureResource3 = directXBase->CreateTextureResource(metadata3);
	ComPtr<ID3D12Resource> intermediateResource3 = directXBase->UploadTextureData(textureResource3, mipImages3);


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


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc3{};
	srvDesc3.Format = metadata3.format;
	srvDesc3.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc3.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc3.Texture2D.MipLevels = UINT(metadata3.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);

	// Heapの4番目にParticle用のcircle.pngのSRVを作成する（0:ImGui 1,2:Texture 3:Instancing 4:circle）
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);


	// SRVの生成
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);
	device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);
	device->CreateShaderResourceView(textureResource3.Get(), &srvDesc3, textureSrvHandleCPU3);

	////////////////////////////// Instancing ////////////////////////////
	const uint32_t kNumMaxInstance = 10; // 最大インスタンス数

	// Instancing用のTransformationMatrixリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
		directXBase->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);

	// 書き込むためのアドレスを取得
	ParticleForGPU* instancingData = nullptr;
	instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

	// 単位行列を書き込んでおく
	for (uint32_t index = 0; index < kNumMaxInstance; ++index)
	{
		instancingData[index].WVP = MakeIdentity4x4();
		instancingData[index].World = MakeIdentity4x4();
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

	// Heapの3番目にInstancing用のSRVを作成する（0:ImGui 1,2:Texture）
	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
	device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

	// Instancing用に最大数分のParticleを用意し、位置が少しずつずれるように初期化する
	Particle particles[kNumMaxInstance];
	for (uint32_t index = 0; index < kNumMaxInstance; ++index)
	{
		particles[index] = MakeNewParticle(randomEngine);
	}

	// Δtを定義。とりあえず60fps固定
	const float kDeltaTime = 1.0f / 60.0f;

	// trueでParticle用のPipeline(Instancing描画)、falseで従来のObject3d用Pipelineで描画する
	bool useInstancing = true;

	bool useMonsterBall = false; // falseでuvChecker.pngを貼る

	// xAudioエンジンのインスタンスを生成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);

	// マスターボイスを生成
	hr = xAudio2->CreateMasteringVoice(&masterVoice);

	// 音声読み込み
	sound1.Load("Resources/Alarm01.wav");

	// 音声再生
	sound1.Play(xAudio2.Get());

	while (true)
	{
		// ウィンドウのメッセージ処理。終了メッセージが来たらループを抜ける
		if (winApp->ProcessMessage())
		{
			break;
		}
		else
		{
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

			ImGui::ShowDemoWindow();

			ImGui::Begin("Material Window");
			ImGui::ColorEdit4("Color", colorArray);
			const char* blendModeNames[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
			ImGui::Combo("BlendMode", &currentBlendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames));
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);
			ImGui::Checkbox("Use Instancing (Particle)", &useInstancing);
			ImGui::Separator();
			ImGui::Text("Lighting Setting");

			bool enableLighting = (materialData->enableLighting != 0);
			if (ImGui::Checkbox("Enable Lighting", &enableLighting))
			{
				materialData->enableLighting = enableLighting ? 1 : 0;
			}

			// 平行光源の向きの変更
			float lightDirection[3] = { directionalLightData->direction.x, directionalLightData->direction.y, directionalLightData->direction.z };
			if (ImGui::SliderFloat3("Light Direction", lightDirection, -1.0f, 1.0f))
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
			ImGui::SliderFloat("Light Intensity", &directionalLightData->intensity, 0.0f, 3.0f);

			ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);

			ImGui::Separator();
			ImGui::Text("Model Transform");

			ImGui::SliderAngle("Model Rotate X", &transform.rotate.x);
			ImGui::SliderAngle("Model Rotate Y", &transform.rotate.y);
			ImGui::SliderAngle("Model Rotate Z", &transform.rotate.z);

			ImGui::Separator();
			ImGui::Text("Debug Camera");
			ImGui::Text(isDebugCameraActive ? "State : ON (F1 to disable)" : "State : OFF (F1 to enable)");
			ImGui::Checkbox("Debug Camera Active", &isDebugCameraActive);
			ImGui::Text("Move : W/A/S/D, Up/Down : E/Q");
			ImGui::Text("Look : Mouse");
			{
				Vector3 debugCamPos = debugCamera.GetTranslation();
				if (ImGui::DragFloat3("Debug Cam Position", &debugCamPos.x, 0.1f))
				{
					debugCamera.SetTranslation(debugCamPos);
				}
			}

			ImGui::End();

			ImGui::Render();
#endif

			//transform.rotate.y += 0.01f;



			// オブジェクトのワールド行列
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);

			// カメラの行列（デバッグカメラが有効な場合はそちらを使用）
			Matrix4x4 viewMatrix{};
			if (isDebugCameraActive)
			{
				viewMatrix = debugCamera.GetViewMatrix();
			}
			else
			{
				Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				viewMatrix = Inverse(cameraMatrix); // カメラの逆行列がビュー行列
			}
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4(); // スプライトはカメラの影響を受けないので、ビュー行列は単位行列

			// 投影行列
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);

			// 行列の合成
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));

			// 定数バッファへ書き込み
			transformationMatrixData->WVP = worldViewProjectionMatrix;
			transformationMatrixData->World = worldMatrix;

			// Instancing用のTransformationMatrixを更新する
			Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
			uint32_t numInstance = 0; // 描画すべきインスタンス数
			for (uint32_t index = 0; index < kNumMaxInstance; ++index)
			{
				// 生存期間を過ぎていたら更新せず描画対象にしない
				if (particles[index].lifeTime <= particles[index].currentTime)
				{
					continue;
				}

				// 速度を反映して移動させる
				particles[index].transform.translate.x += particles[index].velocity.x * kDeltaTime;
				particles[index].transform.translate.y += particles[index].velocity.y * kDeltaTime;
				particles[index].transform.translate.z += particles[index].velocity.z * kDeltaTime;
				particles[index].currentTime += kDeltaTime; // 経過時間を足す

				Matrix4x4 instanceWorldMatrix = MakeAffineMatrix(
					particles[index].transform.scale,
					particles[index].transform.rotate,
					particles[index].transform.translate);
				Matrix4x4 instanceWVPMatrix = Multiply(instanceWorldMatrix, viewProjectionMatrix);
				instancingData[numInstance].WVP = instanceWVPMatrix;
				instancingData[numInstance].World = instanceWorldMatrix;
				instancingData[numInstance].color = particles[index].color;
				++numInstance; // 生きているParticleの数を1つカウントする
			}

			*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;

			// UVTransform用の行列
			Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));

			materialDataSprite->uvTransform = uvTransformMatrix;

			materialData->uvTransform = MakeIdentity4x4();

			// ImGuiでの色変更
			materialData->color.x = colorArray[0];
			materialData->color.y = colorArray[1];
			materialData->color.z = colorArray[2];
			materialData->color.w = colorArray[3];

			// 指定した色で画面全体をクリアする
			float clearColor[] = { 0.1f,0.25f,0.5f,1.0f };
			directXBase->PreDraw(clearColor);

			// RootSignatureを設定。PSOに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(graphicsPipelineStates[currentBlendMode].Get());     // PSOを設定
			// 形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけばよい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


			// 探索用のDescriptorHeapの設定
			ComPtr<ID3D12DescriptorHeap> descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps->GetAddressOf());
			// マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

			/////////////////// 三角形用の描画
			//commandList->IASetVertexBuffers(0, 1, &vertexBufferView); //  球のVBVを設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewModel); // モデルのVBVを設定

			//commandList->DrawInstanced(kVertexCount, 1, 0, 0);
			if (!useInstancing)
			{
				commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
			}

			///////////////////////////////////

			////////////////// Spriteの描画。変更が必要なものだけ変更する
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite); // VBVを設定

			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

			// transformationMatrixの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

			commandList->IASetIndexBuffer(&indexBufferViewSprite);

			// 描画！（DrawCall/ドローコール）6頂点で1つのインスタンス。
			//commandList->DrawInstanced(6, 1, 0, 0);
			//commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			///////////////////////////////////

			////////////////// Particle(Instancing)の描画
			// RootSignatureが変わるので、必要なものは全て設定し直す
			if (useInstancing)
			{
				assert(kNumMaxInstance <= 10); // 確保したResourceの数を超えないようにする

				commandList->SetGraphicsRootSignature(particleRootSignature.Get());
				commandList->SetPipelineState(particlePipelineStates[currentBlendMode].Get());
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

#ifdef USE_IMGUI
			// 実際のcommandListのImGuiの描画コマンドを頼む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif
			//////////////////////////////////////////////////////////////////////////////////////////


			directXBase->PostDraw();
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
