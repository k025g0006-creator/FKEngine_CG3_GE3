#include "Sound.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <cstring>
#include <new>
#include <utility>

namespace
{
using Microsoft::WRL::ComPtr;
using WaveFormat = std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)>;
constexpr auto kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr auto kFirstAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

HRESULT ConfigureAudioStream(IMFSourceReader& reader)
{
    HRESULT result = reader.SetStreamSelection(kAllStreams, FALSE);
    if (FAILED(result)) { return result; }
    result = reader.SetStreamSelection(kFirstAudioStream, TRUE);
    if (FAILED(result)) { return result; }

    ComPtr<IMFMediaType> nativeType;
    result = reader.GetNativeMediaType(kFirstAudioStream, 0, &nativeType);
    if (FAILED(result)) { return result; }
    GUID subtype{};
    result = nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(result)) { return result; }

    if (subtype == MFAudioFormat_PCM || subtype == MFAudioFormat_Float)
    {
        return reader.SetCurrentMediaType(kFirstAudioStream, nullptr, nativeType.Get());
    }

    ComPtr<IMFMediaType> decodedType;
    result = MFCreateMediaType(&decodedType);
    if (FAILED(result)) { return result; }
    result = decodedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (FAILED(result)) { return result; }

    // 圧縮音声はデコーダーが対応する場合に32-bit floatで受け取る。
    for (const auto& outputSubtype : {MFAudioFormat_Float, MFAudioFormat_PCM})
    {
        result = decodedType->SetGUID(MF_MT_SUBTYPE, outputSubtype);
        if (FAILED(result)) { return result; }
        result = reader.SetCurrentMediaType(kFirstAudioStream, nullptr, decodedType.Get());
        if (SUCCEEDED(result)) { return result; }
    }
    return result;
}

HRESULT ReadWaveFormat(IMFSourceReader& reader, WaveFormat& format)
{
    ComPtr<IMFMediaType> mediaType;
    HRESULT result = reader.GetCurrentMediaType(kFirstAudioStream, &mediaType);
    if (FAILED(result)) { return result; }

    WAVEFORMATEX* wave = nullptr;
    UINT32 formatSize = 0;
    result = MFCreateWaveFormatExFromMFMediaType(mediaType.Get(), &wave, &formatSize, MFWaveFormatExConvertFlag_Normal);
    if (FAILED(result)) { return result; }
    format.reset(wave);
    if (formatSize < sizeof(WAVEFORMATEX) || wave->nBlockAlign == 0 ||
        formatSize - sizeof(WAVEFORMATEX) < wave->cbSize)
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    return S_OK;
}

HRESULT AppendSample(IMFSample& sample, std::vector<BYTE>& pcm)
{
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT result = sample.ConvertToContiguousBuffer(&buffer);
    if (FAILED(result)) { return result; }
    DWORD length = 0;
    result = buffer->GetCurrentLength(&length);
    if (FAILED(result) || length == 0) { return result; }
    const auto offset = pcm.size();
    if (length > XAUDIO2_MAX_BUFFER_BYTES - offset) { return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE); }
    try
    {
        pcm.resize(offset + length);
    } catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    BYTE* data = nullptr;
    result = buffer->Lock(&data, nullptr, nullptr);
    if (FAILED(result)) { return result; }
    std::memcpy(pcm.data() + offset, data, length);
    return buffer->Unlock();
}
}

HRESULT Sound::Load(const std::filesystem::path& filename)
{
    if (filename.empty()) { return E_INVALIDARG; }
    ComPtr<IMFSourceReader> reader;
    HRESULT result = MFCreateSourceReaderFromURL(filename.c_str(), nullptr, &reader);
    if (FAILED(result)) { return result; }
    result = ConfigureAudioStream(*reader.Get());
    if (FAILED(result)) { return result; }

    WaveFormat format{nullptr, &CoTaskMemFree};
    result = ReadWaveFormat(*reader.Get(), format);
    if (FAILED(result)) { return result; }

    std::vector<BYTE> pcm;
    DWORD flags = 0;
    do
    {
        ComPtr<IMFSample> sample;
        result = reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(result)) { return result; }
        if (flags & MF_SOURCE_READERF_ERROR) { return E_FAIL; }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            if (!pcm.empty()) { return MF_E_INVALIDMEDIATYPE; }
            result = ReadWaveFormat(*reader.Get(), format);
            if (FAILED(result)) { return result; }
        }
        if (sample)
        {
            result = AppendSample(*sample.Get(), pcm);
            if (FAILED(result)) { return result; }
        }
    } while (!(flags & MF_SOURCE_READERF_ENDOFSTREAM));

    if (pcm.empty() || pcm.size() % format->nBlockAlign != 0) { return MF_E_INVALIDMEDIATYPE; }
    Stop();
    pcm_ = std::move(pcm);
    format_ = std::move(format);
    return S_OK;
}

HRESULT Sound::Play(IXAudio2* xAudio2, bool loop)
{
    const HRESULT result = loop ? PlayBgm(xAudio2) : PlaySe(xAudio2);
    if (SUCCEEDED(result))
    {
        if (loop)
        { 
            StopSe();
        }
        else
        { 
            StopBgm();
        }
    }
    return result;
}

HRESULT Sound::PlayBgm(IXAudio2* xAudio2)
{
    return StartPlayback(xAudio2, true, bgmVoice_);
}

HRESULT Sound::PlaySe(IXAudio2* xAudio2)
{
    return StartPlayback(xAudio2, false, seVoice_);
}

HRESULT Sound::StartPlayback(IXAudio2* xAudio2, bool loop, Voice& voice)
{
    if (!xAudio2) { return E_POINTER; }
    if (pcm_.empty() || !format_) { return MF_E_INVALIDREQUEST; }

    IXAudio2SourceVoice* createdVoice = nullptr;
    HRESULT result = xAudio2->CreateSourceVoice(&createdVoice, format_.get());
    if (FAILED(result)) { return result; }
    Voice nextVoice{createdVoice};

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = pcm_.data();
    buffer.AudioBytes = static_cast<UINT32>(pcm_.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
    result = nextVoice->SubmitSourceBuffer(&buffer);
    if (FAILED(result)) { return result; }
    voice.reset();
    result = nextVoice->Start();
    if (SUCCEEDED(result)) { voice = std::move(nextVoice); }
    return result;
}

void Sound::StopBgm()
{
    bgmVoice_.reset();
}

void Sound::StopSe()
{
    seVoice_.reset();
}

void Sound::Stop()
{
    StopSe();
    StopBgm();
}

void Sound::Unload()
{
    Stop();
    pcm_ = std::vector<BYTE>{};
    format_.reset();
}
