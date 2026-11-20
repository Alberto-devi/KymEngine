#pragma once

#include <imgui.h>

#include <functional>
#include <string>

namespace Kyumist {

class VulkanRenderer;

class UISystem {
public:
    using UIEventCallback = std::function<void(const std::string& elementId, const std::string& eventType)>;

    static void Init(VulkanRenderer* renderer);
    static void Shutdown();
    static bool IsInitialized();

    static void SetEventCallback(UIEventCallback cb);

    static bool LoadDocument(const std::string& relativePath);
    static void UnloadDocument(const std::string& relativePath);
    static void UnloadAllDocuments();

    static bool SetText(const std::string& elementId, const std::string& text);
    static bool SetVisible(const std::string& elementId, bool visible);
    static bool SetClass(const std::string& elementId, const std::string& className, bool active);

    static void UpdateAndRender(
        ImDrawList* drawList,
        ImVec2 viewportMin,
        ImVec2 viewportSize,
        bool allowInput
    );
};

} // namespace Kyumist

