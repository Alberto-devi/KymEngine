#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Kyumist::PlatformPaths {

#ifdef __ANDROID__
namespace detail {
inline std::filesystem::path& AndroidDataDirStorage() {
    static std::filesystem::path s_AndroidDataDir;
    return s_AndroidDataDir;
}
} // namespace detail

inline void SetAndroidDataDir(const std::filesystem::path& dir) {
    detail::AndroidDataDirStorage() = dir;
}

inline const std::filesystem::path& GetAndroidDataDir() {
    return detail::AndroidDataDirStorage();
}
#endif

inline std::filesystem::path GetExecutableDir() {
    std::error_code ec;
#ifdef __ANDROID__
    if (!GetAndroidDataDir().empty()) {
        return GetAndroidDataDir();
    }
#endif
#ifdef _WIN32
    char buf[4096]{};
    const DWORD len = ::GetModuleFileNameA(nullptr, buf, static_cast<DWORD>(sizeof(buf) - 1));
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#else
    char buf[4096]{};
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#endif
    return std::filesystem::current_path(ec);
}

inline std::filesystem::path GetUserDataDir() {
    std::error_code ec;
#ifdef __ANDROID__
    if (!GetAndroidDataDir().empty()) {
        return GetAndroidDataDir();
    }
#endif
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        if (*localAppData != '\0') {
            return std::filesystem::path(localAppData) / "KymEngine";
        }
    }
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        if (*userProfile != '\0') {
            return std::filesystem::path(userProfile) / "KymEngine";
        }
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        if (*xdg != '\0') {
            return std::filesystem::path(xdg) / "KymEngine";
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') {
            return std::filesystem::path(home) / ".local" / "share" / "KymEngine";
        }
    }
#endif
    return std::filesystem::temp_directory_path(ec) / "KymEngine";
}

inline bool IsDirectoryWritable(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return false;
    }
    const std::filesystem::path probe = dir / ".kym_write_probe";
    std::ofstream out(probe, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.close();
    std::filesystem::remove(probe, ec);
    return true;
}

inline std::filesystem::path ResolveAssetsDir() {
    std::error_code ec;
#ifdef __ANDROID__
    if (!GetAndroidDataDir().empty() && std::filesystem::exists(GetAndroidDataDir(), ec)) {
        return GetAndroidDataDir();
    }
#endif
    const std::filesystem::path exeDir = GetExecutableDir();
    const std::filesystem::path candidates[] = {
        exeDir / "assets",
        exeDir / ".." / "assets",
        exeDir / ".." / "share" / "kymengine" / "assets",
        std::filesystem::path("assets")
#ifdef KM_ASSETS_DIR
        , std::filesystem::path(KM_ASSETS_DIR)
#endif
    };
    for (const auto& c : candidates) {
        const std::filesystem::path norm = std::filesystem::weakly_canonical(c, ec);
        if (!ec && std::filesystem::exists(norm, ec) && std::filesystem::is_directory(norm, ec)) {
            return norm;
        }
    }
#ifdef KM_ASSETS_DIR
    return std::filesystem::path(KM_ASSETS_DIR);
#else
    return std::filesystem::path("assets");
#endif
}

inline std::filesystem::path ResolveShadersDir() {
    std::error_code ec;
#ifdef __ANDROID__
    if (!GetAndroidDataDir().empty()) {
        const std::filesystem::path p1 = GetAndroidDataDir() / "shaders";
        if (std::filesystem::exists(p1, ec) && std::filesystem::is_directory(p1, ec)) {
            return p1;
        }
        const std::filesystem::path p2 = GetAndroidDataDir() / "assets" / "shaders";
        if (std::filesystem::exists(p2, ec) && std::filesystem::is_directory(p2, ec)) {
            return p2;
        }
    }
#endif
    const std::filesystem::path exeDir = GetExecutableDir();
    const std::filesystem::path candidates[] = {
        exeDir / "assets" / "shaders",
        exeDir / "shaders",
        exeDir / ".." / "share" / "kymengine" / "assets" / "shaders",
        std::filesystem::path("assets") / "shaders",
        std::filesystem::path("shaders")
#ifdef KM_SHADER_DIR
        , std::filesystem::path(KM_SHADER_DIR)
#endif
    };
    for (const auto& c : candidates) {
        const std::filesystem::path norm = std::filesystem::weakly_canonical(c, ec);
        if (!ec && std::filesystem::exists(norm, ec) && std::filesystem::is_directory(norm, ec)) {
            return norm;
        }
    }
#ifdef KM_SHADER_DIR
    return std::filesystem::path(KM_SHADER_DIR);
#else
    return std::filesystem::path("assets/shaders");
#endif
}

inline std::filesystem::path ResolveJavaApiJar() {
    std::error_code ec;
    const std::filesystem::path exeDir = GetExecutableDir();
    const std::filesystem::path candidates[] = {
        exeDir / "java" / "kyumist-api.jar",
        exeDir / ".." / "java" / "kyumist-api.jar",
        exeDir / ".." / "share" / "kymengine" / "java" / "kyumist-api.jar",
        std::filesystem::path("java") / "kyumist-api.jar",
        std::filesystem::path("build") / "java" / "kyumist-api.jar"
#ifdef KM_JAVA_API_JAR
        , std::filesystem::path(KM_JAVA_API_JAR)
#endif
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
#ifdef KM_JAVA_API_JAR
    return std::filesystem::path(KM_JAVA_API_JAR);
#else
    return std::filesystem::path("build/java/kyumist-api.jar");
#endif
}

inline std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

inline std::filesystem::path Utf8ToPath(const std::string& utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size())
    );
}

} // namespace Kyumist::PlatformPaths

