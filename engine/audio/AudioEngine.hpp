#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Kyumist {

class AudioEngine {
public:
    static void Init(bool headless = false);
    static void Shutdown();

    static void SetAssetsDirectory(const std::filesystem::path& assetsDir);
    static void PlaySound(const std::string& path, float volume = 1.0f);
    static bool IsDeviceAvailable();
    static uint64_t GetPlayedSoundCount();
    static std::filesystem::path EnsureDefaultBounceWav();
    static std::filesystem::path EnsureBounceWavInDir(const std::filesystem::path& assetsDir);
};

} // namespace Kyumist
