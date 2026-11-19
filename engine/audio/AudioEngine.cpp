#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace Kyumist {

namespace {

struct AudioState {
    ma_engine engine{};
    bool engineInitialized = false;
    bool deviceAvailable = false;
    uint64_t playedSoundCount = 0;
    std::filesystem::path assetsDir;
};

AudioState s_Audio;

std::filesystem::path ResolveAssetsRoot() {
    if (!s_Audio.assetsDir.empty()) {
        return s_Audio.assetsDir;
    }
#ifdef KM_ASSETS_DIR
    return std::filesystem::path(KM_ASSETS_DIR);
#else
    return std::filesystem::path("assets");
#endif
}

std::filesystem::path ResolveAudioPath(const std::string& path) {
    std::filesystem::path p(path);
    if (std::filesystem::exists(p)) {
        return p;
    }
    std::filesystem::path inAssets = ResolveAssetsRoot() / p;
    if (std::filesystem::exists(inAssets)) {
        return inAssets;
    }
    return {};
}

void WriteLittleEndian16(std::ofstream& out, uint16_t val) {
    const char bytes[2] = {
        static_cast<char>(val & 0xFF),
        static_cast<char>((val >> 8) & 0xFF)
    };
    out.write(bytes, 2);
}

void WriteLittleEndian32(std::ofstream& out, uint32_t val) {
    const char bytes[4] = {
        static_cast<char>(val & 0xFF),
        static_cast<char>((val >> 8) & 0xFF),
        static_cast<char>((val >> 16) & 0xFF),
        static_cast<char>((val >> 24) & 0xFF)
    };
    out.write(bytes, 4);
}

} // namespace

std::filesystem::path AudioEngine::EnsureDefaultBounceWav() {
    return EnsureBounceWavInDir(ResolveAssetsRoot());
}

std::filesystem::path AudioEngine::EnsureBounceWavInDir(const std::filesystem::path& assetsDir) {
    const std::filesystem::path wavPath = assetsDir / "audio" / "bounce.wav";
    if (std::filesystem::exists(wavPath)) {
        return wavPath;
    }

    std::error_code ec;
    std::filesystem::create_directories(wavPath.parent_path(), ec);

    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr uint16_t bitsPerSample = 16;
    constexpr float durationSec = 0.18f;
    const uint32_t numSamples = static_cast<uint32_t>(sampleRate * durationSec);
    const uint32_t dataSize = numSamples * numChannels * (bitsPerSample / 8);

    std::ofstream out(wavPath, std::ios::binary);
    if (!out.is_open()) {
        KM_CORE_WARN("Failed to create default bounce WAV at '{}'.", wavPath.string());
        return wavPath;
    }

    out.write("RIFF", 4);
    WriteLittleEndian32(out, 36 + dataSize);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    WriteLittleEndian32(out, 16);
    WriteLittleEndian16(out, 1);
    WriteLittleEndian16(out, numChannels);
    WriteLittleEndian32(out, sampleRate);
    WriteLittleEndian32(out, sampleRate * numChannels * (bitsPerSample / 8));
    WriteLittleEndian16(out, numChannels * (bitsPerSample / 8));
    WriteLittleEndian16(out, bitsPerSample);

    out.write("data", 4);
    WriteLittleEndian32(out, dataSize);

    constexpr float twoPi = 6.28318530718f;
    for (uint32_t i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float progress = static_cast<float>(i) / static_cast<float>(numSamples);
        const float freq = 660.0f + 220.0f * progress;
        const float env = std::exp(-progress * 8.0f);
        const float sampleFloat = std::sin(twoPi * freq * t) * env * 0.45f;
        const int16_t sample = static_cast<int16_t>(std::clamp(sampleFloat, -1.0f, 1.0f) * 32767.0f);
        WriteLittleEndian16(out, static_cast<uint16_t>(sample));
    }

    KM_CORE_INFO("Generated default WAV sound at '{}'.", wavPath.string());
    return wavPath;
}

void AudioEngine::Init(bool headless) {
    if (s_Audio.engineInitialized) {
        return;
    }

    EnsureDefaultBounceWav();
    s_Audio.playedSoundCount = 0;

    if (headless) {
        s_Audio.deviceAvailable = false;
        KM_CORE_INFO("AudioEngine initialized in silent headless mode.");
        return;
    }

    const ma_result res = ma_engine_init(nullptr, &s_Audio.engine);
    if (res == MA_SUCCESS) {
        s_Audio.engineInitialized = true;
        s_Audio.deviceAvailable = true;
        KM_CORE_INFO("AudioEngine initialized (miniaudio active).");
    } else {
        s_Audio.engineInitialized = false;
        s_Audio.deviceAvailable = false;
        KM_CORE_WARN("AudioEngine could not open audio device (code {}), falling back to silent mode.",
                     static_cast<int>(res));
    }
}

void AudioEngine::Shutdown() {
    if (s_Audio.engineInitialized) {
        ma_engine_uninit(&s_Audio.engine);
        s_Audio.engineInitialized = false;
    }
    s_Audio.deviceAvailable = false;
}

void AudioEngine::SetAssetsDirectory(const std::filesystem::path& assetsDir) {
    s_Audio.assetsDir = assetsDir;
}

void AudioEngine::PlaySound(const std::string& path, float volume) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path resolved = ResolveAudioPath(path);
    if (resolved.empty()) {
        KM_CORE_WARN("Audio file not found: '{}'.", path);
        return;
    }

    ++s_Audio.playedSoundCount;

    if (s_Audio.deviceAvailable && s_Audio.engineInitialized) {
        ma_engine_set_volume(&s_Audio.engine, std::clamp(volume, 0.0f, 2.0f));
        const ma_result res = ma_engine_play_sound(&s_Audio.engine, resolved.string().c_str(), nullptr);
        if (res != MA_SUCCESS) {
            KM_CORE_WARN("miniaudio failed to play '{}' (code {}).", resolved.string(), static_cast<int>(res));
        }
    }
}

bool AudioEngine::IsDeviceAvailable() {
    return s_Audio.deviceAvailable;
}

uint64_t AudioEngine::GetPlayedSoundCount() {
    return s_Audio.playedSoundCount;
}

} // namespace Kyumist

