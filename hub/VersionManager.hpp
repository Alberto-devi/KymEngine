#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kyumist::Hub {

enum class ReleaseChannel {
    Stable,
    Prerelease,
    Local
};

struct InstalledEditor {
    std::string version;
    ReleaseChannel channel = ReleaseChannel::Stable;
    std::filesystem::path rootDir;
    std::filesystem::path executablePath;
    bool isBundled = false;
};

struct RemoteRelease {
    std::string tagName;
    std::string version;
    std::string title;
    std::string publishedAt;
    std::string assetName;
    std::string downloadUrl;
    uint64_t assetSizeBytes = 0;
    std::string hubInstallerName;
    std::string hubInstallerUrl;
    uint64_t hubInstallerSizeBytes = 0;
    bool prerelease = false;
};

struct HubProjectEntry {
    std::string name;
    std::filesystem::path projectFile;
    std::string engineVersion;
    bool fileExists = false;
};

class VersionManager {
public:
    static std::filesystem::path GetEditorsRootDir();
    static std::filesystem::path GetProjectsRegistryFile();

    static std::vector<InstalledEditor> ScanInstalledEditors();
    static const InstalledEditor* FindInstalledVersion(
        const std::vector<InstalledEditor>& editors,
        const std::string& version
    );

    static std::vector<RemoteRelease> FetchRemoteReleases(std::string* outError = nullptr);
    static bool InstallRelease(
        const RemoteRelease& release,
        std::string* outStatus = nullptr,
        std::string* outError = nullptr
    );
    static bool UninstallEditor(const std::string& version, std::string* outError = nullptr);

    static bool LaunchEditor(
        const std::filesystem::path& editorExe,
        const std::filesystem::path& projectFile,
        std::string* outError = nullptr
    );

    static std::vector<HubProjectEntry> LoadProjects();
    static bool AddProject(const std::filesystem::path& projectFileOrDir, std::string* outError = nullptr);
    static void RemoveProject(const std::filesystem::path& projectFile);
    static bool DeleteProjectFromDisk(const std::filesystem::path& projectFile, std::string* outError = nullptr);
    static std::string PickFolderDialog(const std::string& title, const std::filesystem::path& initialDir = {});
    static bool UpdateProjectEngineVersion(
        const std::filesystem::path& projectFile,
        const std::string& newVersion,
        std::string* outError = nullptr
    );

    static int CompareVersions(const std::string& a, const std::string& b);
    static bool DownloadAndApplyHubUpdate(
        const RemoteRelease& release,
        bool* outShouldCloseHub,
        std::string* outMessage = nullptr,
        std::string* outError = nullptr
    );
};

} // namespace Kyumist::Hub

