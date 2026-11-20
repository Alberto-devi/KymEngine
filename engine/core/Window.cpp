#include "core/Window.hpp"
#include "core/Log.hpp"

#ifdef __ANDROID__
#include <android/native_window.h>
#endif

#include <stdexcept>

namespace Kyumist {

#ifdef __ANDROID__
namespace {
ANativeWindow* s_AndroidWin = nullptr;
uint32_t s_AndroidW = 1280;
uint32_t s_AndroidH = 720;
bool s_AndroidClose = false;
} // namespace

void Window::SetAndroidWindow(ANativeWindow* win, uint32_t w, uint32_t h) {
    s_AndroidWin = win;
    s_AndroidW = w > 0 ? w : 1280;
    s_AndroidH = h > 0 ? h : 720;
}

void Window::RequestAndroidClose() {
    s_AndroidClose = true;
}

Window::Window(const WindowProps& props) {
    m_Data.title = props.title;
    m_AndroidWindow = s_AndroidWin;
    m_Data.width = s_AndroidWin ? static_cast<uint32_t>(ANativeWindow_getWidth(s_AndroidWin)) : s_AndroidW;
    m_Data.height = s_AndroidWin ? static_cast<uint32_t>(ANativeWindow_getHeight(s_AndroidWin)) : s_AndroidH;
}

Window::~Window() {
    m_AndroidWindow = nullptr;
}

void Window::PollEvents() {
    if (s_AndroidWin) {
        const uint32_t nw = static_cast<uint32_t>(ANativeWindow_getWidth(s_AndroidWin));
        const uint32_t nh = static_cast<uint32_t>(ANativeWindow_getHeight(s_AndroidWin));
        if (nw > 0 && nh > 0 && (nw != m_Data.width || nh != m_Data.height)) {
            m_Data.width = nw;
            m_Data.height = nh;
            m_Data.framebufferResized = true;
        }
    }
}

bool Window::ShouldClose() const {
    return s_AndroidClose;
}

#else

namespace {

void GlfwErrorCallback(int error, const char* description) {
    KM_CORE_ERROR("GLFW Error ({}): {}", error, description ? description : "unknown");
}

} // namespace

Window::Window(const WindowProps& props) {
    m_Data.title = props.title;
    m_Data.width = props.width;
    m_Data.height = props.height;

    glfwSetErrorCallback(GlfwErrorCallback);

    if (!glfwInit()) {
        KM_CORE_CRITICAL("Failed to initialize GLFW.");
        throw std::runtime_error("Failed to initialize GLFW");
    }
    m_GlfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_Window = glfwCreateWindow(
        static_cast<int>(m_Data.width),
        static_cast<int>(m_Data.height),
        m_Data.title.c_str(),
        nullptr,
        nullptr
    );

    if (!m_Window) {
        KM_CORE_CRITICAL("Failed to create GLFW window '{}'.", m_Data.title);
        glfwTerminate();
        m_GlfwInitialized = false;
        throw std::runtime_error("Failed to create GLFW window");
    }

    KM_CORE_INFO("Created window '{}' ({}x{}).", m_Data.title, m_Data.width, m_Data.height);

    if (glfwVulkanSupported() == GLFW_TRUE) {
        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        KM_CORE_INFO("GLFW Vulkan supported. Required instance extensions ({}):", extensionCount);
        for (uint32_t i = 0; i < extensionCount; ++i) {
            KM_CORE_TRACE("  - {}", extensions[i]);
        }
    } else {
        KM_CORE_WARN("GLFW reports Vulkan is NOT supported in this environment.");
    }

    glfwSetWindowUserPointer(m_Window, &m_Data);
    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
        auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (!data) {
            return;
        }
        data->width = static_cast<uint32_t>(width > 0 ? width : 0);
        data->height = static_cast<uint32_t>(height > 0 ? height : 0);
        data->framebufferResized = true;
    });
}

Window::~Window() {
    if (m_Window) {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
    if (m_GlfwInitialized) {
        glfwTerminate();
        m_GlfwInitialized = false;
    }
}

void Window::PollEvents() {
    glfwPollEvents();
}

bool Window::ShouldClose() const {
    return m_Window ? glfwWindowShouldClose(m_Window) != 0 : true;
}

#endif

} // namespace Kyumist

