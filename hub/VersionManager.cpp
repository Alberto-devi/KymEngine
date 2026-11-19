#include "VersionManager.hpp"

#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "project/ProjectManager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Kyumist::Hub {

namespace {

constexpr const char* kGitHubReleasesApiUrl =
    "https://api.github.com/repos/Alberto-devi/KymEngine/releases?per_page=30";

#ifdef _WIN32
constexpr const char* kEditorExeName = "KyumistEditor.exe";
constexpr const char* kRuntimeExeName = "KyumistRuntime.exe";
constexpr const char* kAssetSuffix = "windows-x64.zip";
#else
constexpr const char* kEditorExeName = "KyumistEditor";
constexpr const char* kRuntimeExeName = "KyumistRuntime";
constexpr const char* kAssetSuffix = "linux-x64.tar.gz";
#endif

bool EndsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) {
        return false;
    }
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string NormalizeVersionTag(std::string tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
        tag.erase(0, 1);
    }
    return tag;
}

std::filesystem::path FindEditorExecutableInDir(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return {};
    }

    const std::filesystem::path direct = dir / kEditorExeName;
    if (std::filesystem::exists(direct, ec) && std::filesystem::is_regular_file(direct, ec)) {
        return direct;
    }

    const std::filesystem::path inBin = dir / "bin" / kEditorExeName;
    if (std::filesystem::exists(inBin, ec) && std::filesystem::is_regular_file(inBin, ec)) {
        return inBin;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::filesystem::path nested = entry.path() / kEditorExeName;
        if (std::filesystem::exists(nested, ec) && std::filesystem::is_regular_file(nested, ec)) {
            return nested;
        }
        const std::filesystem::path nestedBin = entry.path() / "bin" / kEditorExeName;
        if (std::filesystem::exists(nestedBin, ec) && std::filesystem::is_regular_file(nestedBin, ec)) {
            return nestedBin;
        }
    }
    return {};
}

int RunBlockingCommand(const std::string& commandLine) {
#ifdef _WIN32
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = commandLine;
    const BOOL ok = ::CreateProcessA(
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );
    if (!ok) {
        return -1;
    }
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
#else
    const int status = std::system(commandLine.c_str());
    if (status == -1) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
#endif
}

std::string QuoteArg(const std::string& arg) {
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

std::vector<RemoteRelease> ParseReleasesJson(const std::filesystem::path& jsonFile) {
    std::vector<RemoteRelease> result;
    std::ifstream in(jsonFile);
    if (!in.is_open()) {
        return result;
    }

    try {
        nlohmann::json root;
        in >> root;
        if (!root.is_array()) {
            return result;
        }

        for (const auto& item : root) {
            if (item.value("draft", false)) {
                continue;
            }
            RemoteRelease rel;
            rel.tagName = item.value("tag_name", "");
            rel.version = NormalizeVersionTag(rel.tagName);
            rel.title = item.value("name", rel.tagName);
            rel.publishedAt = item.value("published_at", "");
            if (rel.publishedAt.size() >= 10) {
                rel.publishedAt = rel.publishedAt.substr(0, 10);
            }
            rel.prerelease = item.value("prerelease", false);

            if (item.contains("assets") && item["assets"].is_array()) {
#ifndef _WIN32
                const bool runningAppImage = (std::getenv("APPIMAGE") != nullptr);
                std::string fallbackInstallerName;
                std::string fallbackInstallerUrl;
                uint64_t fallbackInstallerSize = 0;
#endif
                for (const auto& asset : item["assets"]) {
                    const std::string name = asset.value("name", "");
                    const std::string url = asset.value("browser_download_url", "");
                    const uint64_t sz = asset.value("size", static_cast<uint64_t>(0));
                    if (EndsWith(name, kAssetSuffix)) {
                        rel.assetName = name;
                        rel.downloadUrl = url;
                        rel.assetSizeBytes = sz;
                    }
#ifdef _WIN32
                    if (EndsWith(name, "win64-setup.exe")) {
                        rel.hubInstallerName = name;
                        rel.hubInstallerUrl = url;
                        rel.hubInstallerSizeBytes = sz;
                    }
#else
                    if (runningAppImage && EndsWith(name, ".AppImage")) {
                        rel.hubInstallerName = name;
                        rel.hubInstallerUrl = url;
                        rel.hubInstallerSizeBytes = sz;
                    } else if (!runningAppImage && EndsWith(name, "_amd64.deb")) {
                        rel.hubInstallerName = name;
                        rel.hubInstallerUrl = url;
                        rel.hubInstallerSizeBytes = sz;
                    } else if (EndsWith(name, ".AppImage") || EndsWith(name, "_amd64.deb")) {
                        fallbackInstallerName = name;
                        fallbackInstallerUrl = url;
                        fallbackInstallerSize = sz;
                    }
#endif
                }
#ifndef _WIN32
                if (rel.hubInstallerUrl.empty() && !fallbackInstallerUrl.empty()) {
                    rel.hubInstallerName = std::move(fallbackInstallerName);
                    rel.hubInstallerUrl = std::move(fallbackInstallerUrl);
                    rel.hubInstallerSizeBytes = fallbackInstallerSize;
                }
#endif
            }

            if (!rel.version.empty() && !rel.downloadUrl.empty()) {
                result.push_back(std::move(rel));
            }
        }
    } catch (const std::exception& ex) {
        KM_WARN("Hub: failed to parse releases JSON: {}", ex.what());
    }

    return result;
}

void EnsureExecutablePermissions(const std::filesystem::path& rootDir) {
#ifndef _WIN32
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string fname = entry.path().filename().string();
        if (fname == kEditorExeName || fname == kRuntimeExeName) {
            ::chmod(entry.path().c_str(), 0755);
        }
    }
#else
    (void)rootDir;
#endif
}

} // namespace

std::filesystem::path VersionManager::GetEditorsRootDir() {
    return PlatformPaths::GetUserDataDir() / "editors";
}

std::filesystem::path VersionManager::GetProjectsRegistryFile() {
    return PlatformPaths::GetUserDataDir() / "kyumist_projects.json";
}

std::vector<InstalledEditor> VersionManager::ScanInstalledEditors() {
    std::vector<InstalledEditor> editors;
    std::error_code ec;

    const std::filesystem::path root = GetEditorsRootDir();
    std::filesystem::create_directories(root, ec);

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string dirName = entry.path().filename().string();
        if (!dirName.empty() && dirName[0] == '.') {
            continue;
        }

        const std::filesystem::path exePath = FindEditorExecutableInDir(entry.path());
        if (exePath.empty()) {
            continue;
        }

        InstalledEditor ed;
        ed.version = dirName;
        ed.rootDir = entry.path();
        ed.executablePath = exePath;
        ed.channel = ReleaseChannel::Stable;

        const std::filesystem::path metaPath = entry.path() / "kym_install.json";
        if (std::filesystem::exists(metaPath, ec)) {
            std::ifstream in(metaPath);
            if (in.is_open()) {
                try {
                    nlohmann::json meta;
                    in >> meta;
                    ed.version = meta.value("version", dirName);
                    const std::string ch = meta.value("channel", "stable");
                    if (ch == "prerelease") {
                        ed.channel = ReleaseChannel::Prerelease;
                    } else if (ch == "local") {
                        ed.channel = ReleaseChannel::Local;
                    }
                } catch (const std::exception&) {
                }
            }
        }
        editors.push_back(std::move(ed));
    }

    std::sort(editors.begin(), editors.end(), [](const InstalledEditor& a, const InstalledEditor& b) {
        return a.version > b.version;
    });

    // Also detect bundled/local editor next to Hub executable (or in dev build/editor)
    const std::filesystem::path hubDir = PlatformPaths::GetExecutableDir();
    const std::filesystem::path bundledCandidates[] = {
        hubDir / kEditorExeName,
        hubDir.parent_path() / "editor" / kEditorExeName
    };
    for (const auto& cand : bundledCandidates) {
        if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec)) {
            const std::string bundledVer = KM_ENGINE_VERSION;
            if (!FindInstalledVersion(editors, bundledVer)) {
                InstalledEditor ed;
                ed.version = bundledVer;
                ed.channel = ReleaseChannel::Local;
                ed.rootDir = cand.parent_path();
                ed.executablePath = std::filesystem::weakly_canonical(cand, ec);
                if (ec) {
                    ed.executablePath = cand;
                }
                ed.isBundled = true;
                editors.push_back(std::move(ed));
            }
            break;
        }
    }

    return editors;
}

const InstalledEditor* VersionManager::FindInstalledVersion(
    const std::vector<InstalledEditor>& editors,
    const std::string& version
) {
    for (const auto& ed : editors) {
        if (ed.version == version) {
            return &ed;
        }
    }
    return nullptr;
}

std::vector<RemoteRelease> VersionManager::FetchRemoteReleases(std::string* outError) {
    std::error_code ec;
    const std::filesystem::path root = GetEditorsRootDir();
    std::filesystem::create_directories(root, ec);

    const std::filesystem::path tmpFile = root / ".releases_tmp.json";
    const std::filesystem::path cacheFile = root / "releases_cache.json";

    const std::string cmd =
        "curl -fsSL --connect-timeout 10 --max-time 25 "
        "-H \"Accept: application/vnd.github+json\" "
        "-H \"User-Agent: KymEngineHub\" "
        "-o " + QuoteArg(tmpFile.string()) + " " + QuoteArg(kGitHubReleasesApiUrl);

    const int rc = RunBlockingCommand(cmd);
    if (rc == 0 && std::filesystem::exists(tmpFile, ec)) {
        std::filesystem::copy_file(tmpFile, cacheFile, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmpFile, ec);
        return ParseReleasesJson(cacheFile);
    }

    std::filesystem::remove(tmpFile, ec);
    if (std::filesystem::exists(cacheFile, ec)) {
        if (outError) {
            *outError = "Sin conexion con GitHub; mostrando cache local.";
        }
        return ParseReleasesJson(cacheFile);
    }

    if (outError) {
        *outError = "No se pudo consultar GitHub Releases (codigo " + std::to_string(rc) + ").";
    }
    return {};
}

bool VersionManager::InstallRelease(
    const RemoteRelease& release,
    std::string* outStatus,
    std::string* outError
) {
    if (release.version.empty() || release.downloadUrl.empty()) {
        if (outError) {
            *outError = "Datos de release invalidos.";
        }
        return false;
    }

    std::error_code ec;
    const std::filesystem::path root = GetEditorsRootDir();
    const std::filesystem::path downloadsDir = root / ".downloads";
    const std::filesystem::path targetDir = root / release.version;
    std::filesystem::create_directories(downloadsDir, ec);

    const std::filesystem::path archivePath = downloadsDir / release.assetName;

    if (outStatus) {
        *outStatus = "Descargando KymEngine " + release.version + "...";
    }

    const std::string dlCmd =
        "curl -fL --retry 2 --connect-timeout 15 "
        "-H \"User-Agent: KymEngineHub\" "
        "-o " + QuoteArg(archivePath.string()) + " " + QuoteArg(release.downloadUrl);

    const int dlRc = RunBlockingCommand(dlCmd);
    if (dlRc != 0 || !std::filesystem::exists(archivePath, ec)) {
        std::filesystem::remove(archivePath, ec);
        if (outError) {
            *outError = "Error al descargar " + release.assetName + " (codigo " + std::to_string(dlRc) + ").";
        }
        return false;
    }

    if (outStatus) {
        *outStatus = "Extrayendo KymEngine " + release.version + "...";
    }

    std::filesystem::remove_all(targetDir, ec);
    std::filesystem::create_directories(targetDir, ec);

#ifdef _WIN32
    const std::string extractCmd =
        "tar -xf " + QuoteArg(archivePath.string()) + " -C " + QuoteArg(targetDir.string());
#else
    const std::string extractCmd =
        "tar -xzf " + QuoteArg(archivePath.string()) + " -C " + QuoteArg(targetDir.string());
#endif

    const int exRc = RunBlockingCommand(extractCmd);
    std::filesystem::remove(archivePath, ec);

    if (exRc != 0) {
        std::filesystem::remove_all(targetDir, ec);
        if (outError) {
            *outError = "Error al extraer el paquete (codigo " + std::to_string(exRc) + ").";
        }
        return false;
    }

    EnsureExecutablePermissions(targetDir);

    if (FindEditorExecutableInDir(targetDir).empty()) {
        std::filesystem::remove_all(targetDir, ec);
        if (outError) {
            *outError = "El paquete descargado no contiene " + std::string(kEditorExeName) + ".";
        }
        return false;
    }

    nlohmann::json meta;
    meta["version"] = release.version;
    meta["tag"] = release.tagName;
    meta["channel"] = release.prerelease ? "prerelease" : "stable";
    meta["publishedAt"] = release.publishedAt;
    std::ofstream metaOut(targetDir / "kym_install.json", std::ios::trunc);
    if (metaOut.is_open()) {
        metaOut << meta.dump(2) << '\n';
    }

    return true;
}

bool VersionManager::UninstallEditor(const std::string& version, std::string* outError) {
    if (version.empty()) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path targetDir = GetEditorsRootDir() / version;
    if (!std::filesystem::exists(targetDir, ec)) {
        return true;
    }
    std::filesystem::remove_all(targetDir, ec);
    if (ec) {
        if (outError) {
            *outError = "No se pudo eliminar la carpeta: " + ec.message();
        }
        return false;
    }
    return true;
}

#ifdef _WIN32
std::wstring QuoteArgW(const std::wstring& arg) {
    std::wstring out = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') {
            out += L"\\\"";
        } else {
            out += c;
        }
    }
    out += L'"';
    return out;
}

std::filesystem::path FindWindowsJavaHome(const std::filesystem::path& editorWorkDir) {
    std::error_code ec;
    auto hasJvmDll = [&](const std::filesystem::path& home) -> bool {
        return std::filesystem::exists(home / "bin" / "server" / "jvm.dll", ec);
    };

    if (hasJvmDll(editorWorkDir / "jre")) {
        return editorWorkDir / "jre";
    }
    const std::filesystem::path hubJre = PlatformPaths::GetExecutableDir() / "jre";
    if (hasJvmDll(hubJre)) {
        return hubJre;
    }
    if (const wchar_t* envHome = _wgetenv(L"JAVA_HOME")) {
        if (*envHome != L'\0' && hasJvmDll(envHome)) {
            return envHome;
        }
    }

    const std::filesystem::path searchRoots[] = {
        L"C:\\Program Files\\Eclipse Adoptium",
        L"C:\\Program Files\\Java",
        L"C:\\Program Files\\Microsoft",
        L"C:\\Program Files\\Amazon Corretto",
        L"C:\\Program Files\\Zulu"
    };
    for (const auto& root : searchRoots) {
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
            continue;
        }
        for (const auto& sub : std::filesystem::directory_iterator(root, ec)) {
            if (sub.is_directory(ec) && hasJvmDll(sub.path())) {
                return sub.path();
            }
        }
    }
    return {};
}
#endif

bool VersionManager::LaunchEditor(
    const std::filesystem::path& editorExe,
    const std::filesystem::path& projectFile,
    std::string* outError
) {
    std::error_code ec;
    if (!std::filesystem::exists(editorExe, ec)) {
        if (outError) {
            *outError = "No se encuentra el ejecutable del editor: " + PlatformPaths::PathToUtf8(editorExe);
        }
        return false;
    }

    const std::filesystem::path workDir = editorExe.parent_path();

#ifdef _WIN32
    const std::filesystem::path javaHome = FindWindowsJavaHome(workDir);
    if (!javaHome.empty()) {
        ::SetEnvironmentVariableW(L"JAVA_HOME", javaHome.wstring().c_str());
        const wchar_t* oldPath = _wgetenv(L"PATH");
        std::wstring newPath =
            (javaHome / "bin" / "server").wstring() + L";" +
            (javaHome / "bin").wstring();
        if (oldPath && *oldPath != L'\0') {
            newPath += L";";
            newPath += oldPath;
        }
        ::SetEnvironmentVariableW(L"PATH", newPath.c_str());
    }

    std::wstring cmdLine = QuoteArgW(editorExe.wstring());
    if (!projectFile.empty()) {
        cmdLine += L" --project " + QuoteArgW(projectFile.wstring());
    }

    const std::filesystem::path logFile = PlatformPaths::GetUserDataDir() / "last_editor_launch.log";
    std::filesystem::create_directories(logFile.parent_path(), ec);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hLog = ::CreateFileW(
        logFile.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        si.hStdInput = nullptr;
    }
    PROCESS_INFORMATION pi{};
    const BOOL ok = ::CreateProcessW(
        editorExe.wstring().c_str(),
        cmdLine.data(),
        nullptr,
        nullptr,
        hLog != INVALID_HANDLE_VALUE ? TRUE : FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
        nullptr,
        workDir.wstring().c_str(),
        &si,
        &pi
    );
    if (hLog != INVALID_HANDLE_VALUE) {
        ::CloseHandle(hLog);
    }
    if (!ok) {
        if (outError) {
            *outError = "No se pudo iniciar KyumistEditor.exe (Windows error " + std::to_string(::GetLastError()) + ").";
        }
        return false;
    }

    const DWORD waitRes = ::WaitForSingleObject(pi.hProcess, 250);
    if (waitRes == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        ::GetExitCodeProcess(pi.hProcess, &exitCode);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        if (exitCode != 0) {
            if (outError) {
                *outError = "KyumistEditor.exe se cerro al iniciar (codigo " + std::to_string(exitCode) +
                            "). Revisa: " + PlatformPaths::PathToUtf8(logFile);
            }
            return false;
        }
        return true;
    }

    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return true;
#else
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (outError) {
            *outError = "No se pudo crear el proceso hijo (fork fallo).";
        }
        return false;
    }
    if (pid == 0) {
        ::setsid();
        const std::string workDirStr = workDir.string();
        if (!workDirStr.empty()) {
            (void)::chdir(workDirStr.c_str());
        }
        const std::string exeStr = editorExe.string();
        const std::string projStr = projectFile.string();
        if (projStr.empty()) {
            char* const argv[] = {const_cast<char*>(exeStr.c_str()), nullptr};
            ::execv(exeStr.c_str(), argv);
        } else {
            char flag[] = "--project";
            char* const argv[] = {
                const_cast<char*>(exeStr.c_str()),
                flag,
                const_cast<char*>(projStr.c_str()),
                nullptr
            };
            ::execv(exeStr.c_str(), argv);
        }
        ::_exit(127);
    }
    return true;
#endif
}

std::vector<HubProjectEntry> VersionManager::LoadProjects() {
    std::vector<HubProjectEntry> projects;
    std::error_code ec;

    auto readRegistry = [&](const std::filesystem::path& regFile) {
        if (!std::filesystem::exists(regFile, ec)) {
            return;
        }
        std::ifstream in(regFile);
        if (!in.is_open()) {
            return;
        }
        try {
            nlohmann::json root;
            in >> root;
            if (!root.contains("projects") || !root["projects"].is_array()) {
                return;
            }
            for (const auto& item : root["projects"]) {
                const std::string pathStr = item.value("path", "");
                if (pathStr.empty()) {
                    continue;
                }
                const std::filesystem::path projFile = PlatformPaths::Utf8ToPath(pathStr);
                const bool duplicate = std::any_of(projects.begin(), projects.end(), [&](const HubProjectEntry& p) {
                    return p.projectFile == projFile;
                });
                if (duplicate) {
                    continue;
                }

                HubProjectEntry entry;
                entry.name = item.value("name", PlatformPaths::PathToUtf8(projFile.parent_path().filename()));
                entry.projectFile = projFile;
                entry.fileExists = std::filesystem::exists(projFile, ec);
                entry.engineVersion = KM_ENGINE_VERSION;

                if (entry.fileExists) {
                    std::ifstream pin(projFile);
                    if (pin.is_open()) {
                        try {
                            nlohmann::json pjson;
                            pin >> pjson;
                            entry.name = pjson.value("name", entry.name);
                            entry.engineVersion = pjson.value(
                                "engineVersion",
                                pjson.value("version", std::string(KM_ENGINE_VERSION))
                            );
                        } catch (const std::exception&) {
                        }
                    }
                }
                projects.push_back(std::move(entry));
            }
        } catch (const std::exception&) {
        }
    };

    readRegistry(GetProjectsRegistryFile());
    return projects;
}

bool VersionManager::AddProject(const std::filesystem::path& projectFileOrDir, std::string* outError) {
    std::error_code ec;
    std::filesystem::path projFile = projectFileOrDir;
    if (std::filesystem::is_directory(projFile, ec)) {
        projFile /= "project.kmproj";
    }
    if (!std::filesystem::exists(projFile, ec)) {
        if (outError) {
            *outError = "No se encontro project.kmproj en: " + PlatformPaths::PathToUtf8(projFile);
        }
        return false;
    }

    const std::filesystem::path canon = std::filesystem::weakly_canonical(projFile, ec);
    const std::filesystem::path resolved = ec ? projFile : canon;
    const std::string resolvedUtf8 = PlatformPaths::PathToUtf8(resolved);

    std::string projName = PlatformPaths::PathToUtf8(resolved.parent_path().filename());
    std::ifstream pin(resolved);
    if (pin.is_open()) {
        try {
            nlohmann::json pjson;
            pin >> pjson;
            projName = pjson.value("name", projName);
        } catch (const std::exception&) {
        }
    }

    const std::filesystem::path regFile = GetProjectsRegistryFile();
    std::filesystem::create_directories(regFile.parent_path(), ec);

    nlohmann::json root;
    root["projects"] = nlohmann::json::array();
    if (std::filesystem::exists(regFile, ec)) {
        std::ifstream in(regFile);
        if (in.is_open()) {
            try {
                in >> root;
            } catch (const std::exception&) {
                root["projects"] = nlohmann::json::array();
            }
        }
    }

    nlohmann::json updated = nlohmann::json::array();
    updated.push_back({{"name", projName}, {"path", resolvedUtf8}});
    if (root.contains("projects") && root["projects"].is_array()) {
        for (const auto& item : root["projects"]) {
            if (item.value("path", "") != resolvedUtf8) {
                updated.push_back(item);
            }
        }
    }
    root["projects"] = updated;

    try {
        std::ofstream out(regFile, std::ios::trunc);
        if (!out.is_open()) {
            if (outError) {
                *outError = "No se pudo guardar el registro de proyectos.";
            }
            return false;
        }
        out << root.dump(2) << '\n';
    } catch (const std::exception& ex) {
        if (outError) {
            *outError = std::string("Error al guardar el registro: ") + ex.what();
        }
        return false;
    }
    return true;
}

void VersionManager::RemoveProject(const std::filesystem::path& projectFile) {
    std::error_code ec;
    const std::filesystem::path regFile = GetProjectsRegistryFile();
    if (!std::filesystem::exists(regFile, ec)) {
        return;
    }
    nlohmann::json root;
    {
        std::ifstream in(regFile);
        if (!in.is_open()) {
            return;
        }
        try {
            in >> root;
        } catch (const std::exception&) {
            return;
        }
    }
    if (!root.contains("projects") || !root["projects"].is_array()) {
        return;
    }
    const std::string targetUtf8 = PlatformPaths::PathToUtf8(projectFile);
    nlohmann::json filtered = nlohmann::json::array();
    for (const auto& item : root["projects"]) {
        if (item.value("path", "") != targetUtf8) {
            filtered.push_back(item);
        }
    }
    root["projects"] = filtered;
    try {
        std::ofstream out(regFile, std::ios::trunc);
        if (out.is_open()) {
            out << root.dump(2) << '\n';
        }
    } catch (const std::exception&) {
    }
}

bool VersionManager::UpdateProjectEngineVersion(
    const std::filesystem::path& projectFile,
    const std::string& newVersion,
    std::string* outError
) {
    std::error_code ec;
    if (!std::filesystem::exists(projectFile, ec)) {
        if (outError) {
            *outError = "El archivo del proyecto no existe.";
        }
        return false;
    }

    nlohmann::json pjson;
    {
        std::ifstream in(projectFile);
        if (!in.is_open()) {
            if (outError) {
                *outError = "No se pudo leer project.kmproj.";
            }
            return false;
        }
        try {
            in >> pjson;
        } catch (const std::exception& ex) {
            if (outError) {
                *outError = std::string("JSON invalido: ") + ex.what();
            }
            return false;
        }
    }

    const std::filesystem::path backupPath = projectFile.string() + ".bak";
    std::filesystem::copy_file(projectFile, backupPath, std::filesystem::copy_options::overwrite_existing, ec);

    pjson["engineVersion"] = newVersion;
    std::ofstream out(projectFile, std::ios::trunc);
    if (!out.is_open()) {
        if (outError) {
            *outError = "No se pudo escribir project.kmproj.";
        }
        return false;
    }
    out << pjson.dump(2) << '\n';
    return true;
}

int VersionManager::CompareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& v, std::vector<int>& nums, bool& hasPre) {
        const size_t dash = v.find('-');
        hasPre = (dash != std::string::npos);
        const std::string core = (dash == std::string::npos) ? v : v.substr(0, dash);
        size_t start = 0;
        while (start < core.size()) {
            size_t dot = core.find('.', start);
            if (dot == std::string::npos) {
                dot = core.size();
            }
            try {
                nums.push_back(std::stoi(core.substr(start, dot - start)));
            } catch (const std::exception&) {
                nums.push_back(0);
            }
            start = dot + 1;
        }
    };

    std::vector<int> na, nb;
    bool preA = false, preB = false;
    parse(NormalizeVersionTag(a), na, preA);
    parse(NormalizeVersionTag(b), nb, preB);

    const size_t n = std::max(na.size(), nb.size());
    na.resize(n, 0);
    nb.resize(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (na[i] != nb[i]) {
            return na[i] < nb[i] ? -1 : 1;
        }
    }
    if (preA != preB) {
        return preA ? -1 : 1;
    }
    return 0;
}

bool VersionManager::DownloadAndApplyHubUpdate(
    const RemoteRelease& release,
    bool* outShouldCloseHub,
    std::string* outMessage,
    std::string* outError
) {
    if (outShouldCloseHub) {
        *outShouldCloseHub = false;
    }
    if (release.hubInstallerUrl.empty() || release.hubInstallerName.empty()) {
        if (outError) {
            *outError = "Esta release no incluye instalador de actualizacion para tu sistema.";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::path downloadDir = std::filesystem::temp_directory_path(ec) / "KymEngineHubUpdate";
#ifndef _WIN32
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0' && std::filesystem::exists(std::filesystem::path(home) / "Descargas", ec)) {
            downloadDir = std::filesystem::path(home) / "Descargas";
        } else if (*home != '\0' && std::filesystem::exists(std::filesystem::path(home) / "Downloads", ec)) {
            downloadDir = std::filesystem::path(home) / "Downloads";
        }
    }
#endif
    std::filesystem::create_directories(downloadDir, ec);
    const std::filesystem::path targetFile = downloadDir / release.hubInstallerName;

    const std::string dlCmd =
        "curl -fL --retry 2 --connect-timeout 15 "
        "-H \"User-Agent: KymEngineHub\" "
        "-o " + QuoteArg(PlatformPaths::PathToUtf8(targetFile)) + " " + QuoteArg(release.hubInstallerUrl);

    const int dlRc = RunBlockingCommand(dlCmd);
    if (dlRc != 0 || !std::filesystem::exists(targetFile, ec)) {
        std::filesystem::remove(targetFile, ec);
        if (outError) {
            *outError = "No se pudo descargar la actualizacion del Hub (codigo " + std::to_string(dlRc) + ").";
        }
        return false;
    }

#ifdef _WIN32
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    const std::wstring exeW = targetFile.wstring();
    const std::wstring dirW = downloadDir.wstring();
    sei.lpFile = exeW.c_str();
    sei.lpDirectory = dirW.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!::ShellExecuteExW(&sei)) {
        if (outError) {
            *outError = "Instalador descargado en " + PlatformPaths::PathToUtf8(targetFile) +
                        ", pero no se pudo abrir automaticamente (error " +
                        std::to_string(::GetLastError()) + ").";
        }
        return false;
    }
    if (sei.hProcess) {
        ::CloseHandle(sei.hProcess);
    }
    if (outShouldCloseHub) {
        *outShouldCloseHub = true;
    }
    if (outMessage) {
        *outMessage = "Instalador iniciado; cerrando Hub para actualizar...";
    }
    return true;
#else
    if (const char* appImagePath = std::getenv("APPIMAGE")) {
        if (*appImagePath != '\0' && EndsWith(release.hubInstallerName, ".AppImage")) {
            ::chmod(targetFile.c_str(), 0755);
            const std::filesystem::path currentAppImage(appImagePath);
            std::filesystem::rename(targetFile, currentAppImage, ec);
            if (ec) {
                std::filesystem::copy_file(targetFile, currentAppImage, std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::remove(targetFile, ec);
            }
            if (!ec) {
                ::chmod(currentAppImage.c_str(), 0755);
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::setsid();
                    const std::string exeStr = currentAppImage.string();
                    char* const argv[] = {const_cast<char*>(exeStr.c_str()), nullptr};
                    ::execv(exeStr.c_str(), argv);
                    ::_exit(127);
                }
                if (outShouldCloseHub) {
                    *outShouldCloseHub = true;
                }
                if (outMessage) {
                    *outMessage = "AppImage actualizado; reiniciando KymEngine Hub...";
                }
                return true;
            }
        }
    }

    if (EndsWith(release.hubInstallerName, ".deb")) {
        if (RunBlockingCommand("command -v pkexec >/dev/null 2>&1") == 0) {
            const std::string pkCmd = "pkexec apt-get install -y " + QuoteArg(targetFile.string());
            const int pkRc = RunBlockingCommand(pkCmd);
            if (pkRc == 0) {
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::setsid();
                    char exe[] = "/usr/bin/kymengine";
                    char* const argv[] = {exe, nullptr};
                    ::execv(exe, argv);
                    ::_exit(127);
                }
                if (outShouldCloseHub) {
                    *outShouldCloseHub = true;
                }
                if (outMessage) {
                    *outMessage = "Hub actualizado correctamente; reiniciando...";
                }
                return true;
            }
        }
        if (outMessage) {
            *outMessage = "Descargado en " + targetFile.string() +
                          ". Ejecuta: sudo apt install \"" + targetFile.string() + "\"";
        }
        return true;
    }

    if (outMessage) {
        *outMessage = "Actualizacion descargada en: " + targetFile.string();
    }
    return true;
#endif
}

} // namespace Kyumist::Hub

