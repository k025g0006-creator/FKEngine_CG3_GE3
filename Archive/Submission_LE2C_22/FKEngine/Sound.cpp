#include "Sound.h"

#include <fstream>
#include <cassert>
#include <cstring>

Sound::~Sound()
{
	Unload();
}

void Sound::Load(const std::string& filename)
{
	// 二重ロード対策
	if (pBuffer_)
	{
		Unload();
	}

	// ファイル入力ストリームのインスタンス
	std::ifstream file;

	// .wavファイルをバイナリモードで開く
	file.open(filename, std::ios_base::binary);

	// ファイルオープン失敗を抽出する
	assert(file.is_open());

	// RIFFヘッダーの読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));

	// ファイルがRIFFかチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
	{
		assert(0);
	}

	// タイプがWAVEかチェック
	if (strncmp(riff.type, "WAVE", 4) != 0)
	{
		assert(0);
	}

	// formatチャンクの読み込み
	FormatChunk format = {};

	// チャンクヘッダーの確認
	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0)
	{
		assert(0);
	}

	// チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read((char*)&format.fmt, format.chunk.size);

	// Dataチャンクの読み込み
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));

	// JUNKチャンクを検出した場合
	if (strncmp(data.id, "JUNK", 4) == 0)
	{
		// 読み取り位置をJUNKチャンクの終わりまで進める
		file.seekg(data.size, std::ios_base::cur);

		// 再読み込み
		file.read((char*)&data, sizeof(data));
	}

	if (strncmp(data.id, "data", 4) != 0)
	{
		assert(0);
	}

	// Dataチャンクのデータ部（波形データ）の読み込み
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	// waveファイルを閉じる
	file.close();

	// メンバへ格納
	wfex_ = format.fmt;
	pBuffer_ = reinterpret_cast<BYTE*>(pBuffer);
	bufferSize_ = data.size;
}

void Sound::Play(IXAudio2* xAudio2, bool loop)
{
	assert(pBuffer_ && "Sound::Load() を呼び出してから再生してください");

	HRESULT result;

	// 波形フォーマットを元にSourceVoiceを生成
	result = xAudio2->CreateSourceVoice(&pSourceVoice_, &wfex_);
	assert(SUCCEEDED(result));

	// 再生する波形データの設定
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = pBuffer_;
	buf.AudioBytes = bufferSize_;
	buf.Flags = XAUDIO2_END_OF_STREAM;
	if (loop)
	{
		buf.LoopCount = XAUDIO2_LOOP_INFINITE;
	}

	// 波形データの再生
	result = pSourceVoice_->SubmitSourceBuffer(&buf);
	assert(SUCCEEDED(result));
	result = pSourceVoice_->Start();
	assert(SUCCEEDED(result));
}

void Sound::Stop()
{
	if (pSourceVoice_)
	{
		pSourceVoice_->Stop();
		pSourceVoice_->DestroyVoice();
		pSourceVoice_ = nullptr;
	}
}

void Sound::Unload()
{
	Stop();

	// バッファのメモリを解放
	delete[] pBuffer_;

	pBuffer_ = nullptr;
	bufferSize_ = 0;
	wfex_ = {};
}
