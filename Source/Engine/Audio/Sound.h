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
    // 同じ役割の音声を再生し直す。低音ブーストなしで再生できた場合は S_FALSE。
    HRESULT PlayBgm(IXAudio2* xAudio2);
    HRESULT PlaySe(IXAudio2* xAudio2);
    static constexpr float kMaxBassBoostDb = 12.0f;
    // loop が true なら BGM、false なら SE。再生前にも設定できる。
    HRESULT SetBassBoostDb(float gainDb, bool loop);
    float GetBassBoostDb(bool loop) const;
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

    struct Playback
    {
        Voice voice;
        float bassBoostDb = 0.0f;
        bool hasBassEffect = false;
    };

    HRESULT StartPlayback(IXAudio2* xAudio2, bool loop, Playback& playback);

    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format_{nullptr, &CoTaskMemFree};
    std::vector<BYTE> pcm_;
    Playback bgm_;
    Playback se_;
};
