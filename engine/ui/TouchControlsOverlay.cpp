#include "ui/TouchControlsOverlay.hpp"

#include "core/InputMap.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

namespace Kyumist {

namespace {

TouchControlsConfig s_Config;
bool s_StickDragging = false;
std::vector<TouchControlsOverlay::TouchPoint> s_TouchPoints;
bool s_MultiTouchMode = false;
int32_t s_StickPointerId = -1;

} // namespace

void TouchControlsOverlay::SetTouchPoints(const TouchPoint* points, size_t count) {
    s_TouchPoints.clear();
    if (points && count > 0) {
        s_TouchPoints.assign(points, points + count);
    }
    s_MultiTouchMode = true;
}

void TouchControlsOverlay::ClearTouchPoints() {
    s_TouchPoints.clear();
    s_MultiTouchMode = false;
    s_StickPointerId = -1;
    s_StickDragging = false;
}

TouchControlsConfig& TouchControlsOverlay::GetConfig() {
    return s_Config;
}

bool TouchControlsOverlay::LoadFromProject(const std::filesystem::path& projectDir) {
    s_Config = TouchControlsConfig{};
    std::error_code ec;
    const std::filesystem::path projFile = projectDir / "project.kmproj";
    if (!std::filesystem::exists(projFile, ec)) {
        return false;
    }
    std::ifstream in(projFile);
    if (!in.is_open()) {
        return false;
    }
    try {
        nlohmann::json root;
        in >> root;
        if (!root.contains("touchControls") || !root["touchControls"].is_object()) {
            return false;
        }
        const auto& tc = root["touchControls"];
        s_Config.enabledOnMobile = tc.value("enabledOnMobile", true);
        s_Config.simulateInEditor = tc.value("simulateInEditor", false);
        s_Config.buttonAAction = tc.value("buttonAAction", std::string("jump"));
        s_Config.buttonALabel = tc.value("buttonALabel", std::string("A"));
        s_Config.buttonBAction = tc.value("buttonBAction", std::string("action"));
        s_Config.buttonBLabel = tc.value("buttonBLabel", std::string("B"));
        s_Config.opacity = std::clamp(tc.value("opacity", 0.72f), 0.15f, 1.0f);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool TouchControlsOverlay::SaveToProject(const std::filesystem::path& projectDir) {
    std::error_code ec;
    const std::filesystem::path projFile = projectDir / "project.kmproj";
    if (!std::filesystem::exists(projFile, ec)) {
        return false;
    }
    nlohmann::json root;
    {
        std::ifstream in(projFile);
        if (!in.is_open()) {
            return false;
        }
        try {
            in >> root;
        } catch (const std::exception&) {
            return false;
        }
    }

    root["touchControls"] = {
        {"enabledOnMobile", s_Config.enabledOnMobile},
        {"simulateInEditor", s_Config.simulateInEditor},
        {"buttonAAction", s_Config.buttonAAction},
        {"buttonALabel", s_Config.buttonALabel},
        {"buttonBAction", s_Config.buttonBAction},
        {"buttonBLabel", s_Config.buttonBLabel},
        {"opacity", s_Config.opacity}
    };

    try {
        std::ofstream out(projFile, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << root.dump(2) << '\n';
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool TouchControlsOverlay::ShouldShow(bool isMobilePlatform) {
    if (isMobilePlatform) {
        return s_Config.enabledOnMobile;
    }
    return s_Config.simulateInEditor;
}

void TouchControlsOverlay::RenderAndProcess(
    ImDrawList* drawList,
    ImVec2 viewportMin,
    ImVec2 viewportSize,
    bool allowInput
) {
    if (viewportSize.x <= 80.0f || viewportSize.y <= 80.0f) {
        return;
    }

    const float minDim = std::min(viewportSize.x, viewportSize.y);
    const float stickRadius = std::clamp(minDim * 0.115f, 44.0f, 130.0f);
    const float knobRadius = stickRadius * 0.42f;
    const float btnRadius = std::clamp(minDim * 0.075f, 28.0f, 82.0f);
    const float margin = stickRadius + std::clamp(minDim * 0.04f, 24.0f, 52.0f);

    const ImVec2 stickCenter(
        viewportMin.x + margin,
        viewportMin.y + viewportSize.y - margin
    );
    const ImVec2 btnACenter(
        viewportMin.x + viewportSize.x - margin + 10.0f,
        viewportMin.y + viewportSize.y - margin + 12.0f
    );
    const ImVec2 btnBCenter(
        btnACenter.x - btnRadius * 2.25f,
        btnACenter.y - btnRadius * 0.75f
    );

    auto distSq = [](ImVec2 a, ImVec2 b) -> float {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    };

    ImVec2 knobPos = stickCenter;
    float axisX = 0.0f;
    float axisY = 0.0f;
    bool btnAPressed = false;
    bool btnBPressed = false;

    if (s_MultiTouchMode) {
        if (!allowInput) {
            s_StickPointerId = -1;
            s_StickDragging = false;
        } else {
            ImVec2 stickPos = stickCenter;
            bool foundStick = false;
            if (s_StickPointerId != -1) {
                for (const auto& pt : s_TouchPoints) {
                    if (pt.active && pt.pointerId == s_StickPointerId) {
                        stickPos = ImVec2(pt.x, pt.y);
                        foundStick = true;
                        break;
                    }
                }
                if (!foundStick) {
                    s_StickPointerId = -1;
                }
            }

            if (s_StickPointerId == -1) {
                const float captureR = stickRadius * 1.35f;
                const float captureR2 = captureR * captureR;
                for (const auto& pt : s_TouchPoints) {
                    if (!pt.active) {
                        continue;
                    }
                    const ImVec2 ptPos(pt.x, pt.y);
                    if (distSq(ptPos, stickCenter) <= captureR2) {
                        s_StickPointerId = pt.pointerId;
                        stickPos = ptPos;
                        foundStick = true;
                        break;
                    }
                }
            }

            s_StickDragging = foundStick;
            if (s_StickDragging) {
                float dx = stickPos.x - stickCenter.x;
                float dy = stickPos.y - stickCenter.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > stickRadius && len > 0.0001f) {
                    dx = (dx / len) * stickRadius;
                    dy = (dy / len) * stickRadius;
                }
                knobPos = ImVec2(stickCenter.x + dx, stickCenter.y + dy);
                axisX = dx / stickRadius;
                axisY = -dy / stickRadius;
            }

            const float btnHitR = btnRadius * 1.25f;
            const float btnHitR2 = btnHitR * btnHitR;
            for (const auto& pt : s_TouchPoints) {
                if (!pt.active || pt.pointerId == s_StickPointerId) {
                    continue;
                }
                const ImVec2 ptPos(pt.x, pt.y);
                if (distSq(ptPos, btnACenter) <= btnHitR2) {
                    btnAPressed = true;
                }
                if (distSq(ptPos, btnBCenter) <= btnHitR2) {
                    btnBPressed = true;
                }
            }
        }
    } else if (ImGui::GetCurrentContext() != nullptr) {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;
        const bool mouseDown = allowInput && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool mouseClicked = allowInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        if (!mouseDown) {
            s_StickDragging = false;
        } else if (mouseClicked && distSq(mouse, stickCenter) <= (stickRadius * 1.25f) * (stickRadius * 1.25f)) {
            s_StickDragging = true;
        }

        if (s_StickDragging) {
            float dx = mouse.x - stickCenter.x;
            float dy = mouse.y - stickCenter.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > stickRadius && len > 0.0001f) {
                dx = (dx / len) * stickRadius;
                dy = (dy / len) * stickRadius;
            }
            knobPos = ImVec2(stickCenter.x + dx, stickCenter.y + dy);
            axisX = dx / stickRadius;
            axisY = -dy / stickRadius; // up = +1
        }

        btnAPressed = mouseDown && !s_StickDragging && distSq(mouse, btnACenter) <= btnRadius * btnRadius;
        btnBPressed = mouseDown && !s_StickDragging && distSq(mouse, btnBCenter) <= btnRadius * btnRadius;
    }

    InputMap::SetTouchAxis(axisX, axisY);
    constexpr float kDeadzone = 0.28f;
    InputMap::SetVirtualActionState("move_left", axisX < -kDeadzone);
    InputMap::SetVirtualActionState("move_right", axisX > kDeadzone);
    InputMap::SetVirtualActionState("move_forward", axisY > kDeadzone);
    InputMap::SetVirtualActionState("move_back", axisY < -kDeadzone);

    if (!s_Config.buttonAAction.empty()) {
        InputMap::SetVirtualActionState(s_Config.buttonAAction, btnAPressed);
    }
    if (!s_Config.buttonBAction.empty()) {
        InputMap::SetVirtualActionState(s_Config.buttonBAction, btnBPressed);
    }

    if (!drawList) {
        return;
    }

    const int alphaBase = static_cast<int>(s_Config.opacity * 170.0f);
    const int alphaBorder = static_cast<int>(s_Config.opacity * 235.0f);

    // Draw joystick base and knob
    drawList->AddCircleFilled(stickCenter, stickRadius, IM_COL32(20, 26, 36, alphaBase), 40);
    drawList->AddCircle(stickCenter, stickRadius, IM_COL32(130, 170, 230, alphaBorder), 40, 2.0f);
    drawList->AddCircleFilled(
        knobPos,
        knobRadius,
        s_StickDragging ? IM_COL32(70, 155, 255, alphaBorder) : IM_COL32(190, 210, 235, alphaBase + 30),
        32
    );

    auto drawActionBtn = [&](ImVec2 center, const std::string& label, bool pressed, ImU32 activeCol) {
        drawList->AddCircleFilled(
            center,
            btnRadius,
            pressed ? activeCol : IM_COL32(22, 28, 38, alphaBase),
            32
        );
        drawList->AddCircle(center, btnRadius, IM_COL32(200, 220, 245, alphaBorder), 32, 2.0f);
        const ImVec2 txtSz = ImGui::CalcTextSize(label.c_str());
        drawList->AddText(
            ImVec2(center.x - txtSz.x * 0.5f, center.y - txtSz.y * 0.5f),
            IM_COL32(245, 248, 255, 255),
            label.c_str()
        );
    };

    drawActionBtn(btnACenter, s_Config.buttonALabel, btnAPressed, IM_COL32(48, 175, 110, 230));
    drawActionBtn(btnBCenter, s_Config.buttonBLabel, btnBPressed, IM_COL32(220, 115, 45, 230));
}

} // namespace Kyumist

