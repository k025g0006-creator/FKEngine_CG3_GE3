#pragma once
#include <xaudio2.h>
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <string>

// チャンクヘッダ
struct ChunkHeader
{
	char id[4];   // チャンク毎のID
	int32_t size; // チャンクサイズ
};

// RIFFヘッダチャンク
struct RiffHeader
{
	ChunkHeader chunk;   // "RIFF"
	char type[4];        // "WAVE"
};

// FMTチャンク
struct FormatChunk
{
	ChunkHeader chunk;  // "fmt"
	WAVEFORMATEX fmt;   // 波形フォーマット
};

struct AudioVoiceDeleter
{
	void operator()(IXAudio2Voice* voice) const { voice->DestroyVoice(); }
};

class Sound
{
public:
	Sound() = default;
	// 音声データと再生ボイスはこのインスタンスが所有する。
	Sound(const Sound&) = delete;
	Sound& operator=(const Sound&) = delete;

	// 音声データの読み込み
	void Load(const std::string& filename);

	// 音声の再生
	// loop : trueでループ再生
	void Play(IXAudio2* xAudio2, bool loop = false);

	// 停止
	void Stop();

	// 音声データの解放
	void Unload();

private:
	// 波形フォーマット
	WAVEFORMATEX wfex_{};

	// バッファの先頭アドレス
	std::unique_ptr<BYTE[]> buffer_;

	// バッファのサイズ
	unsigned int bufferSize_ = 0;

	// 再生に使用しているSourceVoice（Stop用に保持）
	// 逆順に破棄されるので、ボイスはバッファより先に解放される。
	std::unique_ptr<IXAudio2SourceVoice, AudioVoiceDeleter> sourceVoice_;
};
