#pragma once

#include <imgui.h>

#include <filesystem>
#include <string>

namespace Kyumist {

struct TouchControlsConfig {
    bool enabledOnMobile = true;
    bool simulateInEditor = false;
    std::string buttonAAction = "jump";
    std::string buttonALabel = "A";
    std::string buttonBAction = "action";
    std::string buttonBLabel = "B";
    float opacity = 0.72f;
};

class TouchControlsOverlay {
public:
    static TouchControlsConfig& GetConfig();
    static bool LoadFromProject(const std::filesystem::path& projectDir);
    static bool SaveToProject(const std::filesystem::path& projectDir);

    static bool ShouldShow(bool isMobilePlatform);
    static void RenderAndProcess(
        ImDrawList* drawList,
        ImVec2 viewportMin,
        ImVec2 viewportSize,
        bool allowInput
    );
};

} // namespace Kyumist

