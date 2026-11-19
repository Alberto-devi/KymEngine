#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Kyumist {

struct ProjectConfig {
    std::string name;
    std::string version = "0.1.0";
    std::string startScene = "scenes/main.kmscene";
    std::filesystem::path projectDir;
};

struct RecentProjectEntry {
    std::string name;
    std::string projectFilePath;
};

struct SampleProjectEntry {
    std::string id;
    std::string name;
    std::string categoryBadge;
    std::string description;
    std::string projectFilePath;
    int templateIndex = 0;
};

class ProjectManager {
public:
    static void EnsureDefaultSampleProject();
    static void EnsureSampleProjects();
    static std::vector<SampleProjectEntry> GetSampleProjects();
    static bool ResetSampleProject(int sampleIndex, std::string* outError = nullptr);

    static bool CreateNewProject(
        const std::filesystem::path& parentDir,
        const std::string& projectName,
        int templateIndex,
        std::string* outError = nullptr
    );

    static bool OpenProject(const std::filesystem::path& projectFileOrDir);
    static void CloseActiveProject();

    static bool HasActiveProject();
    static const ProjectConfig& GetActiveProject();
    static std::filesystem::path GetActiveProjectDir();
    static std::filesystem::path GetStartScenePath();

    static std::vector<RecentProjectEntry> GetRecentProjects();
    static void RemoveRecentProject(const std::string& projectFilePath);

    static bool ExportProjectBuild(
        const std::filesystem::path& outputDir,
        std::string* outError = nullptr
    );
};

} // namespace Kyumist

