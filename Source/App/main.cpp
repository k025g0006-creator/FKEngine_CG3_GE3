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
#include <numbers>
#include <list>

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
	Matrix4x4 WorldInverseTranspose; // 法線を変換するための行列（Worldの逆転置行列）
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
	float shininess;     // 光沢度（大きいほどハイライトが小さく鋭くなる）
	Vector3 specularColor; // 物体の鏡面反射色（shininessと合わせて16byteになる）
	int32_t useBlinnPhong; // 1:Blinn-Phong、0:Phong（比較用に切り替えられる）
	float padding3[3];     // 全体を16byteの倍数(128byte)に揃える
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

struct PointLight
{
	Vector4 color;     // ライトの色
	Vector3 position;  // ライトの位置
	float intensity;   // 輝度
	float radius;      // ライトの影響範囲
	float decay;       // 影響範囲内での減衰率
	float padding[2];  // 定数バッファを16byte境界に揃える
};

struct SpotLight
{
	Vector4 color;     // ライトの色
	Vector3 position;  // ライトの位置
	float intensity;   // 輝度
	Vector3 direction; // 光が進む方向
	float distance;    // 影響範囲
	float decay;       // 距離による減衰率
	float cosAngle;         // 照射範囲の境界角のcos
	float cosFalloffStart;  // Falloffを開始する角度のcos
	float padding[1];       // 定数バッファを16byte境界に揃える
};

// カメラのワールド位置（PixelShaderへ送る。HLSL側のCameraと並びを合わせる）
struct CameraForGPU
{
	Vector3 worldPosition;
	float padding; // 定数バッファは16byte単位で読まれるので、float3だけだと範囲外を読んでしまう。16byteに揃える
};

// CPU側の配置がシェーダーの定数バッファ定義と一致していることを確認する。
static_assert(sizeof(TransformationMatrix) == 192);
static_assert(sizeof(Material) == 128);
static_assert(sizeof(PointLight) == 48);
static_assert(sizeof(SpotLight) == 64);
static_assert(sizeof(CameraForGPU) == 16);


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

// Particleを発生させる装置
struct Emitter
{
	FKEngine::Particle transform; // エミッタのTransform（translateが発生位置）
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
		GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0));
	const std::u8string imguiIniPathUtf8 = (GetExecutableDirectory() / "Config/imgui.ini").u8string();
	const std::string imguiIniPath(imguiIniPathUtf8.begin(), imguiIniPathUtf8.end());
	io.IniFilename = imguiIniPath.c_str();
	io.Fonts->Build();
#endif
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
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = directXBase->CompileShader(L"Resources/Shaders/Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = directXBase->CompileShader(L"Resources/Shaders/Object3d.PS.hlsl", L"ps_6_0");
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

	// モデル類は通常の不透明描画、Particleは加算ブレンドで描画する
	int currentBlendMode = kBlendModeNone;
	int currentParticleBlendMode = kBlendModeAdd;

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
	Microsoft::WRL::ComPtr<IDxcBlob> particleVertexShaderBlob = directXBase->CompileShader(L"Resources/Shaders/Particle.VS.hlsl", L"vs_6_0");
	assert(particleVertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> particlePixelShaderBlob = directXBase->CompileShader(L"Resources/Shaders/Particle.PS.hlsl", L"ps_6_0");
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
	ModelData terrainData = LoadObjFile("Resources", "terrain.obj");
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

	ComPtr<ID3D12Resource> terrainVertexResource = directXBase->CreateBufferResource(
		sizeof(VertexData) * terrainData.vertices.size());
	D3D12_VERTEX_BUFFER_VIEW terrainVertexBufferView{};
	terrainVertexBufferView.BufferLocation = terrainVertexResource->GetGPUVirtualAddress();
	terrainVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * terrainData.vertices.size());
	terrainVertexBufferView.StrideInBytes = sizeof(VertexData);
	VertexData* terrainVertexData = nullptr;
	hr = terrainVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&terrainVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(terrainVertexData, terrainData.vertices.data(), sizeof(VertexData) * terrainData.vertices.size());


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
	materialData->shininess = 40.0f;
	materialData->specularColor = { 0.25f, 0.25f, 0.25f };
	materialData->useBlinnPhong = 1;

	materialDataSprite->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };

	// ImGuiでの初期化
	float colorArray[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	// WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ComPtr<ID3D12Resource> wvpResource = directXBase->CreateBufferResource(sizeof(TransformationMatrix));
	ComPtr<ID3D12Resource> terrainWvpResource = directXBase->CreateBufferResource(sizeof(TransformationMatrix));

	// データを書き込む
	Matrix4x4* wvpData = nullptr;

	TransformationMatrix* transformationMatrixData = nullptr;

	// 書き込むためのアドレスを取得

	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
	TransformationMatrix* terrainTransformationData = nullptr;
	hr = terrainWvpResource->Map(0, nullptr, reinterpret_cast<void**>(&terrainTransformationData));
	assert(SUCCEEDED(hr));

	// 単位行列を書き込んでおく
	transformationMatrixData->WVP = MakeIdentity4x4();
	transformationMatrixData->World = MakeIdentity4x4();
	transformationMatrixData->WorldInverseTranspose = MakeIdentity4x4();
	terrainTransformationData->WVP = MakeIdentity4x4();
	terrainTransformationData->World = MakeIdentity4x4();
	terrainTransformationData->WorldInverseTranspose = MakeIdentity4x4();

	FKEngine::Particle transformSprite{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	// Sprite用のtransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ComPtr<ID3D12Resource> transformationMatrixResourceSprite = directXBase->CreateBufferResource(sizeof(TransformationMatrix));

	// データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	// 単位行列を書き込んでおく
	transformationMatrixDataSprite->WVP = MakeIdentity4x4();
	transformationMatrixDataSprite->World = MakeIdentity4x4();
	transformationMatrixDataSprite->WorldInverseTranspose = MakeIdentity4x4();

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
	directionalLightData->intensity = 0.0f;

	ComPtr<ID3D12Resource> pointLightResource = directXBase->CreateBufferResource(sizeof(PointLight));
	PointLight* pointLightData = nullptr;
	hr = pointLightResource->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData));
	assert(SUCCEEDED(hr));
	pointLightData->color = { 1.0f, 0.85f, 0.65f, 1.0f };
	pointLightData->position = { 0.0f, 2.0f, -3.0f };
	pointLightData->intensity = 0.0f;
	pointLightData->radius = 10.0f;
	pointLightData->decay = 2.0f;
	pointLightData->padding[0] = 0.0f;
	pointLightData->padding[1] = 0.0f;

	ComPtr<ID3D12Resource> spotLightResource = directXBase->CreateBufferResource(sizeof(SpotLight));
	SpotLight* spotLightData = nullptr;
	hr = spotLightResource->Map(0, nullptr, reinterpret_cast<void**>(&spotLightData));
	assert(SUCCEEDED(hr));
	spotLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	spotLightData->position = { 2.0f, 1.25f, 0.0f };
	spotLightData->intensity = 4.0f;
	Vector3 initialSpotDirection = { -1.0f, -1.0f, 0.0f };
	spotLightData->direction = Vector3::Normalize(initialSpotDirection);
	spotLightData->distance = 7.0f;
	spotLightData->decay = 2.0f;
	spotLightData->cosAngle = std::cos(std::numbers::pi_v<float> / 3.0f);
	spotLightData->cosFalloffStart = std::cos(std::numbers::pi_v<float> / 6.0f);
	spotLightData->padding[0] = 0.0f;

	// カメラのワールド位置を送るためのResource。Mapしたままにして、毎フレーム書き換える
	ComPtr<ID3D12Resource> cameraResource = directXBase->CreateBufferResource(sizeof(CameraForGPU));
	CameraForGPU* cameraData = nullptr;
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));
	cameraData->worldPosition = { 0.0f, 0.0f, 0.0f };
	materialDataSprite->enableLighting = 0;
	materialDataSprite->shininess = 40.0f;
	materialDataSprite->specularColor = { 1.0f, 1.0f, 1.0f };
	materialDataSprite->useBlinnPhong = 1;

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
	FKEngine::Particle transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,1.0f,0.0f} };

	FKEngine::Particle cameraTransform{ {1.0f,1.0f,1.0f},{0.38f,0.0f,0.0f},{0.0f,6.0f,-16.0f} };

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

	// Particle(球)用のTexture(monsterBall.png)
	DirectX::ScratchImage mipImages4 = DirectXBase::LoadTexture("Resources/monsterBall.png");
	const DirectX::TexMetadata& metadata4 = mipImages4.GetMetadata();
	ComPtr<ID3D12Resource> textureResource4 = directXBase->CreateTextureResource(metadata4);
	ComPtr<ID3D12Resource> intermediateResource4 = directXBase->UploadTextureData(textureResource4, mipImages4);

	// terrain.mtl の map_Kd で指定された草テクスチャ
	DirectX::ScratchImage terrainMipImages = DirectXBase::LoadTexture(terrainData.material.textureFilePath);
	const DirectX::TexMetadata& terrainMetadata = terrainMipImages.GetMetadata();
	ComPtr<ID3D12Resource> terrainTextureResource = directXBase->CreateTextureResource(terrainMetadata);
	ComPtr<ID3D12Resource> terrainTextureUploadResource = directXBase->UploadTextureData(
		terrainTextureResource, terrainMipImages);


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

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc4{};
	srvDesc4.Format = metadata4.format;
	srvDesc4.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc4.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc4.Texture2D.MipLevels = UINT(metadata4.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);

	// Heapの4番目にParticle用のcircle.pngのSRVを作成する（0:ImGui 1,2:Texture 3:Instancing 4:circle）
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);

	// Heapの6番目にParticle(球)用のmonsterBall.pngのSRVを作成する（5:monsterBall）
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU4 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 5);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU4 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 5);
	D3D12_CPU_DESCRIPTOR_HANDLE terrainTextureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 6);
	D3D12_GPU_DESCRIPTOR_HANDLE terrainTextureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 6);


	// SRVの生成
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);
	device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);
	device->CreateShaderResourceView(textureResource3.Get(), &srvDesc3, textureSrvHandleCPU3);
	device->CreateShaderResourceView(textureResource4.Get(), &srvDesc4, textureSrvHandleCPU4);
	D3D12_SHADER_RESOURCE_VIEW_DESC terrainSrvDesc{};
	terrainSrvDesc.Format = terrainMetadata.format;
	terrainSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	terrainSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	terrainSrvDesc.Texture2D.MipLevels = UINT(terrainMetadata.mipLevels);
	device->CreateShaderResourceView(terrainTextureResource.Get(), &terrainSrvDesc, terrainTextureSrvHandleCPU);

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

			ImGui::DragFloat2("テクスチャ位置", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("テクスチャ拡大率", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("テクスチャ回転", &uvTransformSprite.rotate.z);

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

			// ビュー行列の逆行列がカメラのワールド行列。その平行移動成分がカメラのワールド位置
			{
				Matrix4x4 cameraWorldMatrix = Inverse(viewMatrix);
				cameraData->worldPosition = { cameraWorldMatrix.m[3][0], cameraWorldMatrix.m[3][1], cameraWorldMatrix.m[3][2] };
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
			// 法線用。非均一スケールでも法線が面に対して垂直のままになる
			transformationMatrixData->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
			const Matrix4x4 terrainWorldMatrix = MakeIdentity4x4();
			terrainTransformationData->WVP = Multiply(terrainWorldMatrix, Multiply(viewMatrix, projectionMatrix));
			terrainTransformationData->World = terrainWorldMatrix;
			terrainTransformationData->WorldInverseTranspose = MakeIdentity4x4();

			// Instancing用のTransformationMatrixを更新する
			Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

			// BillboardMatrixを作る（全てのParticleで同じ行列を使える）
			Matrix4x4 billboardMatrix = MakeIdentity4x4(); // 利用しない場合は単位行列
			if (useBillboard)
			{
				// ビュー行列の逆行列がカメラのワールド行列（デバッグカメラ使用時も同じ式で求まる）
				Matrix4x4 cameraMatrix = Inverse(viewMatrix);
				// 板ポリの裏が見えているので、Y軸でπ回転させて表をカメラ側へ向ける
				Matrix4x4 backToFrontMatrix = MakeRotateYMatrix(std::numbers::pi_v<float>);
				billboardMatrix = Multiply(backToFrontMatrix, cameraMatrix);
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
					if (useAccelerationField && IsCollision(accelerationField.area, (*particleIterator).transform.translate))
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
					Matrix4x4 scaleMatrix = MakeScaleMatrix((*particleIterator).transform.scale);
					Matrix4x4 translateMatrix = MakeTranslateMatrix((*particleIterator).transform.translate);
					Matrix4x4 instanceWorldMatrix = Multiply(Multiply(scaleMatrix, billboardMatrix), translateMatrix);
					Matrix4x4 instanceWVPMatrix = Multiply(instanceWorldMatrix, viewProjectionMatrix);
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

			transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
			transformationMatrixDataSprite->World = worldMatrixSprite;
			transformationMatrixDataSprite->WorldInverseTranspose = Transpose(Inverse(worldMatrixSprite));

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
			commandList->SetGraphicsRootConstantBufferView(1, terrainWvpResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, terrainTextureSrvHandleGPU);
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
			// カメラとPointLightの定数バッファ
			commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());

			// 地形を描画する
			commandList->IASetVertexBuffers(0, 1, &terrainVertexBufferView);
			commandList->DrawInstanced(UINT(terrainData.vertices.size()), 1, 0, 0);

			// 以降は既存モデルとモンスターボール用の変換・テクスチャに戻す
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);

			/////////////////// 三角形用の描画
			//commandList->IASetVertexBuffers(0, 1, &vertexBufferView); //  球のVBVを設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewModel); // モデルのVBVを設定

			//commandList->DrawInstanced(kVertexCount, 1, 0, 0);
			if (!useInstancing)
			{
				commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
			}

			// モンスターボールを1個だけ描画する（Enable LightingやLight Directionなどが効く）
			if (showBall)
			{
				commandList->IASetVertexBuffers(0, 1, &vertexBufferView); // 球のVBV
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU4); // monsterBall.png
				commandList->DrawInstanced(kVertexCount, 1, 0, 0);
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

#ifdef USE_IMGUI
			// 実際のcommandListのImGuiの描画コマンドを頼む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif
			//////////////////////////////////////////////////////////////////////////////////////////


			directXBase->PostDraw();
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
