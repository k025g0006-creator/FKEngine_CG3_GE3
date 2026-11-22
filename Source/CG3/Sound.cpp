#include "Sound.h"

#include <cstring>
#include <fstream>
#include <utility>

void Sound::Load(const std::string& filename)
{
	std::ifstream file(filename, std::ios_base::binary);

	RiffHeader riff{};
	if (!file.read(reinterpret_cast<char*>(&riff), sizeof(riff)) ||
		std::memcmp(riff.chunk.id, "RIFF", 4) != 0 ||
		std::memcmp(riff.type, "WAVE", 4) != 0)
	{
		return;
	}

	constexpr int32_t kPcmFormatSize = 16;
	FormatChunk format{};
	if (!file.read(reinterpret_cast<char*>(&format.chunk), sizeof(format.chunk)) ||
		std::memcmp(format.chunk.id, "fmt ", 4) != 0 ||
		format.chunk.size < kPcmFormatSize ||
		format.chunk.size > static_cast<int32_t>(sizeof(format.fmt)) ||
		!file.read(reinterpret_cast<char*>(&format.fmt), format.chunk.size) ||
		format.fmt.cbSize != 0)
	{
		return;
	}

	ChunkHeader data{};
	if (!file.read(reinterpret_cast<char*>(&data), sizeof(data))) { return; }
	if (std::memcmp(data.id, "JUNK", 4) == 0)
	{
		if (data.size < 0) { return; }
		const auto paddedSize = static_cast<std::streamoff>(data.size) + (data.size & 1);
		if (!file.seekg(paddedSize, std::ios_base::cur) ||
			!file.read(reinterpret_cast<char*>(&data), sizeof(data)))
		{
			return;
		}
	}
	if (std::memcmp(data.id, "data", 4) != 0 || data.size <= 0 ||
		static_cast<unsigned int>(data.size) > XAUDIO2_MAX_BUFFER_BYTES)
	{
		return;
	}

	const auto dataStart = file.tellg();
	if (dataStart < 0 || !file.seekg(0, std::ios_base::end) ||
		file.tellg() - dataStart < data.size || !file.seekg(dataStart))
	{
		return;
	}
	auto buffer = std::make_unique<BYTE[]>(static_cast<std::size_t>(data.size));
	if (!file.read(reinterpret_cast<char*>(buffer.get()), data.size)) { return; }

	// 再生中のボイスを破棄してから、そのボイスが参照するバッファを置き換える。
	Stop();
	wfex_ = format.fmt;
	buffer_ = std::move(buffer);
	bufferSize_ = static_cast<unsigned int>(data.size);
}

void Sound::Play(IXAudio2* xAudio2, bool loop)
{
	if (!xAudio2 || !buffer_) { return; }

	IXAudio2SourceVoice* createdVoice = nullptr;
	if (FAILED(xAudio2->CreateSourceVoice(&createdVoice, &wfex_))) { return; }
	std::unique_ptr<IXAudio2SourceVoice, AudioVoiceDeleter> voice{createdVoice};

	XAUDIO2_BUFFER buffer{};
	buffer.pAudioData = buffer_.get();
	buffer.AudioBytes = bufferSize_;
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	if (loop)
	{
		buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
	}

	if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start())) { return; }
	sourceVoice_ = std::move(voice);
}

void Sound::Stop()
{
	sourceVoice_.reset();
}

void Sound::Unload()
{
	Stop();
	buffer_.reset();
	bufferSize_ = 0;
	wfex_ = {};
}
