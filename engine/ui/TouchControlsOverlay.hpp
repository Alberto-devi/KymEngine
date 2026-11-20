#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
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
    struct TouchPoint {
        int32_t pointerId = -1;
        float x = 0.0f;
        float y = 0.0f;
        bool active = false;
    };

    static TouchControlsConfig& GetConfig();
    static bool LoadFromProject(const std::filesystem::path& projectDir);
    static bool SaveToProject(const std::filesystem::path& projectDir);

    static void SetTouchPoints(const TouchPoint* points, size_t count);
    static void ClearTouchPoints();

    static bool ShouldShow(bool isMobilePlatform);
    static void RenderAndProcess(
        ImDrawList* drawList,
        ImVec2 viewportMin,
        ImVec2 viewportSize,
        bool allowInput
    );
};

} // namespace Kyumist

