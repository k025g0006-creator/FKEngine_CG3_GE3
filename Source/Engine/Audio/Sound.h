#pragma once

#include <xaudio2.h>
#include <combaseapi.h>
#include <filesystem>
#include <memory>
#include <vector>

class Sound
{
public:
    Sound() = default;
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;

    // COM・Media Foundationの初期化後に呼ぶ。読み込みに失敗した場合は以前の音声を保持する。
    HRESULT Load(const std::filesystem::path& filename);
    HRESULT Play(IXAudio2* xAudio2, bool loop = false);
    // 同じ役割の音声を再生し直す。BGMとSEは個別に停止できる。
    HRESULT PlayBgm(IXAudio2* xAudio2);
    HRESULT PlaySe(IXAudio2* xAudio2);
    void StopBgm();
    void StopSe();
    void Stop();
    void Unload();
    const WAVEFORMATEX* GetFormat() const { return format_.get(); }

private:
    struct VoiceDeleter
    {
        void operator()(IXAudio2SourceVoice* voice) const { voice->DestroyVoice(); }
    };
    using Voice = std::unique_ptr<IXAudio2SourceVoice, VoiceDeleter>;

    HRESULT StartPlayback(IXAudio2* xAudio2, bool loop, Voice& voice);

    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format_{nullptr, &CoTaskMemFree};
    std::vector<BYTE> pcm_;
    Voice bgmVoice_;
    Voice seVoice_;
};
