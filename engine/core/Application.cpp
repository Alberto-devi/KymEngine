#include "core/Application.hpp"
#include "core/Log.hpp"

#include <chrono>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Kyumist {

Application::Application(const ApplicationSpecification& spec)
    : m_Spec(spec) {
    Log::Init();
    KM_CORE_INFO("Initializing {}...", m_Spec.name);

    uint32_t apiVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&apiVersion) == VK_SUCCESS) {
        KM_CORE_INFO(
            "Vulkan Instance Version available: {}.{}.{}",
            VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion),
            VK_API_VERSION_PATCH(apiVersion)
        );
    } else {
        KM_CORE_WARN("Failed to query Vulkan instance version.");
    }

    KM_CORE_TRACE("GLM active (sizeof(glm::mat4) = {} bytes).", sizeof(glm::mat4));

    if (!m_Spec.headless) {
        WindowProps props{m_Spec.name, m_Spec.width, m_Spec.height};
        m_Window = std::make_unique<Window>(props);
    } else {
        KM_CORE_INFO("Running in headless mode (no window created).");
    }

    m_Renderer = std::make_unique<VulkanRenderer>(m_Window.get(), m_Spec.headless);
}

Application::~Application() {
    m_Renderer.reset();
    m_Window.reset();
}

void Application::Run() {
    OnInit();

    auto lastTime = std::chrono::steady_clock::now();
    uint32_t frameCount = 0;

    while (!m_ShouldClose) {
        if (m_Window) {
            m_Window->PollEvents();
#ifndef __ANDROID__
            if (m_Window->ShouldClose() ||
                glfwGetKey(m_Window->GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                break;
            }
            if (m_Window->GetWidth() == 0 || m_Window->GetHeight() == 0) {
                glfwWaitEvents();
                continue;
            }
#else
            if (m_Window->ShouldClose()) {
                break;
            }
#endif
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        OnUpdate(Timestep(deltaSeconds));
        m_Renderer->BeginImGuiFrame();
        OnImGuiRender();
        m_Renderer->EndImGuiFrameAndDraw();
        ++frameCount;

        if (m_Spec.maxFrames > 0 && frameCount >= m_Spec.maxFrames) {
            break;
        }
    }

    OnShutdown();
}

} // namespace Kyumist

