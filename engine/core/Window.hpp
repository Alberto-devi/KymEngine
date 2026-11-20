#pragma once

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
struct ANativeWindow;
struct GLFWwindow;
#endif
#include <cstdint>
#include <functional>
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
#ifdef __ANDROID__
    ANativeWindow* GetAndroidWindow() const { return m_AndroidWindow; }
    void UpdateAndroidWindow(ANativeWindow* win);
    static void SetAndroidWindow(ANativeWindow* win, uint32_t w, uint32_t h);
    static void SetAndroidEventPump(std::function<void()> pump);
    static void MarkAndroidResized(uint32_t w, uint32_t h);
    static void RequestAndroidClose();
#endif

private:
    struct WindowData {
        std::string title;
        uint32_t width = 1280;
        uint32_t height = 720;
        bool framebufferResized = false;
    };

    GLFWwindow* m_Window = nullptr;
#ifdef __ANDROID__
    ANativeWindow* m_AndroidWindow = nullptr;
#endif
    WindowData m_Data;
    bool m_GlfwInitialized = false;
};

} // namespace Kyumist

