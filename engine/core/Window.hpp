#pragma once

#include <GLFW/glfw3.h>
#include <cstdint>
#include <string>

namespace Kyumist {

struct WindowProps {
    std::string title = "KymEngine";
    uint32_t width = 1280;
    uint32_t height = 720;
};

class Window {
public:
    explicit Window(const WindowProps& props = WindowProps{});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void PollEvents();
    bool ShouldClose() const;

    uint32_t GetWidth() const { return m_Data.width; }
    uint32_t GetHeight() const { return m_Data.height; }
    bool WasResized() const { return m_Data.framebufferResized; }
    void ResetResizedFlag() { m_Data.framebufferResized = false; }

    GLFWwindow* GetNativeWindow() const { return m_Window; }

private:
    struct WindowData {
        std::string title;
        uint32_t width = 1280;
        uint32_t height = 720;
        bool framebufferResized = false;
    };

    GLFWwindow* m_Window = nullptr;
    WindowData m_Data;
    bool m_GlfwInitialized = false;
};

} // namespace Kyumist

